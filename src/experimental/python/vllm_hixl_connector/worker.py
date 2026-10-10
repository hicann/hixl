# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""Worker-side logic of the HIXL connector."""

import copy
import logging
import math
import os
import random
import threading
import time
from collections import OrderedDict
from typing import Any, NamedTuple
import numpy as np
import msgspec
import torch
from vllm.config import VllmConfig
from vllm.distributed import get_pcp_group
from vllm.distributed.kv_transfer.kv_connector.utils import BlockIds
from vllm.distributed.parallel_state import (
    get_pp_group,
    get_tensor_model_parallel_rank,
    get_tp_group,
)
from vllm.logger import logger
from vllm.utils.math_utils import cdiv
from vllm.utils.network_utils import get_ip
from vllm.v1.kv_cache_interface import (
    FullAttentionSpec,
    KVCacheConfig,
    MambaSpec,
    MLAAttentionSpec,
    UniformTypeKVCacheSpecs,
)
from vllm_ascend.ascend_config import get_ascend_config
from vllm_ascend.distributed.kv_transfer.utils.utils import (
    RegisterRegions,
    collect_storage_merged_register_regions,
    get_transfer_timeout_value,
)
from vllm_ascend.distributed.utils import (
    get_decode_context_model_parallel_rank,
    get_decode_context_model_parallel_world_size,
)
from vllm_ascend.utils import enable_sfa_dcp_replicated_indexer
from .hixl_wrapper import _Hixl
from .metadata import (
    GroupPull,
    HIXLAgentMetadata,
    HIXLConnectorMetadata,
    RemotePortInfo,
    ReqMeta,
)
from .transfer_threads import KVCacheRecvingThread, KVCacheSendingThread
from .utils import string_to_int64_hash


def _kv_head_groups(
    num_key_value_heads: int,
    tp_size: int,
    use_mla: bool,
    use_sparse: bool,
) -> list[tuple[int, ...]]:
    """Group KV head indices per TP rank for the CP port mapping.

    MLA/sparse models share a single head group; otherwise heads are
    split evenly across ranks (tp <= heads) or replicated one head per
    group (tp >= 2 * heads).
    """
    if use_mla or use_sparse:
        return [tuple([0])]
    if num_key_value_heads // tp_size >= 1:
        kv_head_groups = []
        for tp_rank in range(tp_size):
            kv_head_ids = [
                head_idx + tp_rank * (num_key_value_heads // tp_size)
                for head_idx in range(num_key_value_heads // tp_size)
            ]
            kv_head_groups.append(tuple(kv_head_ids))
        return kv_head_groups
    if tp_size // num_key_value_heads > 1:
        kv_head_groups = []
        for kv_head_idx in range(num_key_value_heads):
            kv_head_groups.append(tuple([kv_head_idx]))
        return kv_head_groups
    raise ValueError(
        f"Unsupported TP layout: tp_size({tp_size}) must be <= "
        f"num_key_value_heads({num_key_value_heads}) or >= 2 * "
        f"num_key_value_heads({num_key_value_heads})."
    )


def _cp_group_meta(
    num_key_value_heads: int,
    use_mla: bool,
    use_sparse: bool,
    tp_size: int,
    pcp_size: int,
    dcp_size: int,
    port_base: int,
) -> dict[tuple[int, ...], dict]:
    """Build the per-head-group CP port groups used to pair P and D ranks.

    Key is a kv head group; value carries the ``pcp_size * dcp_size``
    port groups plus the round-robin cursor ``select_cp_groups_id`` used
    when pairing D-side CP groups against P-side replicas.
    """
    cp_group_meta: dict[tuple[int, ...], dict] = {}
    kv_head_groups = _kv_head_groups(num_key_value_heads, tp_size, use_mla, use_sparse)
    dcp_repeat_num = tp_size // len(kv_head_groups) // dcp_size

    for kv_head_group_idx, kv_head_group in enumerate(kv_head_groups):
        if kv_head_group not in cp_group_meta:
            cp_group_meta[kv_head_group] = {}
            cp_group_meta[kv_head_group]["cp_groups"] = []
            cp_group_meta[kv_head_group]["select_cp_groups_id"] = 0
        kv_head_group_offset = tp_size // len(kv_head_groups) * kv_head_group_idx
        for dcp_repeat_idx in range(dcp_repeat_num):
            # len(cp_group) == pcp_size * dcp_size
            cp_group = []
            dcp_repeat_offset = dcp_size * dcp_repeat_idx
            for pcp_rank in range(pcp_size):
                pcp_rank_offset = tp_size * pcp_rank
                for dcp_rank in range(dcp_size):
                    cp_group.append(
                        dcp_rank
                        + port_base
                        + pcp_rank_offset
                        + dcp_repeat_offset
                        + kv_head_group_offset
                    )
            cp_group_meta[kv_head_group]["cp_groups"].append(cp_group)

    return cp_group_meta


class KVSplitResult(NamedTuple):
    """Per-shard transfer plan built by ``_get_kv_split_metadata``.

    Field names anchor the three aligned lists so a mixed-up return
    fails loudly at the attribute level instead of silently swapping
    remote/local block ids downstream.
    """

    remote_handshake_port_list: list[list[int]]
    local_block_ids_list: list[BlockIds]
    remote_block_ids_list: list[BlockIds]


class HIXLConnectorWorker:
    """Implementation of Worker side methods"""

    def __init__(
        self, vllm_config: VllmConfig, engine_id: str, kv_cache_config: KVCacheConfig
    ):
        self._get_prefill_decode_size(vllm_config)
        # Process-wide side effect: the hixl engine reads this variable for its
        # transfer timeout, so every other component in this process that
        # honours ASCEND_TRANSFER_TIMEOUT is affected as well. Set here because
        # engine initialization precedes the first transfer.
        os.environ["ASCEND_TRANSFER_TIMEOUT"] = str(get_transfer_timeout_value())
        if self._prefill_tp_size < self._decode_tp_size:
            raise ValueError(
                f"prefill_tp_size: {self._prefill_tp_size} must be greater than"
                f" or equal to the decode_tp_size: {self._decode_tp_size}"
            )

        # Metadata.
        self.vllm_config = vllm_config
        self.ascend_config = get_ascend_config()
        self.engine_id = engine_id
        self.tp_rank = get_tensor_model_parallel_rank()
        self.tp_size = vllm_config.parallel_config.tensor_parallel_size
        self.tp_group = get_tp_group()
        self.pp_rank = get_pp_group().rank_in_group
        self.dp_rank = vllm_config.parallel_config.data_parallel_rank_local
        self.dp_size = vllm_config.parallel_config.data_parallel_size_local
        self.pp_size = vllm_config.parallel_config.pipeline_parallel_size
        self.kv_caches: dict[str, torch.Tensor] = {}
        self.side_channel_host = get_ip()
        self.pcp_size = get_pcp_group().world_size
        self.total_layers = vllm_config.model_config.get_total_num_hidden_layers()
        # pp_size and pcp_size cannot both be greater than 1
        if self.pp_size > 1 and self.pcp_size > 1:
            raise ValueError(
                f"pp_size({self.pp_size}) and pcp_size({self.pcp_size}) cannot "
                "both be greater than 1."
            )
        self.pcp_rank = get_pcp_group().rank_in_group if self.pcp_size > 1 else 0
        self.dcp_size = get_decode_context_model_parallel_world_size()
        self.dcp_rank = (
            get_decode_context_model_parallel_rank() if self.dcp_size > 1 else 0
        )

        self.max_device_id = self.tp_size * self.dp_size * self.pcp_size * self.pp_size
        self.kv_role = vllm_config.kv_transfer_config.kv_role
        self.num_key_value_heads = (
            self.vllm_config.model_config.hf_text_config.num_key_value_heads
        )

        # kv cache config
        self.kv_cache_config = kv_cache_config
        self.num_blocks: int = kv_cache_config.num_blocks
        self.kv_group2layeridx: dict[int, tuple[dict[str, Any], list[int]]] = {}
        self.use_hybrid = (
            not self.vllm_config.scheduler_config.disable_hybrid_kv_cache_manager
            and any(
                not isinstance(g.kv_cache_spec, FullAttentionSpec)
                for g in self.kv_cache_config.kv_cache_groups
            )
            and len(self.kv_cache_config.kv_cache_groups) > 1
        )
        self._is_hma_required = (
            not vllm_config.scheduler_config.disable_hybrid_kv_cache_manager
            and any(
                not isinstance(g.kv_cache_spec, FullAttentionSpec)
                for g in kv_cache_config.kv_cache_groups
            )
        )
        self._layer_specs = {
            layer: group.kv_cache_spec
            for group in kv_cache_config.kv_cache_groups
            for layer in group.layer_names
        }

        # Handshake base port
        dp_offset = (
            vllm_config.parallel_config.data_parallel_rank
            * vllm_config.parallel_config.tensor_parallel_size
            * vllm_config.parallel_config.pipeline_parallel_size
            * self.pcp_size
        )
        self.side_channel_port = vllm_config.kv_transfer_config.kv_port + dp_offset
        device_index = (
            self.pp_rank * self.pcp_size + self.pcp_rank
        ) * self.tp_size + self.tp_rank
        self.handshake_port = self.side_channel_port + device_index
        self.sockets: dict = {}
        self.engine = _Hixl.from_vllm_config(
            vllm_config,
            host=self.side_channel_host,
            dp_offset=dp_offset,
            device_index=device_index,
        )
        self.engine.initialize()
        self.listen_port = self.engine.listen_port

        # Background thread for sending or receiving KV caches.
        self.kv_send_thread: KVCacheSendingThread | None = None
        self.kv_recv_thread: KVCacheRecvingThread | None = None

        # Handshake metadata of this worker
        self.xfer_handshake_metadata: HIXLAgentMetadata | None = None

        # kv_transfer variables
        self.vllm_config = vllm_config
        self.block_size = vllm_config.cache_config.block_size
        if self.vllm_config.model_config.is_deepseek_mla:
            self.tp_num_need_pulls = 1
        else:
            num_d_block_heads = max(1, self.num_key_value_heads // self.tp_size)
            num_p_block_heads = max(
                1, self.num_key_value_heads // self._prefill_tp_size
            )
            self.tp_num_need_pulls = num_d_block_heads // num_p_block_heads
        self.local_remote_block_port_mapping: dict[str, list[list[int]] | None] = {}
        self.remote_port_send_num: dict[str, dict[int, RemotePortInfo]] = {}

    def _get_prefill_decode_size(self, vllm_config: VllmConfig):
        # get prefill tp and dp size from extra config
        prefill_parallel_config: dict[str, Any] = (
            vllm_config.kv_transfer_config.get_from_extra_config("prefill", {})
        )

        if "tp_size" not in prefill_parallel_config:
            raise ValueError(
                "extra_config.prefill.tp_size is required in kv_transfer_config."
            )
        self._prefill_tp_size = prefill_parallel_config["tp_size"]

        if "dp_size" not in prefill_parallel_config:
            raise ValueError(
                "extra_config.prefill.dp_size is required in kv_transfer_config."
            )
        self._prefill_dp_size = prefill_parallel_config["dp_size"]
        # get prefill pp size from extra config
        self._prefill_pp_size = prefill_parallel_config.get("pp_size", 1)
        # get decode tp and dp size from extra config
        decode_parallel_config: dict[str, Any] = (
            vllm_config.kv_transfer_config.get_from_extra_config("decode", {})
        )
        if "tp_size" not in decode_parallel_config:
            raise ValueError(
                "extra_config.decode.tp_size is required in kv_transfer_config."
            )
        self._decode_tp_size = decode_parallel_config["tp_size"]
        if "dp_size" not in decode_parallel_config:
            raise ValueError(
                "extra_config.decode.dp_size is required in kv_transfer_config."
            )
        self._decode_dp_size = decode_parallel_config["dp_size"]
        # get prefill pp size from extra config
        self._decode_pp_size = decode_parallel_config.get("pp_size", 1)
        if self._decode_pp_size != 1:
            raise ValueError(
                f"decode pp_size({self._decode_pp_size}) must be 1; pipeline "
                "parallelism on the decode side is not supported."
            )
        self._prefill_pp_layer_partition = prefill_parallel_config.get(
            "pp_layer_partition"
        )

    @staticmethod
    def _serialize_kv_group_spec(
        group_spec: Any,
        layer_names: list[str] | None = None,
        kv_cache_spec: Any | None = None,
        kv_cache_group_id: int | None = None,
        total_num_kv_heads: int | None = None,
    ) -> dict[str, Any]:
        def to_msgpackable(value: Any) -> Any:
            if value is None or isinstance(value, (str, int, float, bool)):
                return value
            if isinstance(value, dict):
                return {str(k): to_msgpackable(v) for k, v in value.items()}
            if isinstance(value, (list, tuple)):
                return [to_msgpackable(item) for item in value]
            try:
                builtins_value = msgspec.to_builtins(value)
                if builtins_value is value:
                    return repr(value)
                return to_msgpackable(builtins_value)
            except TypeError:
                return repr(value)

        if layer_names is None:
            layer_names = list(group_spec.layer_names)
        if kv_cache_spec is None:
            kv_cache_spec = group_spec.kv_cache_spec
        spec = kv_cache_spec
        if isinstance(kv_cache_spec, UniformTypeKVCacheSpecs):
            spec = {
                layer_name: kv_cache_spec.kv_cache_specs[layer_name]
                for layer_name in layer_names
            }
        serialized_kv_cache_spec = to_msgpackable(spec)
        if not isinstance(serialized_kv_cache_spec, dict):
            serialized_kv_cache_spec = {"repr": serialized_kv_cache_spec}
        num_key_value_heads = HIXLConnectorWorker._get_spec_num_key_value_heads(spec)
        if num_key_value_heads is not None:
            serialized_kv_cache_spec["num_kv_heads"] = num_key_value_heads
            serialized_kv_cache_spec["num_key_value_heads"] = num_key_value_heads
        if total_num_kv_heads is not None:
            serialized_kv_cache_spec["total_num_kv_heads"] = total_num_kv_heads

        serialized = {
            "layer_names": layer_names,
            "kv_cache_spec_type": type(kv_cache_spec).__name__,
            "kv_cache_spec": serialized_kv_cache_spec,
        }
        if kv_cache_group_id is not None:
            serialized["kv_cache_group_id"] = kv_cache_group_id
        if isinstance(kv_cache_spec, MambaSpec):
            serialized["shapes"] = [list(shape) for shape in kv_cache_spec.shapes]
            serialized["dtype_sizes"] = [
                torch.tensor([], dtype=dtype).element_size()
                for dtype in kv_cache_spec.dtypes  # type: ignore[misc]
            ]
        return serialized

    @staticmethod
    def _get_spec_num_key_value_heads(spec: Any) -> int | None:
        for key in ("num_kv_heads", "num_key_value_heads"):
            num_key_value_heads = getattr(spec, key, None)
            if isinstance(num_key_value_heads, int):
                return num_key_value_heads
        return None

    @classmethod
    def _get_kv_transfer_spec_key(
        cls,
        spec: Any,
        total_num_kv_heads: int | None,
    ) -> tuple[str, int | None, int | None]:
        # TODO: Extend this key with KV cache layout fields (for example num_dims)
        # if a future model has layers with the same number of kv heads but incompatiible
        # cache shapes.
        return (
            type(spec).__name__,
            cls._get_spec_num_key_value_heads(spec),
            total_num_kv_heads,
        )

    def _get_spec_total_num_kv_heads(self, spec: Any, layer_idx: int) -> int | None:
        local_num_kv_heads = self._get_spec_num_key_value_heads(spec)
        if local_num_kv_heads is None or isinstance(spec, MLAAttentionSpec):
            return local_num_kv_heads

        model_config = self.vllm_config.model_config
        speculative_config = self.vllm_config.speculative_config
        if (
            layer_idx >= self.total_layers
            and speculative_config is not None
            and speculative_config.draft_model_config is not None
        ):
            model_config = speculative_config.draft_model_config
        return model_config.get_total_num_kv_heads()

    def _build_kv_group2layeridx(self) -> dict[int, tuple[dict[str, Any], list[int]]]:
        from vllm.v1.worker.utils import extract_layer_index

        kv_group2layeridx: dict[int, tuple[dict[str, Any], list[int]]] = {}
        num_attn_module = (
            2
            if self.vllm_config.model_config.hf_text_config.model_type
            == "longcat_flash"
            else 1
        )
        next_mtp_layer_idx = self.total_layers
        transfer_group_id = 0
        for kv_cache_group_id, group_spec in enumerate(
            self.kv_cache_config.kv_cache_groups
        ):
            layer_entries: list[tuple[str, int]] = []
            # For eagle3 method there is no "mtp" in layer names, and upstream model initiation assigns the layer id
            # that is sliced by Pipeline Parallel. So the eagle layer id will conflict with target model layers.
            # Here we determine whether the current layer is an eagle layer based on whether the layer id has been
            # assigned to previous layers. If the layer id has been assigned, we treat the current layer as
            # an eagle layer and assign a new layer id starting from total_layers.
            assigned_indices: set[int] = set()
            for layer_name in group_spec.layer_names:
                if "mtp" in layer_name:
                    layer_idx = next_mtp_layer_idx
                    next_mtp_layer_idx += 1
                else:
                    layer_idx = extract_layer_index(layer_name, num_attn_module)
                    if (
                        assigned_indices
                        and layer_idx < min(assigned_indices)
                        or layer_idx in assigned_indices
                    ):
                        layer_idx = next_mtp_layer_idx
                        next_mtp_layer_idx += 1
                assigned_indices.add(layer_idx)
                layer_entries.append((layer_name, layer_idx))

            spec_groups: OrderedDict[
                tuple[str, int | None, int | None],
                list[tuple[str, int, Any, int | None]],
            ] = OrderedDict()
            for layer_name, layer_idx in layer_entries:
                kv_cache_spec = group_spec.kv_cache_spec
                if isinstance(kv_cache_spec, UniformTypeKVCacheSpecs):
                    kv_cache_spec = kv_cache_spec.kv_cache_specs[layer_name]
                total_num_kv_heads = self._get_spec_total_num_kv_heads(
                    kv_cache_spec, layer_idx
                )
                spec_key = self._get_kv_transfer_spec_key(
                    kv_cache_spec, total_num_kv_heads
                )
                spec_groups.setdefault(spec_key, []).append(
                    (layer_name, layer_idx, kv_cache_spec, total_num_kv_heads)
                )

            if len(spec_groups) > 1:
                logger.info(
                    "Split KV cache manager group %d into %d HIXL transfer groups by KV spec: %s",
                    kv_cache_group_id,
                    len(spec_groups),
                    list(spec_groups),
                )

            for entries in spec_groups.values():
                layer_names = [layer_name for layer_name, _, _, _ in entries]
                layer_indices = [layer_idx for _, layer_idx, _, _ in entries]
                kv_cache_spec = entries[0][2]
                total_num_kv_heads = entries[0][3]
                kv_group2layeridx[transfer_group_id] = (
                    self._serialize_kv_group_spec(
                        group_spec,
                        layer_names=layer_names,
                        kv_cache_spec=kv_cache_spec,
                        kv_cache_group_id=kv_cache_group_id,
                        total_num_kv_heads=total_num_kv_heads,
                    ),
                    layer_indices,
                )
                transfer_group_id += 1
        return kv_group2layeridx

    def _has_mamba_group(self) -> bool:
        return any(
            group_spec["kv_cache_spec_type"] == "MambaSpec"
            for group_spec, _ in self.kv_group2layeridx.values()
        )

    def _requires_group_aware_attention_transfer(self) -> bool:
        total_num_kv_heads = {
            self._get_attention_group_num_key_value_heads(group_spec)
            for group_spec, layer_indices in self.kv_group2layeridx.values()
            if layer_indices and group_spec["kv_cache_spec_type"] != "MambaSpec"
        }
        return len(total_num_kv_heads) > 1

    @staticmethod
    def _as_kv_cache_tuple(kv_cache_tuple: Any) -> list[torch.Tensor]:
        if isinstance(kv_cache_tuple, (list, tuple)):
            return list(kv_cache_tuple)
        return [kv_cache_tuple]

    def _get_layer_spec(self, layer_name: str) -> Any:
        layer_spec = self._layer_specs[layer_name]
        if isinstance(layer_spec, UniformTypeKVCacheSpecs):
            layer_spec = layer_spec.kv_cache_specs[layer_name]
        return layer_spec

    def _get_mamba_conv_padding(self, layer_spec: Any) -> int:
        if not isinstance(layer_spec, MambaSpec):
            return 0
        conv_nbytes = torch.tensor([], dtype=layer_spec.dtypes[0]).element_size()  # type: ignore[misc]
        conv_shape = torch.Size(layer_spec.shapes[0])
        return self.num_blocks * conv_shape.numel() * conv_nbytes

    def _get_registered_kv_tensor_buffers(
        self, kv_caches: dict[str, torch.Tensor]
    ) -> tuple[list[int], list[int]]:
        ptrs: list[int] = []
        lengths: list[int] = []

        conv_padding = 0
        for kv_cache_tensor in self.kv_cache_config.kv_cache_tensors:
            shared_addrs: list[int] = []
            has_mtp = False
            for layer_name in kv_cache_tensor.shared_by:
                has_mtp = has_mtp or "mtp" in layer_name
                layer_spec = self._get_layer_spec(layer_name)
                conv_padding = max(
                    conv_padding, self._get_mamba_conv_padding(layer_spec)
                )
                for single_kv_cache in self._as_kv_cache_tuple(kv_caches[layer_name]):
                    shared_addrs.append(single_kv_cache.data_ptr())

            if not shared_addrs:
                continue
            base_addr = min(shared_addrs)
            if has_mtp:
                base_addr -= conv_padding
            if base_addr % (2 * 1024 * 1024) != 0:
                raise ValueError(f"Tensor start addr {base_addr} is not aligned to 2M.")
            ptrs.append(base_addr)
            lengths.append(kv_cache_tensor.size)

        return ptrs, lengths

    def _get_registered_kv_tensor_buffers_hybrid(
        self, kv_caches: dict[str, torch.Tensor]
    ) -> tuple[list[int], list[int]]:
        ptrs: list[int] = []
        lengths: list[int] = []

        for kv_cache_tensor in self.kv_cache_config.kv_cache_tensors:
            shared_addrs: list[int] = []
            for layer_name in kv_cache_tensor.shared_by:
                for single_kv_cache in self._as_kv_cache_tuple(kv_caches[layer_name]):
                    shared_addrs.append(single_kv_cache.data_ptr())

            if not shared_addrs:
                continue
            base_addr = min(shared_addrs)
            if base_addr % (2 * 1024 * 1024) != 0:
                raise ValueError(f"Tensor start addr {base_addr} is not aligned to 2M.")
            ptrs.append(base_addr)
            lengths.append(kv_cache_tensor.size)

        return ptrs, lengths

    def _get_registered_layer_buffers(
        self, kv_caches: dict[str, torch.Tensor]
    ) -> tuple[list[int], list[int]]:
        ptrs: list[int] = []
        lengths: list[int] = []

        for kv_cache_tuple in kv_caches.values():
            for single_kv_cache in self._as_kv_cache_tuple(kv_cache_tuple):
                ptrs.append(single_kv_cache.data_ptr())
                lengths.append(
                    single_kv_cache.element_size() * math.prod(single_kv_cache.shape)
                )

        return ptrs, lengths

    def register_kv_caches(self, kv_caches: dict[str, torch.Tensor]):
        """Register the KV Cache data."""
        self.use_mla = self.vllm_config.model_config.is_deepseek_mla
        self.use_sparse = hasattr(
            self.vllm_config.model_config.hf_text_config, "index_topk"
        )
        self.enable_sfa_dcp_replicated_indexer = enable_sfa_dcp_replicated_indexer(
            self.vllm_config
        )

        self.num_blocks = self.kv_cache_config.num_blocks
        logger.info("num_blocks: %s", self.num_blocks)
        self.kv_caches = kv_caches
        # Maps each KV cache group to its serialized group spec and physical
        # layer indices: {group_id: (group_spec, [layer_idx0, layer_idx1, ...])}.
        self.kv_group2layeridx = self._build_kv_group2layeridx()
        self._is_hma_required = (
            self._is_hma_required or self._requires_group_aware_attention_transfer()
        )
        has_mamba_group = self._has_mamba_group()
        layer_name_to_idx = {
            layer_name: layer_idx
            for _, (group_spec, layer_indices) in self.kv_group2layeridx.items()
            for layer_name, layer_idx in zip(group_spec["layer_names"], layer_indices)
        }
        metadata_layers = max(layer_name_to_idx.values(), default=-1) + 1
        # Per-layer registered KV cache base addresses:
        # [layer_idx][cache_idx] -> data_ptr of one cache tensor, e.g. K/V.
        self.kv_caches_base_addr: list[list[int]] = [[] for _ in range(metadata_layers)]
        # Per-layer block scaling between logical KV blocks and tensor blocks:
        # [layer_idx][cache_idx] -> cache tensor num_blocks / logical num_blocks.
        self.block_size_scale: list[list[int]] = [[] for _ in range(metadata_layers)]
        # Per-layer byte length of one tensor block:
        # [layer_idx][cache_idx] -> element_size * prod(block_shape).
        self.block_len_per_addr: list[list[int]] = [[] for _ in range(metadata_layers)]
        # Per-layer full tensor shape for each registered KV cache address:
        # [layer_idx][cache_idx] -> cache tensor shape, including num_blocks.
        self.block_shape_per_addr: list[list[int]] = [
            [] for _ in range(metadata_layers)
        ]
        # Per-layer byte stride between consecutive tensor blocks:
        # [layer_idx][cache_idx] -> stride(0) * element_size.
        self.block_stride_per_addr: list[list[int]] = [
            [] for _ in range(metadata_layers)
        ]

        # TODO: For DSV4 use_compress, metadata/transfer can be optimized by
        # aggregating layer views that share the same raw KVCacheTensor.
        for layer_name, kv_cache_tuple in kv_caches.items():
            layer_idx = layer_name_to_idx[layer_name]
            for single_kv_cache in self._as_kv_cache_tuple(kv_cache_tuple):
                tensor_num_blocks = single_kv_cache.shape[0]
                block_size_scale = tensor_num_blocks // self.num_blocks
                block_shape = single_kv_cache.shape[1:]
                self.block_len_per_addr[layer_idx].append(
                    single_kv_cache.element_size() * math.prod(block_shape)
                )
                self.block_stride_per_addr[layer_idx].append(
                    single_kv_cache.stride(0) * single_kv_cache.element_size()
                )
                self.block_shape_per_addr[layer_idx].append(single_kv_cache.shape)
                self.block_size_scale[layer_idx].append(block_size_scale)
                self.kv_caches_base_addr[layer_idx].append(single_kv_cache.data_ptr())

        # Registration granularity is unchanged from the original connector: the allocator
        # 2M-aligns each raw KV tensor for disaggregation, and views inside it
        # (conv at kv_padding, ssm after k) start at unaligned offsets, so
        # registering whole storage keeps the aligned base and leaves room to
        # aggregate layer views later. The logical ledger above stays per view.
        if has_mamba_group:
            ptrs, lengths = self._get_registered_kv_tensor_buffers(kv_caches)
            register_regions = RegisterRegions(ptrs=ptrs, lengths=lengths)
        elif self.use_hybrid:
            ptrs, lengths = self._get_registered_kv_tensor_buffers_hybrid(kv_caches)
            register_regions = RegisterRegions(ptrs=ptrs, lengths=lengths)
        else:
            # For normal attention / sparse-c8 KV cache, keep metadata at the
            # logical tensor level but merge registration ranges by underlying
            # storage.
            register_regions = collect_storage_merged_register_regions(kv_caches)

        n_registered = self.engine.register_physical_regions(
            list(zip(register_regions.ptrs, register_regions.lengths, strict=True))
        )

        logger.debug(
            "HIXLConnector register kv caches metadata: kv_group2layeridx=%s, kv_caches_base_addr=%s, "
            "block_len_per_addr=%s, block_stride_per_addr=%s, block_shape_per_addr=%s, "
            "block_size_scale=%s, ptrs=%s, lengths=%s, n_registered=%s",
            self.kv_group2layeridx,
            self.kv_caches_base_addr,
            self.block_len_per_addr,
            self.block_stride_per_addr,
            self.block_shape_per_addr,
            self.block_size_scale,
            register_regions.ptrs,
            register_regions.lengths,
            n_registered,
        )
        # After KV Caches registered, start the sending or receiving thread.
        metadata = HIXLAgentMetadata(
            engine_id=self.engine_id,
            listen_port=self.listen_port,
            kv_group2layeridx=self.kv_group2layeridx,
            block_size=self.block_size,
            kv_caches_base_addr=self.kv_caches_base_addr,
            block_size_scale=self.block_size_scale,
            num_blocks=self.num_blocks,
            block_lens=self.block_len_per_addr,
            block_strides=self.block_stride_per_addr,
            local_ip=get_ip(),
            handshake_port=self.handshake_port,
        )
        self.xfer_handshake_metadata = metadata

        ready_event = threading.Event()
        if self.kv_role == "kv_producer":
            self.kv_send_thread = KVCacheSendingThread(
                self.vllm_config,
                self.tp_rank,
                self._prefill_tp_size,
                self.engine_id,
                self.side_channel_host,
                self.side_channel_port,
                metadata,
                ready_event,
                self.kv_caches,
                self.pcp_rank,
            )
            self.kv_send_thread.start()
        else:
            self.kv_recv_thread = KVCacheRecvingThread(
                self.tp_rank,
                self.tp_size,
                self._prefill_pp_size,
                self.engine,
                self.engine_id,
                self.handshake_port,
                self.side_channel_port,
                self.kv_caches_base_addr,
                self.block_len_per_addr,
                self.block_stride_per_addr,
                self._is_hma_required,
                ready_event,
                self.vllm_config,
                self.kv_caches,
                self._prefill_pp_layer_partition,
                self.kv_group2layeridx,
                self.block_size_scale,
            )
            self.kv_recv_thread.start()
        start_wait_time = time.time()
        thread = (
            self.kv_send_thread
            if self.kv_role == "kv_producer"
            else self.kv_recv_thread
        )
        assert thread is not None
        while not ready_event.is_set():
            if not thread.is_alive():
                raise RuntimeError("KV Cache sending/receiving thread failed to start.")
            if time.time() - start_wait_time > 5 * 60:
                raise RuntimeError("Timeout waiting for KV Cache thread to be ready.")
            time.sleep(3)

    def shutdown(self):
        # Close the transfer lifecycle in order: stop dispatching new pulls,
        # drain in-flight pulls (the executor shutdown waits for them so a
        # late DMA never races the teardown), disconnect peers, then
        # deregister the KV segments, finalize the engine and close the ZMQ
        # pool.
        if self.kv_recv_thread is not None:
            self.kv_recv_thread.shutdown()
        if self.kv_send_thread is not None:
            self.kv_send_thread.shutdown()
        engine = getattr(self, "engine", None)
        if engine is None:
            return
        try:
            engine.disconnect_all()
        except Exception:  # noqa: BLE001
            logger.warning(
                "HIXLConnector disconnect-all failed during shutdown.",
                exc_info=True,
            )
        try:
            engine.finalize()
        except Exception:  # noqa: BLE001
            logger.warning(
                "HIXLConnector finalize failed during shutdown.", exc_info=True
            )

    def get_finished(self) -> tuple[set[str], set[str]]:
        done_sending = (
            self.kv_send_thread.get_and_clear_finished_requests(  # type: ignore[union-attr]
            )
            if self.kv_role == "kv_producer"
            else set()
        )
        done_recving = (
            self.kv_recv_thread.get_and_clear_finished_requests(  # type: ignore[union-attr]
            )
            if self.kv_role == "kv_consumer"
            else set()
        )
        if self.tp_rank == 0:
            if logger.isEnabledFor(logging.DEBUG):
                logger.debug(
                    "Number of completed KV cache send requests: %d, receive requests: %d",
                    len(done_sending),
                    len(done_recving),
                )
        return done_sending, done_recving

    def get_block_ids_with_load_errors(self) -> set[int]:
        if self.kv_role == "kv_consumer" and self.kv_recv_thread is not None:
            return self.kv_recv_thread.get_and_clear_invalid_block_ids()
        return set()

    @staticmethod
    def _expand_block_ids(block_ids, scale):
        # Expand each logical block into its `scale` contiguous kernel blocks:
        # logical block b -> [b*scale, b*scale+1, ..., b*scale+scale-1].
        return [bid * scale + offset for bid in block_ids for offset in range(scale)]

    def _local_kernel_ids_for_shard(
        self,
        shard_first_p_block,
        num_blocks_to_pull,
        shard_cp_rank,
        num_prefix_p_blocks,
        rank_first_d_block,
        block_size_ratio,
        local_cp_size,
        remote_cp_size,
        remote_block_size,
        kernel_size,
        local_block_ids,
    ):
        """Map this shard's pulled P-blocks straight to D-side kernel block ids.

        The shard (CP rank ``shard_cp_rank``) pulls ``num_blocks_to_pull`` P-blocks
        starting at this rank's local index ``shard_first_p_block``. The destination
        kernel position is derived directly from the CP rank and the block index,
        replacing the previous two-step pipeline (local_chunk_token_starts + per-token
        expansion). TP rank does not affect the block id (it only selects ports / the
        head-dim offset at the transfer stage).
        """
        # Number of kernel blocks contained in one D-block (Bd/kernel) and one P-block (Bp/kernel).
        kernels_per_d_block = self.block_size // kernel_size
        kernels_per_p_block = remote_block_size // kernel_size
        # Tokens addressable by this rank's D-blocks; a kernel beyond this has no destination.
        local_token_limit = len(local_block_ids) * self.block_size
        kernel_block_ids: list[int] = []
        for block_idx in range(num_blocks_to_pull):
            # P-blocks are round-robin interleaved across the remote CP ranks, so this rank's
            # block_idx-th pulled block maps to global prompt block (in P-units):
            #   global_p_block = (shard_first_p_block + block_idx) * Rcp + shard_cp_rank
            global_p_block = (
                shard_first_p_block + block_idx
            ) * remote_cp_size + shard_cp_rank
            if remote_block_size > self.block_size:
                # Bp > Bd (only supported when D-side has no CP): one P-block spans multiple
                # D-blocks, so walk it kernel by kernel via the absolute token offset within
                # the external (post-prefix) zone: p_block_token_start = (p - P0) * Bp.
                p_block_token_start = (
                    global_p_block - num_prefix_p_blocks
                ) * remote_block_size
                for kernel_idx in range(kernels_per_p_block):
                    token_offset = p_block_token_start + kernel_idx * kernel_size
                    if token_offset >= local_token_limit:
                        # P-side tail block is partial; its trailing kernels have no D token.
                        break
                    # Locate the D-block holding this token, then the kernel slot inside it.
                    d_block = local_block_ids[token_offset // self.block_size]
                    kernel_in_d_block = (token_offset % self.block_size) // kernel_size
                    kernel_block_ids.append(
                        d_block * kernels_per_d_block + kernel_in_d_block
                    )
            else:
                # Bd >= Bp: the P-block falls entirely inside one D-block.
                # Global D-block d = p // r (r = Bd/Bp); its index within this rank's local
                # list is (d - rank_first_d_block) // Lcp. The P-block occupies a contiguous
                # run of kernels_per_p_block kernels starting at intra-block kernel offset
                # ((p % r) * Bp) / kernel.
                d_block_local_idx = (
                    global_p_block // block_size_ratio - rank_first_d_block
                ) // local_cp_size
                if d_block_local_idx >= len(local_block_ids):
                    # Pairs with the remote-side truncation when the P-side tail block is partial.
                    continue
                d_block = local_block_ids[d_block_local_idx]
                first_kernel_in_d_block = (
                    (global_p_block % block_size_ratio) * remote_block_size
                ) // kernel_size
                for kernel_idx in range(kernels_per_p_block):
                    kernel_block_ids.append(
                        d_block * kernels_per_d_block
                        + first_kernel_in_d_block
                        + kernel_idx
                    )
        return kernel_block_ids

    @staticmethod
    def _group_compress_ratio(group_spec):
        # Tokens per KV slot for this group (>1 for compressed specs); defaults to 1.
        compress_ratio = 1
        kv_cache_spec = group_spec.get("kv_cache_spec")
        if isinstance(kv_cache_spec, dict):
            for spec in kv_cache_spec.values():
                if isinstance(spec, dict) and isinstance(
                    spec.get("compress_ratio"), int
                ):
                    compress_ratio = max(1, spec["compress_ratio"])
                    break
        return compress_ratio

    @staticmethod
    def _get_kv_cache_group_id(group_idx: int, group_spec: dict[str, Any]) -> int:
        return group_spec.get("kv_cache_group_id", group_idx)

    def _get_kernel_block_ids(self, layer_indices, meta, group_idx, group_spec):
        """No-CP per-group block ids at kernel granularity: (local, remote).

        Mamba state is not block-sharded, so its logical ids pass through unchanged.
        Attention expands both sides to kernel blocks, skips the prefix-cached remote
        kernels (already on D, located via num_computed_tokens), clips unused tail
        kernels that hybrid page-alignment expanded from a partial scheduler block,
        and trims both lists to the shorter one so remote/local stay aligned.
        """
        kv_cache_group_id = self._get_kv_cache_group_id(group_idx, group_spec)
        if group_spec["kv_cache_spec_type"] == "MambaSpec":
            return list(meta.local_block_ids[kv_cache_group_id]), list(
                meta.remote_block_ids[kv_cache_group_id]
            )

        remote_block_size = meta.remote_block_size or self.block_size

        # kernel_size is the shared (P==D) granularity; remote_scale is derived from it.
        local_scale = self.block_size_scale[layer_indices[0]][0]
        kernel_size = self.block_size // local_scale
        if remote_block_size % kernel_size != 0:
            raise ValueError(
                f"remote_block_size({remote_block_size}) is not divisible by "
                f"kernel_size({kernel_size})."
            )

        remote_scale = remote_block_size // kernel_size
        kernel_local = self._expand_block_ids(
            list(meta.local_block_ids[kv_cache_group_id]), local_scale
        )
        kernel_remote = self._expand_block_ids(
            list(meta.remote_block_ids[kv_cache_group_id]), remote_scale
        )
        # Skip prefix-cached remote kernels (D-side already holds them). The token size of one
        # remote kernel is kernel_size * compress_ratio, so the number to skip is
        # num_computed_tokens // (kernel_size * compress_ratio).
        remote_kernel_token_size = kernel_size * self._group_compress_ratio(group_spec)
        remote_start_idx = meta.num_computed_tokens // remote_kernel_token_size
        kernel_remote = kernel_remote[remote_start_idx:]
        num_kernel_blocks = min(len(kernel_remote), len(kernel_local))
        # One scheduler attention block may expand to many kernel pages so its
        # page size can align with mamba. Only the prefix of those pages holds
        # computed tokens; drop the unwritten tail (e.g. 1 token -> 1/12 pages).
        num_external_tokens = getattr(meta, "num_external_tokens", 0)
        if num_external_tokens > 0:
            tokens_to_cover = meta.num_computed_tokens + num_external_tokens
            needed_kernels = (
                cdiv(tokens_to_cover, remote_kernel_token_size) - remote_start_idx
            )
            if needed_kernels > 0:
                num_kernel_blocks = min(num_kernel_blocks, needed_kernels)
        return kernel_local[:num_kernel_blocks], kernel_remote[:num_kernel_blocks]

    def _get_group_kernel_params(self, remote_block_size):
        # Per attention group kernel-expansion params: (local_scale, remote_scale, kernel_size).
        # The kernel size is shared by both sides, so remote_scale is derived locally from it
        # (no remote handshake scale needed). Mamba groups are not block-sharded and skipped.
        group_kernel_params: dict[int, tuple[int, int, int]] = {}
        for group_idx, (group_spec, layer_indices) in self.kv_group2layeridx.items():
            if group_spec["kv_cache_spec_type"] == "MambaSpec":
                continue
            local_scale = self.block_size_scale[layer_indices[0]][0]
            kernel_size = self.block_size // local_scale
            if remote_block_size % kernel_size != 0:
                raise ValueError(
                    f"remote_block_size({remote_block_size}) is not divisible by "
                    f"kernel_size({kernel_size})."
                )
            remote_scale = remote_block_size // kernel_size
            group_kernel_params[group_idx] = (local_scale, remote_scale, kernel_size)
        return group_kernel_params

    def _get_local_remote_cp_params(self, meta: ReqMeta):
        """Resolve CP geometry: (remote_block_size, local_cp_rank, local_cp_size,
        remote_cp_size, r_blk), where r_blk = Bd/Bp (>=1) is the D/P block-size ratio.
        Also validates that P/D block sizes are compatible under D-side CP.
        """
        remote_block_size = meta.remote_block_size or self.block_size
        local_cp_rank = self.dcp_rank + self.pcp_rank * self.dcp_size
        local_cp_size = self.dcp_size * self.pcp_size
        # Remote-derived sizes drive the division and modulo below; a bogus
        # value must fail fast instead of slicing a wrong layout.
        if meta.remote_pcp_size < 1 or meta.remote_dcp_size < 1:
            raise ValueError(
                f"Remote CP sizes must be >= 1, got remote_pcp_size="
                f"{meta.remote_pcp_size}, remote_dcp_size={meta.remote_dcp_size}."
            )
        remote_cp_size = meta.remote_pcp_size * meta.remote_dcp_size

        if remote_block_size != self.block_size:
            if (
                self.block_size % remote_block_size != 0
                and remote_block_size % self.block_size != 0
            ):
                raise ValueError(
                    f"Block sizes of P ({remote_block_size}) and D ({self.block_size}) "
                    "must be divisible by each other."
                )
            if local_cp_size > 1:
                if self.block_size % remote_block_size != 0:
                    raise ValueError(
                        f"D node DCP does not support P node block_size"
                        f"({remote_block_size}) > D block_size({self.block_size})."
                    )
                # Ensure that the blocks of each P cp rank belong to the same D rank.
                if (remote_cp_size // local_cp_size) % (
                    self.block_size // remote_block_size
                ) != 0:
                    raise ValueError(
                        f"remote_cp_size({remote_cp_size}) must be an integer multiple "
                        f"of r({self.block_size // remote_block_size}) * "
                        f"local_cp_size({local_cp_size})."
                    )

        r_blk = (
            self.block_size // remote_block_size
            if self.block_size > remote_block_size
            else 1
        )
        return remote_block_size, local_cp_rank, local_cp_size, remote_cp_size, r_blk

    def _build_cp_port_mapping(
        self,
        meta: ReqMeta,
        prefill_tp_size: int,
        r_blk: int,
    ) -> dict[int, list[list[int]]]:
        """Map each D-side side-channel port to its P-side pull ports.

        P and D CP groups are paired per head group; each D CP group
        round-robins over the P replicas, and the ``r_blk`` block-mapping
        rule selects which P ranks feed which D rank (``p_idx % Lcp``
        when Bd == Bp, ``(p_idx // r) % Lcp`` when Bd = r * Bp).
        """
        p_node_cp_group_meta = _cp_group_meta(
            self.num_key_value_heads,
            self.use_mla,
            self.use_sparse,
            prefill_tp_size,
            meta.remote_pcp_size,
            meta.remote_dcp_size,
            meta.remote_port,
        )
        d_node_cp_group_meta = _cp_group_meta(
            self.num_key_value_heads,
            self.use_mla,
            self.use_sparse,
            self.tp_size,
            self.pcp_size,
            self.dcp_size,
            self.side_channel_port,
        )
        local_remote_block_port_mappings: dict[int, list[list[int]]] = {}
        for d_node_head_key in d_node_cp_group_meta:
            for p_node_head_key in p_node_cp_group_meta:
                if not set(p_node_head_key).issubset(set(d_node_head_key)):
                    continue
                d_node_head_group = d_node_cp_group_meta[d_node_head_key]
                p_node_head_group = p_node_cp_group_meta[p_node_head_key]
                for d_cp_group in d_node_head_group["cp_groups"]:
                    select_cp_groups_id = p_node_head_group["select_cp_groups_id"]
                    p_cp_groups = p_node_head_group["cp_groups"]
                    p_cp_group = p_cp_groups[select_cp_groups_id]
                    p_node_head_group["select_cp_groups_id"] = (
                        select_cp_groups_id + 1
                        if select_cp_groups_id + 1 < len(p_cp_groups)
                        else 0
                    )
                    for d_idx, d_port in enumerate(d_cp_group):
                        if d_port not in local_remote_block_port_mappings:
                            local_remote_block_port_mappings[d_port] = []
                        p_port_remote_list = []
                        for p_idx, p_port in enumerate(p_cp_group):
                            # When Bd == Bp, r_blk = 1, which degenerates to the original `p_idx % Lcp` rule.
                            # When Bd = r * Bp, all blocks of P CP rank q are mapped to D rank `(q // r) % Lcp`.
                            if (p_idx // r_blk) % len(d_cp_group) == d_idx:
                                p_port_remote_list.append(p_port)
                        local_remote_block_port_mappings[d_port].append(
                            p_port_remote_list
                        )

        logger.info(
            "CP block port mapping built: prefill_tp_size=%d, decode_tp_size=%d, "
            "p_node_cp_group_entries=%d, d_node_cp_group_entries=%d, "
            "d_port_mapping_entries=%d.",
            prefill_tp_size,
            self.tp_size,
            len(p_node_cp_group_meta),
            len(d_node_cp_group_meta),
            len(local_remote_block_port_mappings),
        )
        # The full tables expose every node's host/port topology; keep
        # them at DEBUG so INFO stays readable on large clusters.
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug(
                "CP block port mapping details: p_node_cp_group_meta=%s, "
                "d_node_cp_group_meta=%s, local_remote_block_port_mappings=%s.",
                p_node_cp_group_meta,
                d_node_cp_group_meta,
                local_remote_block_port_mappings,
            )

        return local_remote_block_port_mappings

    def _build_remote_port_send_num(
        self,
        meta: ReqMeta,
        prefill_tp_size: int,
        local_remote_block_port_mappings: dict[int, list[list[int]]],
    ) -> dict[int, RemotePortInfo]:
        """Count, per remote P port, how many pulls this D rank issues."""
        remote_port_send_num: dict[int, RemotePortInfo] = {}
        remote_ports: set[int] = set(
            range(
                meta.remote_port,
                meta.remote_port + prefill_tp_size * meta.remote_pcp_size,
            )
        )
        kv_port = self.vllm_config.kv_transfer_config.kv_port
        for key, remote_host_info in meta.remote_multi_nodes_meta_mapping.items():
            remote_ports.add(
                int(remote_host_info.get("handshake_port", kv_port + int(key)))
            )
        for remote_port_head_list in local_remote_block_port_mappings.values():
            for remote_port_list in remote_port_head_list:
                for remote_port in remote_port_list:
                    remote_ports.add(remote_port)

        for remote_port in remote_ports:
            remote_host, _ = self._get_remote_host_info_by_port(
                meta.remote_port,
                remote_port,
                meta.remote_host,
                meta.remote_engine_id,
                meta.remote_multi_nodes_meta_mapping,
            )
            remote_port_send_num[remote_port] = {"num": 0, "host": remote_host}

        for remote_port_head_list in local_remote_block_port_mappings.values():
            for remote_port_list in remote_port_head_list:
                for remote_port in remote_port_list:
                    remote_port_send_num[remote_port]["num"] += 1
        return remote_port_send_num

    def _set_hma_shared_port(
        self,
        prefill_tp_size: int,
        meta: ReqMeta,
        remote_handshake_port_list: list[list[int]],
        req_id: str,
    ) -> list[list[int]]:
        """Rewrite remote attention ports for HMA load balancing and append Mamba ports.

        Only applies to HMA (hybrid) non-MLA/non-sparse models. It does two things:

        1. Attention replica balancing. A remote attention port offset decomposes as
           ``kv_head_group_offset + dcp_repeat_offset + dcp_rank``. Within the same head
           group, only the TP replicas that share the same ``dcp_rank`` but differ in
           ``dcp_repeat`` hold the exact same attention KV shard. So substitution is
           restricted to the dcp_repeat (replica) dimension - the head group and dcp_rank
           parts are preserved - otherwise different DCP shards would fetch duplicated KV.
           The replica is picked from the request's random rank choice to spread load.
        2. Mamba port append. The Mamba state lives on a different set of P ranks than the
           attention shards, so the matching Mamba ports are appended to the final shard
           (which carries the Mamba transfer); duplicates are skipped.
        """
        if self._is_hma_required and not (self.use_mla or self.use_sparse):
            remote_dcp = max(meta.remote_dcp_size, 1)
            group_span = prefill_tp_size // len(
                _kv_head_groups(
                    self.num_key_value_heads,
                    prefill_tp_size,
                    self.use_mla,
                    self.use_sparse,
                )
            )
            n_replica = max(group_span // remote_dcp, 1)
            chosen_tp_list = self._get_remote_rank(req_id, prefill_tp_size)
            if n_replica > 1:
                for shard_ports in remote_handshake_port_list:
                    for i in range(len(shard_ports)):
                        # Decompose the port offset into pcp segment + head-group offset +
                        # dcp part, keeping all of them and only swapping the replica.
                        tp_off = (shard_ports[i] - meta.remote_port) % prefill_tp_size
                        pcp_seg = (shard_ports[i] - meta.remote_port) - tp_off
                        group_off = tp_off // group_span * group_span
                        dcp_part = (tp_off - group_off) % remote_dcp
                        # Determine replica ID using random choice of current request to maintain load balancing.
                        replica = (
                            chosen_tp_list[i % len(chosen_tp_list)] // remote_dcp
                        ) % n_replica
                        shard_ports[i] = (
                            meta.remote_port
                            + pcp_seg
                            + group_off
                            + replica * remote_dcp
                            + dcp_part
                        )
            # Append this D rank's matching Mamba ports to the final shard (the one that
            # carries the Mamba state); k = prefill_tp / decode_tp ports per D rank.
            k = prefill_tp_size // self.tp_size
            final_ports = remote_handshake_port_list[-1]
            pcp_seg = (
                (final_ports[0] - meta.remote_port) // prefill_tp_size * prefill_tp_size
            )
            for j in range(k):
                p = meta.remote_port + pcp_seg + self.tp_rank * k + j
                if p not in final_ports:
                    final_ports.append(p)
        return remote_handshake_port_list

    def _check_cp_layout(self, meta: ReqMeta, prefill_tp_size: int) -> None:
        """Validate remote/local CP sizes and per-rank KV head divisibility."""
        remote_cp = meta.remote_pcp_size * meta.remote_dcp_size
        local_cp = self.pcp_size * self.dcp_size
        if remote_cp % local_cp != 0:
            raise ValueError(
                f"remote CP size({remote_cp}) must be an integer multiple of "
                f"local CP size({local_cp})."
            )
        if not (self.use_mla or self.use_sparse):
            p_node_heads_per_rank = math.ceil(
                self.num_key_value_heads / prefill_tp_size
            )
            d_node_heads_per_rank = math.ceil(self.num_key_value_heads / self.tp_size)
            if d_node_heads_per_rank % p_node_heads_per_rank != 0:
                raise ValueError(
                    f"D node kv heads per rank({d_node_heads_per_rank}) must be "
                    f"divisible by P node kv heads per rank({p_node_heads_per_rank})."
                )

    def _get_kv_split_metadata(
        self,
        req_id: str,
        meta: ReqMeta,
    ) -> KVSplitResult:
        """Build per-transfer port and block-id metadata for remote KV reads.

        Args:
            req_id: Remote request id used as the stable hash key when choosing
                prefill TP ranks.
            meta: Request-level transfer metadata from the scheduler. It
                contains remote/local block ids, remote P-side port base,
                remote P-side PCP/DCP/PTP sizes, and prompt/prefix-cache
                token counts.

        Returns:
            A tuple of three aligned lists. Index ``i`` describes one transfer
            shard for this local D-side rank:
            * remote_handshake_port_list[i]: remote P worker handshake ports
              to pull from. The inner list length is the number of TP pulls
              needed for that shard.
            * local_block_ids_list[i]: local kernel block ids, grouped by KV cache
              group, where received blocks are written.
            * remote_block_ids_list[i]: remote kernel block ids, grouped by KV cache
              group, where blocks are read from.

        In PCP/DCP scenarios, prompt blocks can be split across multiple remote
        P workers. This method also accounts for unequal P/D prefix-cache hits
        by reducing the number of remote blocks that still need to be pulled.
        """
        prefill_tp_size: int = (
            meta.remote_ptp_size
            if meta.remote_ptp_size is not None
            else self._prefill_tp_size
        )

        if (
            meta.remote_pcp_size * meta.remote_dcp_size * self.pcp_size * self.dcp_size
            == 1
        ):
            if self._is_hma_required:
                chosen_rank_list, _ = self._get_hybrid_remote_rank_group_pulls(
                    req_id, prefill_tp_size
                )
            else:
                chosen_rank_list = self._get_remote_rank(req_id, prefill_tp_size)

            remote_handshake_port_list = [
                [x + meta.remote_port for x in chosen_rank_list]
            ]
            # No CP: expand logical blocks into kernel blocks here so the transfer
            # stage consumes kernel-level ids directly (chunk_starts no longer needed).
            local_block_ids: list[list[int]] = [[] for _ in meta.local_block_ids]
            remote_block_ids: list[list[int]] = [[] for _ in meta.remote_block_ids]
            for group_idx, (
                group_spec,
                layer_indices,
            ) in self.kv_group2layeridx.items():
                local_kernel_block_ids, remote_kernel_block_ids = (
                    self._get_kernel_block_ids(
                        layer_indices, meta, group_idx, group_spec
                    )
                )
                kv_cache_group_id = self._get_kv_cache_group_id(group_idx, group_spec)
                local_block_ids[kv_cache_group_id] = local_kernel_block_ids
                remote_block_ids[kv_cache_group_id] = remote_kernel_block_ids
            local_block_ids_list = [
                tuple(local_block_ids) for _ in remote_handshake_port_list
            ]
            remote_block_ids_list = [
                tuple(remote_block_ids) for _ in remote_handshake_port_list
            ]
            return KVSplitResult(
                remote_handshake_port_list,
                local_block_ids_list,
                remote_block_ids_list,
            )

        remote_block_size, local_cp_rank, local_cp_size, remote_cp_size, r_blk = (
            self._get_local_remote_cp_params(meta)
        )

        # Per attention group kernel-expansion params. remote_scale is derived locally from
        # the shared kernel size, so no remote handshake scale is needed here.
        group_kernel_params = self._get_group_kernel_params(remote_block_size)

        if meta.remote_engine_id not in self.local_remote_block_port_mapping:
            self.local_remote_block_port_mapping[meta.remote_engine_id] = None

        if self.local_remote_block_port_mapping[meta.remote_engine_id] is None:
            local_remote_block_port_mappings = self._build_cp_port_mapping(
                meta, prefill_tp_size, r_blk
            )
            self.local_remote_block_port_mapping[meta.remote_engine_id] = (
                local_remote_block_port_mappings[self.handshake_port]
            )
            self.remote_port_send_num[meta.remote_engine_id] = (
                self._build_remote_port_send_num(
                    meta, prefill_tp_size, local_remote_block_port_mappings
                )
            )

        local_remote_block_port_mapping = copy.deepcopy(
            self.local_remote_block_port_mapping[meta.remote_engine_id]
        )

        num_external_blocks = math.ceil(meta.num_external_tokens / self.block_size)
        num_external_blocks_p = math.ceil(meta.num_external_tokens / remote_block_size)

        kv_group_items = list(self.kv_group2layeridx.items())
        sequence_group_idx = next(
            (
                group_spec.get("kv_cache_group_id", group_idx)
                for group_idx, (group_spec, _) in kv_group_items
                if group_spec["kv_cache_spec_type"] != "MambaSpec"
            ),
            0,
        )
        if math.ceil(num_external_blocks / (self.pcp_size * self.dcp_size)) != len(
            meta.local_block_ids[sequence_group_idx]
        ):
            raise ValueError(
                f"num_external_blocks({num_external_blocks}) divided by "
                f"cp_size({self.pcp_size * self.dcp_size}) does not match "
                f"local_block_ids length({len(meta.local_block_ids[sequence_group_idx])})."
            )
        if meta.num_prompt_blocks < num_external_blocks_p:
            raise ValueError(
                f"meta.num_prompt_blocks({meta.num_prompt_blocks}) is smaller than "
                f"num_external_blocks({num_external_blocks_p})."
            )

        remote_block_nums_all = [
            meta.num_prompt_blocks // remote_cp_size
        ] * remote_cp_size
        num_remain_blocks = meta.num_prompt_blocks % remote_cp_size
        for i in range(num_remain_blocks):
            remote_block_nums_all[i] += 1
        last_block_location = (num_remain_blocks + remote_cp_size - 1) % remote_cp_size

        # Considering prefix cache, the remote_block_nums_all should be revised
        num_prefix_cached_blocks = meta.num_prompt_blocks - num_external_blocks_p
        remote_block_nums_all = [
            num - num_prefix_cached_blocks // remote_cp_size
            for num in remote_block_nums_all
        ]
        num_remain_blocks = num_prefix_cached_blocks % remote_cp_size
        for i in range(num_remain_blocks):
            remote_block_nums_all[i] -= 1

        # make sure the last block (which may be unfull) of P nodes is put to the last block of D node
        remote_block_nums: list[int] = []
        shard_cp_ranks: list[int] = []
        final_block_idx: int | None = None

        for cp_rank, block_num in enumerate(remote_block_nums_all):
            # When r_blk = 1, it degrades to the original cp_rank % Lcp rule.
            if (cp_rank // r_blk) % local_cp_size == local_cp_rank:
                if last_block_location == cp_rank:
                    final_block_idx = len(remote_block_nums)
                remote_block_nums.append(block_num)
                shard_cp_ranks.append(cp_rank)

        assert local_remote_block_port_mapping is not None
        if final_block_idx is not None:
            final_block_num = remote_block_nums.pop(final_block_idx)
            shard_cp_ranks.append(shard_cp_ranks.pop(final_block_idx))
            remote_block_nums.append(final_block_num)
            for mapping in local_remote_block_port_mapping:
                final_block_port = mapping.pop(final_block_idx)
                mapping.append(final_block_port)

        # Number of matched P-blocks in the prefix (Note: use P-side unit)
        num_prefix_p_blocks = num_prefix_cached_blocks
        if r_blk > 1:
            # The prefix match granularity for D is Bd = r_blk * Bp, so P0 must be an integer multiple of r_blk.
            if num_prefix_p_blocks % r_blk != 0:
                raise ValueError(
                    f"num_prefix_p_blocks({num_prefix_p_blocks}) must be an integer "
                    f"multiple of r_blk({r_blk})."
                )

        # The first D-block in the external zone (global block ID in D-units)
        # and the first external D-block owned by this rank.
        num_prefix_d_blocks = num_prefix_p_blocks // r_blk
        first_d = num_prefix_d_blocks + (
            (local_cp_rank - num_prefix_d_blocks) % local_cp_size
        )

        remote_handshake_port_list, local_block_ids_list, remote_block_ids_list = (
            [],
            [],
            [],
        )
        for idx in range(len(local_remote_block_port_mapping[0])):
            mapping_list = []
            for mapping in local_remote_block_port_mapping:
                mapping_list.append(mapping[idx])
            remote_handshake_port_list.append(mapping_list)

        # Attention port TP offset = kv_head_group_offset + dcp_repeat_offset + dcp_rank
        # Within the same head group, only the TP replicas that share the same dcp_rank but have
        # different dcp_repeat hold the exact same attention KV shard. Therefore, substitution is
        # strictly limited to the dcp_repeat (replica) dimension. The head group and dcp_rank parts
        # must be preserved as-is; otherwise, different DCP shards will end up fetching duplicated KV caches.
        remote_handshake_port_list = self._set_hma_shared_port(
            prefill_tp_size, meta, remote_handshake_port_list, req_id
        )

        # the local_block_ids_list and remote_block_ids_list are related with remote_handshake_port_list
        # such as: local_block_ids_list[[1],[2],[5],[6]], remote_block_ids_list[[1],[1],[1],[1]],
        # remote_handshake_port_list[[30000],[30001],[30004],[30005]]
        # D rank will get remote block 1 in port 30004 and save it in local block 5

        for remote_kv_id in range(len(remote_handshake_port_list)):
            num_blocks_to_pull = remote_block_nums[remote_kv_id]
            # rank-local index of this shard's first external block; used both to slice
            # the remote ids and to derive the matching local kernel positions.
            shard_cp_rank = shard_cp_ranks[remote_kv_id]
            remote_first = (
                num_prefix_p_blocks - shard_cp_rank + remote_cp_size - 1
            ) // remote_cp_size

            group_remote_block_ids: list[list[int]] = []
            group_local_block_ids: list[list[int]] = []
            is_final_shard = remote_kv_id == len(remote_handshake_port_list) - 1
            for group_idx, (group_spec, _) in kv_group_items:
                if group_spec["kv_cache_spec_type"] == "MambaSpec":
                    # Mamba state is not context-block sharded like attention
                    # KV. Transfer the final state from the final PCP/DCP shard.
                    group_remote_block_ids.append(
                        list(meta.remote_block_ids[group_idx]) if is_final_shard else []
                    )
                    group_local_block_ids.append(
                        list(meta.local_block_ids[group_idx]) if is_final_shard else []
                    )
                    continue
                # Attention: expand to kernel blocks here. Remote is sliced from remote_first
                # (skips this rank's prefix-cached blocks) then expanded; local kernels are
                # located directly from CP rank + block index. This removes the need to pass
                # chunk_starts down to the transfer stage. A shard that pulls nothing has
                # n == 0, so both kernel lists naturally come out empty.
                _, remote_scale, kernel_size = group_kernel_params[group_idx]
                remote_logical = list(
                    meta.remote_block_ids[group_idx][
                        remote_first : remote_first + num_blocks_to_pull
                    ]
                )
                kernel_remote = self._expand_block_ids(remote_logical, remote_scale)
                kernel_local = self._local_kernel_ids_for_shard(
                    remote_first,
                    num_blocks_to_pull,
                    shard_cp_rank,
                    num_prefix_p_blocks,
                    first_d,
                    r_blk,
                    local_cp_size,
                    remote_cp_size,
                    remote_block_size,
                    kernel_size,
                    list(meta.local_block_ids[group_idx]),
                )
                num_kernel_blocks = min(len(kernel_remote), len(kernel_local))
                group_remote_block_ids.append(kernel_remote[:num_kernel_blocks])

                group_local_block_ids.append(kernel_local[:num_kernel_blocks])
            remote_block_ids_list.append(tuple(group_remote_block_ids))
            local_block_ids_list.append(tuple(group_local_block_ids))

        tp_num_need_pulls = self._get_tp_num_need_pulls(prefill_tp_size)
        if self._is_hma_required:
            # HMA: The final shard might be padded with Mamba ports;
            # the total port count is permitted to exceed the number required by attention.
            if len(remote_handshake_port_list[0]) < tp_num_need_pulls:
                raise ValueError(
                    f"tp_num_need_pulls({tp_num_need_pulls}) exceeds the number of "
                    f"remote ports({remote_handshake_port_list[0]})."
                )
        elif tp_num_need_pulls != len(remote_handshake_port_list[0]):
            raise ValueError(
                f"tp_num_need_pulls({tp_num_need_pulls}) does not match the number "
                f"of remote ports({remote_handshake_port_list[0]})."
            )

        return KVSplitResult(
            remote_handshake_port_list, local_block_ids_list, remote_block_ids_list
        )

    def _get_cp_shard_pulls(
        self,
        remote_handshake_port_list,
        prefill_tp_size,
        remote_base_port,
        remote_pcp_size,
    ):
        # CP case: `group_pulls` is derived from `port` (which already includes the random selection result),
        # eliminating the need for a table lookup.
        mamba_num = prefill_tp_size // self.tp_size
        attn_num = self._get_tp_num_need_pulls(prefill_tp_size)
        attn_gids = [
            g
            for g, (spec, li) in self.kv_group2layeridx.items()
            if li and spec["kv_cache_spec_type"] != "MambaSpec"
        ]
        mamba_gids = [
            g
            for g, (spec, li) in self.kv_group2layeridx.items()
            if li and spec["kv_cache_spec_type"] == "MambaSpec"
        ]
        num_shards = len(remote_handshake_port_list)
        result = []
        for shard_idx, ports in enumerate(remote_handshake_port_list):
            is_final = shard_idx == num_shards - 1
            shard_pulls = []
            for port_idx, port in enumerate(ports):
                pulls = []
                port_tp = (port - remote_base_port) % prefill_tp_size
                # PCP and PP are mutually exclusive; when PCP > 1, pp_rank is always 0.
                pp_rank = (
                    0
                    if remote_pcp_size > 1
                    else (port - remote_base_port) // prefill_tp_size
                )
                # The first attn_num ports of each shard (i.e., the original ports with randomly substituted TPs).
                if port_idx < attn_num:
                    pulls += [
                        GroupPull(
                            group_id=g,
                            remote_tp_offset=port_idx,
                            num_group_pulls=attn_num,
                            prefill_pp_rank=pp_rank,
                            is_group_transfer_end=port_idx == attn_num - 1,
                        )
                        for g in attn_gids
                    ]
                # Mamba: Only applicable to the final shard; the offset is back-calculated from the port's TP ID.
                if is_final:
                    m_off = port_tp - self.tp_rank * mamba_num
                    if 0 <= m_off < mamba_num:
                        pulls += [
                            GroupPull(
                                group_id=g,
                                remote_tp_offset=m_off,
                                num_group_pulls=mamba_num,
                                prefill_pp_rank=pp_rank,
                                is_group_transfer_end=m_off == mamba_num - 1,
                            )
                            for g in mamba_gids
                        ]
                shard_pulls.append(pulls)
            result.append(shard_pulls)
        return result

    def _get_group_pulls_metadata(
        self,
        req_id: str,
        remote_handshake_port_list: list[list[int]],
        prefill_tp_size: int,
        remote_base_port: int,
        remote_pcp_size: int = 1,
        remote_dcp_size: int = 1,
    ) -> list[list[list[GroupPull]]]:
        """Build per-port KV cache group pull descriptors.

        Args:
            req_id: Remote request id used to reproduce hybrid-attention rank
                selection for the same request.
            remote_handshake_port_list: Output from ``_get_kv_split_metadata``.
                Each outer item is one transfer shard; each inner item is a
                remote P worker handshake port.
            prefill_tp_size: Effective remote prefill TP size. This may come
                from ``meta.remote_ptp_size`` when P and D use different TP
                sizes.
            remote_base_port: Remote P-side handshake base port. A remote
                worker rank is ``remote_handshake_port - remote_base_port``.

        Returns:
            A three-level list aligned with ``remote_handshake_port_list``:
            ``result[shard_idx][remote_port_idx]`` is the list of ``GroupPull``
            entries for that remote port. Each ``GroupPull`` identifies the KV
            cache group, the remote TP offset to read, the number of pulls
            needed to assemble that group, the prefill PP rank, and whether
            this pull is the final pull for the group. The final-pull flag is
            used by the receiver to decide when group reformatting can run.
        """
        cp_transfer = (
            remote_pcp_size * remote_dcp_size * self.pcp_size * self.dcp_size > 1
        )
        if self._is_hma_required:
            if not cp_transfer:
                # Non-CP case: port = base + chosen_rank, which has a one-to-one correspondence
                # with the table keys, maintaining the original logic.
                _, rank_group_pulls = self._get_hybrid_remote_rank_group_pulls(
                    req_id, prefill_tp_size
                )
                return [
                    [rank_group_pulls[p - remote_base_port] for p in ports]
                    for ports in remote_handshake_port_list
                ]

            # CP case: `group_pulls` is derived from `port` (which already includes the random selection result),
            # eliminating the need for a table lookup.
            return self._get_cp_shard_pulls(
                remote_handshake_port_list,
                prefill_tp_size,
                remote_base_port,
                remote_pcp_size,
            )

        tp_num_need_pulls = self._get_tp_num_need_pulls(prefill_tp_size)
        group_ids = [
            group_id
            for group_id, (_, layer_indices) in self.kv_group2layeridx.items()
            if layer_indices
        ]

        def make_group_pulls(
            remote_tp_offset: int, prefill_pp_rank: int
        ) -> list[GroupPull]:
            return [
                GroupPull(
                    group_id=group_id,
                    remote_tp_offset=remote_tp_offset,
                    num_group_pulls=tp_num_need_pulls,
                    prefill_pp_rank=prefill_pp_rank,
                    is_group_transfer_end=remote_tp_offset == tp_num_need_pulls - 1,
                )
                for group_id in group_ids
            ]

        group_pulls_list = []
        for pcp_dcp_rank, remote_ports in enumerate(remote_handshake_port_list):
            if len(remote_ports) == 1:
                remote_tp_offsets = [pcp_dcp_rank % tp_num_need_pulls]
                prefill_pp_ranks = [
                    (
                        (remote_ports[0] - remote_base_port)
                        % (prefill_tp_size * self._prefill_pp_size)
                    )
                    // prefill_tp_size
                ]
            else:
                if len(remote_ports) % tp_num_need_pulls != 0:
                    raise ValueError(
                        f"Number of remote ports({remote_ports}) must be divisible "
                        f"by tp_num_need_pulls({tp_num_need_pulls})."
                    )
                remote_tp_offsets = [
                    rank_idx % tp_num_need_pulls
                    for rank_idx in range(len(remote_ports))
                ]
                prefill_pp_ranks = [
                    (
                        (remote_port - remote_base_port)
                        % (prefill_tp_size * self._prefill_pp_size)
                    )
                    // prefill_tp_size
                    for remote_port in remote_ports
                ]
            group_pulls_list.append(
                [
                    make_group_pulls(remote_tp_offset, prefill_pp_rank)
                    for remote_tp_offset, prefill_pp_rank in zip(
                        remote_tp_offsets, prefill_pp_ranks
                    )
                ]
            )
        return group_pulls_list

    def _get_hybrid_remote_rank_group_pulls(
        self,
        req_id: str,
        prefill_tp_size: int,
    ) -> tuple[list[int], dict[int, list[GroupPull]]]:
        rank_group_pulls: OrderedDict[int, list[GroupPull]] = OrderedDict()

        def add_group_pull(remote_rank: int, group_pull: GroupPull) -> None:
            rank_group_pulls.setdefault(remote_rank, []).append(group_pull)

        for group_id, (group_spec, layer_indices) in self.kv_group2layeridx.items():
            if not layer_indices:
                continue

            if group_spec["kv_cache_spec_type"] == "MambaSpec":
                if prefill_tp_size % self.tp_size != 0:
                    raise ValueError(
                        f"Hybrid Mamba prefill tp size({prefill_tp_size}) must be "
                        f"divisible by decode tp size({self.tp_size})."
                    )
                num_group_pulls = prefill_tp_size // self.tp_size
                for pp_rank in range(self._prefill_pp_size):
                    pp_rank_offset = pp_rank * prefill_tp_size
                    local_tp_offset = self.tp_rank * num_group_pulls
                    for remote_tp_offset in range(num_group_pulls):
                        remote_rank = (
                            pp_rank_offset + local_tp_offset + remote_tp_offset
                        )
                        add_group_pull(
                            remote_rank,
                            GroupPull(
                                group_id=group_id,
                                remote_tp_offset=remote_tp_offset,
                                num_group_pulls=num_group_pulls,
                                prefill_pp_rank=pp_rank,
                                is_group_transfer_end=remote_tp_offset
                                == num_group_pulls - 1,
                            ),
                        )
                continue

            num_group_pulls = self._get_attention_group_num_need_pulls(
                group_spec, prefill_tp_size
            )
            chosen_rank_list = self._get_attention_group_remote_rank(
                req_id, group_spec, prefill_tp_size
            )
            if len(chosen_rank_list) != num_group_pulls * self._prefill_pp_size:
                raise ValueError(
                    f"chosen_rank_list({chosen_rank_list}) does not match "
                    f"num_group_pulls({num_group_pulls}) and prefill pp "
                    f"size({self._prefill_pp_size})."
                )
            for rank_idx, remote_rank in enumerate(chosen_rank_list):
                prefill_pp_rank = rank_idx // num_group_pulls
                add_group_pull(
                    remote_rank,
                    GroupPull(
                        group_id=group_id,
                        remote_tp_offset=rank_idx % num_group_pulls,
                        num_group_pulls=num_group_pulls,
                        prefill_pp_rank=prefill_pp_rank,
                        is_group_transfer_end=rank_idx % num_group_pulls
                        == num_group_pulls - 1,
                    ),
                )

        return list(rank_group_pulls), dict(rank_group_pulls)

    def _get_attention_group_num_need_pulls(
        self, group_spec: dict[str, Any], prefill_tp_size: int
    ) -> int:
        return self._get_attention_group_num_need_pulls_for_decode_tp(
            group_spec, prefill_tp_size, self.tp_size
        )

    def _get_attention_group_num_need_pulls_for_decode_tp(
        self,
        group_spec: dict[str, Any],
        prefill_tp_size: int,
        decode_tp_size: int,
    ) -> int:
        num_key_value_heads = self._get_attention_group_num_key_value_heads(group_spec)
        num_d_block_heads = max(1, num_key_value_heads // decode_tp_size)
        num_p_block_heads = max(1, num_key_value_heads // prefill_tp_size)
        return num_d_block_heads // num_p_block_heads

    def _get_attention_group_num_key_value_heads(
        self, group_spec: dict[str, Any]
    ) -> int:
        kv_cache_spec = group_spec.get("kv_cache_spec", {})
        if isinstance(kv_cache_spec, dict):
            for key in ("total_num_kv_heads", "num_kv_heads", "num_key_value_heads"):
                num_key_value_heads = kv_cache_spec.get(key)
                if isinstance(num_key_value_heads, int):
                    return num_key_value_heads
            for spec in kv_cache_spec.values():
                if not isinstance(spec, dict):
                    continue
                for key in (
                    "total_num_kv_heads",
                    "num_kv_heads",
                    "num_key_value_heads",
                ):
                    num_key_value_heads = spec.get(key)
                    if isinstance(num_key_value_heads, int):
                        return num_key_value_heads
        return self.num_key_value_heads

    def _get_attention_group_remote_rank(
        self,
        req_id: str,
        group_spec: dict[str, Any],
        prefill_tp_size: int,
    ) -> list[int]:
        num_key_value_heads = self._get_attention_group_num_key_value_heads(group_spec)
        num_group_pulls = self._get_attention_group_num_need_pulls(
            group_spec, prefill_tp_size
        )
        return self._get_remote_ranks_for_req(
            req_id,
            prefill_tp_size,
            num_key_value_heads=num_key_value_heads,
            tp_num_need_pulls=num_group_pulls,
            use_mla=num_key_value_heads == 1,
        )[self.tp_rank]

    def _get_sfa_replicate_k_block_ids(
        self,
        meta: ReqMeta,
    ) -> tuple[BlockIds, BlockIds]:
        if not self.enable_sfa_dcp_replicated_indexer:
            return tuple(), tuple()
        if (
            meta.num_external_tokens <= 0
            or not meta.remote_block_ids
            or not meta.local_block_ids
        ):
            return tuple(), tuple()

        if len(meta.remote_block_ids) != 1 or len(meta.local_block_ids) != 1:
            raise ValueError(
                "SFA replicate-K currently expects exactly one KV cache group. "
                f"Got remote groups={len(meta.remote_block_ids)}, local groups={len(meta.local_block_ids)}."
            )

        # Zero remote CP sizes would pass the divisibility check below
        # (0 is divisible by anything) and flow into the shard math, so
        # validate the remote-derived sizes first.
        if meta.remote_pcp_size < 1 or meta.remote_dcp_size < 1:
            raise ValueError(
                f"Remote CP sizes must be >= 1, got remote_pcp_size="
                f"{meta.remote_pcp_size}, remote_dcp_size={meta.remote_dcp_size}."
            )
        remote_cp_size = meta.remote_pcp_size * meta.remote_dcp_size
        local_cp_size = self.pcp_size * self.dcp_size
        if local_cp_size == 0 or remote_cp_size % local_cp_size != 0:
            raise ValueError(
                f"SFA replicate-K expects remote cp size({remote_cp_size}) to be divisible by "
                f"local cp size({local_cp_size})."
            )

        num_prefix_cached_blocks = min(
            meta.num_computed_tokens // self.block_size, meta.num_prompt_blocks
        )
        num_external_blocks = meta.num_prompt_blocks - num_prefix_cached_blocks
        num_external_blocks_from_tokens = math.ceil(
            meta.num_external_tokens / self.block_size
        )
        if num_external_blocks < num_external_blocks_from_tokens:
            raise ValueError(
                f"num_external_blocks({num_external_blocks}) derived from num_computed_tokens "
                f"must cover num_external_blocks_from_tokens({num_external_blocks_from_tokens})."
            )

        if num_prefix_cached_blocks > 0 and not meta.local_full_block_ids:
            raise ValueError(
                "SFA replicate-K requires full local block ids when prefix cache is used."
            )

        remote_blocks = list(meta.remote_block_ids[0])
        local_full_blocks = list((meta.local_full_block_ids or meta.local_block_ids)[0])
        if not local_full_blocks:
            return tuple(), tuple()

        local_block_ids: list[int] = []
        remote_block_ids: list[int] = []
        for global_block_idx in range(num_prefix_cached_blocks, meta.num_prompt_blocks):
            remote_local_idx = global_block_idx // remote_cp_size
            local_local_idx = global_block_idx // local_cp_size
            if remote_local_idx >= len(remote_blocks) or local_local_idx >= len(
                local_full_blocks
            ):
                break
            remote_block_ids.append(
                int(remote_blocks[remote_local_idx]) * remote_cp_size
                + global_block_idx % remote_cp_size
            )
            local_block_ids.append(
                int(local_full_blocks[local_local_idx]) * local_cp_size
                + global_block_idx % local_cp_size
            )

        local_block_ids = local_block_ids[:num_external_blocks]
        remote_block_ids = remote_block_ids[: len(local_block_ids)]
        local_block_ids = local_block_ids[: len(remote_block_ids)]

        logger.debug(
            "HIXL SFA replicate-K block ids prepared from aligned full blocks. "
            "remote_cp_size=%s local_cp_size=%s num_prompt_blocks=%s num_computed_tokens=%s "
            "num_external_blocks=%s local_len=%s remote_len=%s",
            remote_cp_size,
            local_cp_size,
            meta.num_prompt_blocks,
            meta.num_computed_tokens,
            num_external_blocks,
            len(local_block_ids),
            len(remote_block_ids),
        )

        return (local_block_ids,), (remote_block_ids,)

    def start_load_kv(self, metadata: HIXLConnectorMetadata):
        """Enqueue pulls only. Submit and wait stay on the receiving thread."""
        for req_id in metadata.reqs_in_batch:
            if self.kv_send_thread is not None:
                self.kv_send_thread.task_tracker.add_req_to_process(req_id)
            if self.kv_recv_thread is not None:
                self.kv_recv_thread.task_tracker.add_req_to_process(req_id)

        for req_id, meta in metadata.requests.items():
            if logger.isEnabledFor(logging.DEBUG):
                logger.debug(
                    "start_load_kv for request %s from remote engine %s. "
                    "Num local_block_ids: %s. Num remote_block_ids: %s.",
                    req_id,
                    meta.remote_engine_id,
                    len(meta.local_block_ids),
                    len(meta.remote_block_ids),
                )

            remote_req_id = meta.remote_request_id
            prefill_tp_size: int = (
                meta.remote_ptp_size
                if meta.remote_ptp_size is not None
                else self._prefill_tp_size
            )
            (
                local_block_ids_replicate_k,
                remote_block_ids_replicate_k,
            ) = self._get_sfa_replicate_k_block_ids(meta)

            (
                remote_handshake_port_list,
                local_block_ids_list,
                remote_block_ids_list,
            ) = self._get_kv_split_metadata(remote_req_id, meta)
            has_replicate_k_blocks = any(local_block_ids_replicate_k) and any(
                remote_block_ids_replicate_k
            )
            remote_transfer_ports = [
                port
                for remote_ports in remote_handshake_port_list
                for port in remote_ports
            ]
            if has_replicate_k_blocks and not remote_transfer_ports:
                raise ValueError(
                    "SFA replicate-K requires at least one normal KV transfer port."
                )
            replicate_k_transfer_port = (
                remote_transfer_ports[0] if has_replicate_k_blocks else None
            )
            group_pulls_list = self._get_group_pulls_metadata(
                remote_req_id,
                remote_handshake_port_list,
                prefill_tp_size,
                meta.remote_port,
                meta.remote_pcp_size,
                meta.remote_dcp_size,
            )

            for pcp_dcp_rank, remote_ports in enumerate(remote_handshake_port_list):
                for remote_tp_offset, remote_handshake_port in enumerate(remote_ports):
                    assert self.kv_recv_thread is not None
                    remote_host, remote_engine_id = self._get_remote_host_info_by_port(
                        meta.remote_port,
                        remote_handshake_port,
                        meta.remote_host,
                        meta.remote_engine_id,
                        meta.remote_multi_nodes_meta_mapping,
                    )
                    remote_port_send_num = (
                        self.remote_port_send_num[meta.remote_engine_id]
                        if meta.remote_pcp_size * meta.remote_dcp_size > 1
                        else None
                    )
                    local_block_ids_replicate_k_for_port = (
                        local_block_ids_replicate_k
                        if replicate_k_transfer_port is not None
                        and remote_handshake_port == replicate_k_transfer_port
                        else None
                    )
                    remote_block_ids_replicate_k_for_port = (
                        remote_block_ids_replicate_k
                        if replicate_k_transfer_port is not None
                        and remote_handshake_port == replicate_k_transfer_port
                        else None
                    )
                    self.kv_recv_thread.add_request(
                        request_id=req_id,
                        remote_request_id=remote_req_id,
                        local_block_ids=local_block_ids_list[pcp_dcp_rank],
                        remote_block_ids=remote_block_ids_list[pcp_dcp_rank],
                        group_pulls=group_pulls_list[pcp_dcp_rank][remote_tp_offset],
                        remote_engine_id=remote_engine_id,
                        remote_host=remote_host,
                        remote_handshake_port=remote_handshake_port,
                        remote_port_send_num=remote_port_send_num,
                        num_computed_tokens=meta.num_computed_tokens,
                        all_task_done=(
                            pcp_dcp_rank == len(remote_handshake_port_list) - 1
                            and remote_tp_offset == len(remote_ports) - 1
                        ),
                        shard_idx=pcp_dcp_rank,
                        remote_block_size=meta.remote_block_size,
                        local_block_ids_replicate_k=local_block_ids_replicate_k_for_port,
                        remote_block_ids_replicate_k=remote_block_ids_replicate_k_for_port,
                    )

        if self.kv_send_thread is not None and self.pcp_size * self.dcp_size == 1:
            for req_id, delay_start_time in metadata.requests_to_send.items():
                if self.tp_rank in self._prefill_get_remote_rank(req_id):
                    self.kv_send_thread.add_delayed_request(req_id, delay_start_time)
                else:
                    self.kv_send_thread.add_not_transfer_request(req_id)

        if self.kv_send_thread is not None and self.pcp_size * self.dcp_size > 1:
            for req_id, delay_start_time in metadata.requests_to_send.items():
                self.kv_send_thread.add_delayed_request(req_id, delay_start_time)

    def _get_tp_num_need_pulls(self, prefill_tp_size: int | None) -> int:
        if prefill_tp_size is None:
            prefill_tp_size = self._prefill_tp_size

        if prefill_tp_size == self._prefill_tp_size:
            return self.tp_num_need_pulls

        if self.vllm_config.model_config.is_deepseek_mla:
            tp_num_need_pulls = 1
        else:
            num_d_block_heads = max(1, self.num_key_value_heads // self.tp_size)
            num_p_block_heads = max(1, self.num_key_value_heads // prefill_tp_size)
            tp_num_need_pulls = num_d_block_heads // num_p_block_heads
        return tp_num_need_pulls

    def _get_remote_host_info_by_port(
        self,
        base_port: int,
        remote_handshake_port: int,
        remote_host: str,
        remote_engine_id: str,
        remote_multi_nodes_meta_mapping: dict,
    ):
        if remote_multi_nodes_meta_mapping is None:
            return remote_host, remote_engine_id

        kv_port = self.vllm_config.kv_transfer_config.kv_port
        rank = str(remote_handshake_port - kv_port)
        info = remote_multi_nodes_meta_mapping.get(rank)
        if info is None:
            rank = str(remote_handshake_port - base_port)
            info = remote_multi_nodes_meta_mapping.get(rank)
        if info is None:
            return remote_host, remote_engine_id
        return info.get("host", remote_host), info.get("engine_id", remote_engine_id)

    def _prefill_get_remote_rank(self, req_id: str) -> list[int]:
        if self._is_hma_required:
            prefill_ranks: set[int] = set()
            for group_spec, layer_indices in self.kv_group2layeridx.values():
                if layer_indices:
                    prefill_ranks.update(
                        self._get_prefill_ranks_for_group(req_id, group_spec)
                    )
            return sorted(prefill_ranks)
        return sum(self._get_remote_ranks_for_req(req_id), [])

    def _get_prefill_ranks_for_group(
        self, req_id: str, group_spec: dict[str, Any]
    ) -> set[int]:
        if group_spec["kv_cache_spec_type"] == "MambaSpec":
            if self._prefill_tp_size % self._decode_tp_size != 0:
                raise ValueError(
                    f"Hybrid Mamba prefill tp size({self._prefill_tp_size}) must be "
                    f"divisible by decode tp size({self._decode_tp_size})."
                )
            return set(range(self._prefill_tp_size * self._prefill_pp_size))

        num_key_value_heads = self._get_attention_group_num_key_value_heads(group_spec)
        num_group_pulls = self._get_attention_group_num_need_pulls_for_decode_tp(
            group_spec,
            self._prefill_tp_size,
            self._decode_tp_size,
        )
        remote_ranks_by_decode_rank = self._get_remote_ranks_for_req(
            req_id,
            self._prefill_tp_size,
            num_key_value_heads=num_key_value_heads,
            tp_num_need_pulls=num_group_pulls,
            use_mla=num_key_value_heads == 1,
        )
        return {
            rank
            for remote_ranks in remote_ranks_by_decode_rank
            for rank in remote_ranks
        }

    def _get_remote_rank(
        self, req_id: str, prefill_tp_size: int | None = None
    ) -> list[int]:
        return self._get_remote_ranks_for_req(req_id, prefill_tp_size)[self.tp_rank]

    def _get_remote_tp_ranks(
        self,
        tp_ori_data: np.ndarray,
        rand_group_index: list[int],
        num_groups: int,
        prefill_tp_size: int,
        num_key_value_heads: int,
        tp_num_need_pulls: int,
        use_mla: bool,
    ) -> list[list[int]]:
        # random split prefill tp list
        tp_sampled_nums = []
        if prefill_tp_size > num_key_value_heads or use_mla or self.use_sparse:
            tp_ori_data = tp_ori_data.reshape(-1, num_groups)
            chosen_group = tp_ori_data[:, [rand_group_index]]
            flattened = chosen_group.reshape(-1).tolist()
            tp_sampled_nums = [
                flattened[i : i + tp_num_need_pulls]
                for i in range(0, len(flattened), tp_num_need_pulls)
            ]
        # non-random split
        else:
            group_size = prefill_tp_size // self._decode_tp_size
            for i in range(self._decode_tp_size):
                tp_shard = tp_ori_data[i * group_size : (i + 1) * group_size]
                tp_sampled_nums.append(tp_shard.tolist())
        return tp_sampled_nums

    def _get_remote_ranks_for_req(
        self,
        req_id: str,
        prefill_tp_size: int | None = None,
        num_key_value_heads: int | None = None,
        tp_num_need_pulls: int | None = None,
        use_mla: bool | None = None,
    ) -> list[list[int]]:
        if prefill_tp_size is None:
            prefill_tp_size = self._prefill_tp_size
        if num_key_value_heads is None:
            if self.vllm_config.model_config.is_deepseek_mla or self.use_sparse:
                num_key_value_heads = 1
            else:
                num_key_value_heads = self.num_key_value_heads
        if tp_num_need_pulls is None:
            tp_num_need_pulls = self._get_tp_num_need_pulls(prefill_tp_size)
        if use_mla is None:
            use_mla = self.vllm_config.model_config.is_deepseek_mla

        # Divide the ports according to the TP within the PP
        sampled_nums = []
        if prefill_tp_size == self._decode_tp_size:
            sampled_nums = list(
                map(
                    lambda tp: [
                        tp + pp * prefill_tp_size for pp in range(self._prefill_pp_size)
                    ],
                    range(prefill_tp_size),
                )
            )
            return sampled_nums
        num_kv_head = num_key_value_heads
        ori_data = np.arange(prefill_tp_size * self._prefill_pp_size)
        seed = string_to_int64_hash(req_id)
        rand = random.Random(seed)
        # random split prefill tp list
        ori_data_2d = ori_data.reshape(self._prefill_pp_size, -1)
        num_groups = max(
            1, len(ori_data_2d[0]) // num_kv_head
        )  # The number of redundant copies for each KV head within the PP stage
        rand_group_index = rand.sample(
            range(num_groups), (max(self._decode_tp_size // num_kv_head, 1))
        )  # random choose a group
        all_results = [
            self._get_remote_tp_ranks(
                ori_data_2d[pp_index],
                rand_group_index,
                num_groups,
                prefill_tp_size,
                num_key_value_heads,
                tp_num_need_pulls,
                use_mla,
            )
            for pp_index in range(self._prefill_pp_size)
        ]
        for group_index in range(len(all_results[0])):
            group = []
            for pp_index in range(self._prefill_pp_size):
                group.extend(all_results[pp_index][group_index])
            sampled_nums.append(group)
        return sampled_nums
