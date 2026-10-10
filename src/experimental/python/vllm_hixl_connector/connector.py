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

"""``HIXLConnector`` entry point and its scheduler-side logic."""

import logging
import math
import time
from collections.abc import Mapping
from typing import Any, TYPE_CHECKING
import torch
from vllm.config import VllmConfig
from vllm.distributed.kv_transfer.kv_connector.utils import BlockIds
from vllm.distributed.kv_transfer.kv_connector.v1.base import (
    KVConnectorBase_V1,
    KVConnectorHandshakeMetadata,
    KVConnectorMetadata,
    KVConnectorRole,
    SupportsHMA,
)
from vllm.logger import logger
from vllm.utils.math_utils import cdiv
from vllm.utils.network_utils import get_ip
from vllm.v1.core.sched.output import SchedulerOutput
from vllm.v1.kv_cache_interface import (
    FullAttentionSpec,
    KVCacheConfig,
    MambaSpec,
    SlidingWindowSpec,
    UniformTypeKVCacheSpecs,
)
from vllm.v1.request import RequestStatus
from vllm_ascend.ascend_config import get_ascend_config, init_ascend_config
from .metadata import GroupTransferInfo, HIXLConnectorMetadata
from .worker import HIXLConnectorWorker

# isort: off
if TYPE_CHECKING:
    from vllm.v1.attention.backend import AttentionMetadata  # type: ignore
    from vllm.forward_context import ForwardContext
    from vllm.v1.core.kv_cache_manager import KVCacheBlocks
    from vllm.v1.request import Request
# isort: on


class HIXLConnector(KVConnectorBase_V1, SupportsHMA):
    def __init__(
        self,
        vllm_config: VllmConfig,
        role: KVConnectorRole,
        kv_cache_config: KVCacheConfig | None = None,
    ):
        assert vllm_config.kv_transfer_config is not None
        self.engine_id = vllm_config.kv_transfer_config.engine_id
        self._connector_metadata = HIXLConnectorMetadata()

        if role == KVConnectorRole.SCHEDULER:
            self.connector_scheduler: HIXLConnectorScheduler | None = (
                HIXLConnectorScheduler(
                    vllm_config, str(self.engine_id), kv_cache_config
                )
            )
            self.connector_worker: HIXLConnectorWorker | None = None
        elif role == KVConnectorRole.WORKER:
            self.connector_scheduler = None
            self.connector_worker = HIXLConnectorWorker(
                vllm_config, str(self.engine_id), kv_cache_config
            )

    ############################################################
    # Scheduler Side Methods
    ############################################################

    def get_num_new_matched_tokens(
        self, request: "Request", num_computed_tokens: int
    ) -> tuple[int, bool]:
        assert self.connector_scheduler is not None
        return self.connector_scheduler.get_num_new_matched_tokens(
            request, num_computed_tokens
        )

    def update_state_after_alloc(
        self, request: "Request", blocks: "KVCacheBlocks", num_external_tokens: int
    ):
        assert self.connector_scheduler is not None
        return self.connector_scheduler.update_state_after_alloc(
            request, blocks, num_external_tokens
        )

    def build_connector_meta(
        self,
        scheduler_output: SchedulerOutput,
    ) -> KVConnectorMetadata:
        assert self.connector_scheduler is not None
        return self.connector_scheduler.build_connector_meta(scheduler_output)

    def request_finished(
        self,
        request: "Request",
        block_ids: list[int],
    ) -> tuple[bool, dict[str, Any] | None]:
        assert self.connector_scheduler is not None
        return self.connector_scheduler.request_finished(request, (block_ids,))

    def request_finished_all_groups(
        self,
        request: "Request",
        block_ids: tuple[list[int], ...],
    ) -> tuple[bool, dict[str, Any] | None]:
        assert self.connector_scheduler is not None
        return self.connector_scheduler.request_finished(request, block_ids)

    ############################################################
    # Worker Side Methods
    ############################################################
    def register_kv_caches(self, kv_caches: dict[str, torch.Tensor]):
        assert self.connector_worker is not None
        self.connector_worker.register_kv_caches(kv_caches)

    def get_finished(self, finished_req_ids: set[str]) -> tuple[set[str], set[str]]:
        """Get the finished recving and sending requests."""
        assert self.connector_worker is not None
        return self.connector_worker.get_finished()

    def get_block_ids_with_load_errors(self) -> set[int]:
        """Get the block ids whose KV load failed."""
        assert self.connector_worker is not None
        return self.connector_worker.get_block_ids_with_load_errors()

    def start_load_kv(self, forward_context: "ForwardContext", **kwargs) -> None:
        assert self.connector_worker is not None
        assert isinstance(self._connector_metadata, HIXLConnectorMetadata)
        self.connector_worker.start_load_kv(self._connector_metadata)

    def wait_for_layer_load(self, layer_name: str) -> None:
        """HIXLConnector does not do layerwise saving."""
        pass

    def save_kv_layer(
        self,
        layer_name: str,
        kv_layer: torch.Tensor,
        attn_metadata: "AttentionMetadata",
        **kwargs,
    ) -> None:
        """HIXLConnector does not save explicitly."""
        pass

    def wait_for_save(self):
        """HIXLConnector does not save explicitly."""
        pass

    def get_handshake_metadata(self) -> KVConnectorHandshakeMetadata | None:
        """
        Get the KVConnector handshake metadata for this connector.
        This metadata is used for out-of-band connector handshake
        between P/D workers.

        Returns:
            KVConnectorHandshakeMetadata: the handshake metadata.
            None if no handshake metadata is available.
        """
        assert self.connector_worker is not None
        return self.connector_worker.xfer_handshake_metadata

    def set_xfer_handshake_metadata(
        self, metadata: Mapping[int | tuple[int, ...], KVConnectorHandshakeMetadata]
    ) -> None:
        """
        Set the KV connector handshake metadata for this connector.

        Args:
            metadata (dict): the handshake metadata to set.
        """
        assert self.connector_scheduler is not None
        self.connector_scheduler.set_xfer_handshake_metadata(metadata)

    def set_xfer_handshake_metadata_pp_aware(
        self, metadata: Mapping[int | tuple[int, ...], KVConnectorHandshakeMetadata]
    ) -> None:
        assert self.connector_scheduler is not None
        self.connector_scheduler.set_xfer_handshake_metadata_from_workers(metadata)

    def shutdown(self):
        # The scheduler role never built a data plane, so only the worker has
        # an engine to release.
        if self.connector_worker is not None:
            self.connector_worker.shutdown()


class HIXLConnectorScheduler:
    """Implementation of Scheduler side methods"""

    def __init__(
        self, vllm_config: VllmConfig, engine_id: str, kv_cache_config: KVCacheConfig
    ):
        self.vllm_config = vllm_config
        self.kv_cache_config = kv_cache_config
        init_ascend_config(vllm_config)
        self.ascend_config = get_ascend_config()
        self.block_size = vllm_config.cache_config.block_size
        self.engine_id = engine_id
        self.local_ip = get_ip()
        logger.info("Initializing HIXLConnector Scheduler %s", engine_id)

        self.side_channel_host = get_ip()
        self.pcp_size = vllm_config.parallel_config.prefill_context_parallel_size
        self.dcp_size = vllm_config.parallel_config.decode_context_parallel_size
        self.tp_size = vllm_config.parallel_config.tensor_parallel_size
        self.max_device_id = (
            vllm_config.parallel_config.tensor_parallel_size
            * vllm_config.parallel_config.data_parallel_size
            * self.pcp_size
            * vllm_config.parallel_config.pipeline_parallel_size
        )

        # Handshake base port
        self.side_channel_port = (
            vllm_config.kv_transfer_config.kv_port
            + vllm_config.parallel_config.data_parallel_rank
            * vllm_config.parallel_config.tensor_parallel_size
            * vllm_config.parallel_config.pipeline_parallel_size
            * self.pcp_size
        )
        # Requests that need to start recv.
        # New requests are added by update_state_after_alloc in
        # the scheduler. Used to make metadata passed to Worker.
        self._reqs_need_recv: dict[str, tuple[Request, BlockIds, BlockIds, int]] = {}
        self._reqs_need_send: dict[str, float] = {}
        self._reqs_in_batch: set[str] = set()

        # master-slave meta information for cross-nodes
        self.multi_nodes_meta_mapping: dict[str, dict[str, Any]] = {}
        self.kv_cache_groups = kv_cache_config.kv_cache_groups
        self.use_hybrid = (
            not vllm_config.scheduler_config.disable_hybrid_kv_cache_manager
            and any(
                not isinstance(g.kv_cache_spec, FullAttentionSpec)
                for g in kv_cache_config.kv_cache_groups
            )
            and len(kv_cache_config.kv_cache_groups) > 1
        )
        self.use_compress = self._model_uses_compress()
        self.group_transfer_info = [
            self._get_group_transfer_info(group)
            for group in kv_cache_config.kv_cache_groups
        ]
        self.need_truncate = self.use_compress or any(
            info.is_state_group for info in self.group_transfer_info
        )

    def _model_uses_compress(self) -> bool:
        hf_config = getattr(self.vllm_config.model_config, "hf_config", None)
        compress_ratios = getattr(hf_config, "compress_ratios", None)
        return isinstance(compress_ratios, (list, tuple, dict))

    def _get_group_transfer_info(self, group: Any) -> GroupTransferInfo:
        specs = self._get_group_unique_specs(group)
        first_spec = specs[0] if specs else group.kv_cache_spec
        block_size = getattr(
            group.kv_cache_spec,
            "block_size",
            getattr(first_spec, "block_size", self.block_size),
        )
        is_state_group = any(isinstance(spec, MambaSpec) for spec in specs)
        sliding_window = 0
        compress_ratio = 1
        for spec in specs:
            if isinstance(spec, SlidingWindowSpec):
                sliding_window = spec.sliding_window
            elif hasattr(spec, "compress_ratio"):
                compress_ratio = spec.compress_ratio

        return GroupTransferInfo(
            tokens_per_block=block_size * max(1, int(compress_ratio)),
            blocks_per_window=cdiv(sliding_window, block_size) + 1
            if sliding_window
            else 0,
            is_state_group=is_state_group,
        )

    def _get_group_unique_specs(self, group: Any) -> list[Any]:
        if not isinstance(group.kv_cache_spec, UniformTypeKVCacheSpecs):
            return [group.kv_cache_spec]

        specs = []
        for layer_name in group.layer_names:
            layer_spec = group.kv_cache_spec.kv_cache_specs[layer_name]
            if layer_spec not in specs:
                specs.append(layer_spec)
        return specs

    def _get_transfer_block_ids(self, block_ids: BlockIds, prompt_len: int) -> BlockIds:
        """Return blocks that contain prompt KV, dropping MTP extra blocks.

        State groups such as Mamba are not context-block aligned with attention
        KV, so keep them unchanged and only clip attention-like groups here.
        SWA tail clipping is handled as a separate step after this.
        """
        if len(block_ids) == 0:
            return block_ids

        if len(block_ids) != len(self.group_transfer_info):
            raise ValueError(
                f"Number of KV cache groups({len(block_ids)}) does not match the "
                f"transfer layout({len(self.group_transfer_info)})."
            )

        transfer_block_ids = []
        cp_size = max(1, self.pcp_size * self.dcp_size)
        for blocks, group_info in zip(block_ids, self.group_transfer_info):
            if group_info.is_state_group:
                transfer_block_ids.append(blocks)
            else:
                # In context parallelism, each scheduler-visible block id is a
                # CP-grouped/virtual block shared by all CP ranks. It therefore
                # covers cp_size times the token span of one no-CP block.
                num_prompt_blocks = cdiv(
                    prompt_len, group_info.tokens_per_block * cp_size
                )
                transfer_block_ids.append(blocks[:num_prompt_blocks])
        return tuple(transfer_block_ids)

    def _get_swa_transfer_block_ids(self, block_ids: BlockIds) -> BlockIds:
        """Clip SWA groups to their window tail and drop placeholder block 0."""
        if len(block_ids) == 0:
            return block_ids

        if len(block_ids) != len(self.group_transfer_info):
            raise ValueError(
                f"Number of KV cache groups({len(block_ids)}) does not match the "
                f"transfer layout({len(self.group_transfer_info)})."
            )

        transfer_block_ids = []
        for blocks, group_info in zip(block_ids, self.group_transfer_info):
            if group_info.is_state_group or group_info.blocks_per_window == 0:
                transfer_block_ids.append(blocks)
            else:
                window_blocks = blocks[-group_info.blocks_per_window :]
                transfer_block_ids.append(
                    [block_id for block_id in window_blocks if block_id != 0]
                )
        return tuple(transfer_block_ids)

    def _state_prefill_token_count(self, num_prompt_tokens: int) -> int:
        """D-side only. Returns N-1 for Mamba models since the decoder
        always recomputes the last token and must start from h(N-1)."""
        if self.need_truncate and num_prompt_tokens > 1:
            return num_prompt_tokens - 1
        return num_prompt_tokens

    def _truncate_request_for_prefill(self, request: "Request") -> None:
        """P-side only: drop the last prompt token so the prefiller computes
        h(N-1) instead of h(N). The decoder recomputes the last token to
        derive h(N) correctly.

        Guarded by ``_p_side_truncated`` to avoid repeated truncation if the
        request is preempted and rescheduled."""
        params = request.kv_transfer_params
        if (
            params is not None
            # Guard against repeated truncation after preemption/reschedule.
            and not params.get("_p_side_truncated")
            and request.num_prompt_tokens > 1
        ):
            if request.prompt_token_ids is not None:
                request.prompt_token_ids.pop()
            elif request.prompt_embeds is not None:
                request.prompt_embeds = request.prompt_embeds[:-1]
            else:
                return

            request._all_token_ids.pop()
            request.num_prompt_tokens -= 1
            request.max_tokens = 1
            params["_p_side_truncated"] = True

    def get_num_new_matched_tokens(
        self, request: "Request", num_computed_tokens: int
    ) -> tuple[int, bool]:
        """
        For remote prefill, pull all prompt blocks from remote
        asynchronously relative to engine execution.

        Args:
            request (Request): the request object.
            num_computed_tokens (int): the number of locally
                computed tokens for this request
        Returns:
            * the number of tokens that can be loaded from the
              external KV cache beyond what is already computed.
            * true if the external KV cache tokens will be loaded
              asynchronously (between scheduler steps).
        """

        params = request.kv_transfer_params
        logger.debug(
            "HIXLConnector get_num_new_matched_tokens: num_computed_tokens=%s, kv_transfer_params=%s",
            num_computed_tokens,
            params,
        )

        if params is not None and params.get("do_remote_prefill"):
            # Remote prefill: get all prompt blocks from remote.
            token_ids = request.prompt_token_ids or []
            actual = self._state_prefill_token_count(len(token_ids))
            params["num_computed_tokens"] = num_computed_tokens
            count = max(actual - num_computed_tokens, 0)
            if count > 0:
                return count, True

        if params is not None and params.get("do_remote_decode") and self.need_truncate:
            self._truncate_request_for_prefill(request)

        # No remote prefill for this request.
        return 0, False

    def update_state_after_alloc(
        self, request: "Request", blocks: "KVCacheBlocks", num_external_tokens: int
    ):
        params = request.kv_transfer_params
        logger.debug(
            "HIXLConnector update_state_after_alloc: num_external_tokens=%s, kv_transfer_params=%s",
            num_external_tokens,
            params,
        )

        if params is not None and (
            params.get("do_remote_prefill", False)
            or params.get("do_remote_decode", False)
        ):
            self._reqs_in_batch.add(request.request_id)
        if params is not None and params.get("do_remote_prefill"):
            if params.get("remote_block_ids"):
                if all(
                    p in params
                    for p in (
                        "remote_engine_id",
                        "remote_host",
                        "remote_port",
                        "remote_request_id",
                    )
                ):
                    local_block_ids = (
                        blocks.get_unhashed_block_ids_all_groups()
                        if num_external_tokens > 0
                        else []
                    )
                    local_full_block_ids = (
                        blocks.get_block_ids() if num_external_tokens > 0 else tuple()
                    )
                    # Get unhashed blocks to pull from remote.
                    self._reqs_need_recv[request.request_id] = (
                        request,
                        local_block_ids,
                        local_full_block_ids,
                        num_external_tokens,
                    )
                else:
                    logger.warning("Got invalid KVTransferParams. params=%s.", params)
            elif num_external_tokens != 0:
                raise ValueError(
                    f"Request {request.request_id} reports "
                    f"num_external_tokens({num_external_tokens}) without remote "
                    "block ids in kv_transfer_params."
                )
            # Only trigger 1 KV transfer per request.
            params["do_remote_prefill"] = False

    def build_connector_meta(
        self,
        scheduler_output: SchedulerOutput,
    ) -> KVConnectorMetadata:
        meta = HIXLConnectorMetadata()

        # Loop through scheduled reqs and convert to ReqMeta.
        for req_id, (
            req,
            block_ids,
            full_block_ids,
            num_external_tokens,
        ) in self._reqs_need_recv.items():
            assert req.kv_transfer_params is not None
            # For the case where there are no remote blocks to pull
            # (block_ids is empty), we don't need to schedule
            # an async read on the worker side.
            meta.add_new_req(
                request_id=req_id,
                local_block_ids=block_ids,
                local_full_block_ids=full_block_ids,
                num_external_tokens=num_external_tokens,
                kv_transfer_params=req.kv_transfer_params,
            )

        # Clear the list once workers start the transfers
        self._reqs_need_recv.clear()
        meta.requests_to_send = self._reqs_need_send
        self._reqs_need_send = {}
        meta.reqs_in_batch = self._reqs_in_batch
        self._reqs_in_batch = set()

        return meta

    def request_finished(
        self,
        request: "Request",
        block_ids: BlockIds,
    ) -> tuple[bool, dict[str, Any] | None]:
        """
        Once a request is finished, determine whether request blocks
        should be freed now or will be sent asynchronously and freed later.
        """

        params = request.kv_transfer_params
        logger.debug(
            "HIXLConnector request_finished, request_status=%s, kv_transfer_params=%s",
            request.status,
            params,
        )

        if (
            params is None
            or not params.get("do_remote_decode")
            or request.status != RequestStatus.FINISHED_LENGTH_CAPPED
        ):
            return False, None

        num_prompt_blocks = math.ceil(len(request.prompt_token_ids) / self.block_size)
        computed_block_ids = self._get_transfer_block_ids(
            block_ids, len(request.prompt_token_ids)
        )
        computed_block_ids = self._get_swa_transfer_block_ids(computed_block_ids)
        computed_block_lens = [
            len(block_id_list) for block_id_list in computed_block_ids
        ]
        delay_free_blocks = sum(computed_block_lens) > 0
        if delay_free_blocks:
            logger.info(
                "Delaying free of %d blocks for request %s",
                sum(computed_block_lens),
                request.request_id,
            )
            self._reqs_need_send[request.request_id] = time.time()

        return delay_free_blocks, dict(
            do_remote_prefill=True,
            do_remote_decode=False,
            remote_block_ids=computed_block_ids,
            remote_engine_id=self.engine_id,
            remote_request_id=request.request_id,
            remote_host=self.side_channel_host,
            remote_port=self.side_channel_port,
            remote_pcp_size=self.pcp_size,
            remote_dcp_size=self.dcp_size,
            remote_ptp_size=self.tp_size,
            last_token_id=request.output_token_ids[-1],
            remote_multi_nodes_meta_mapping=self.multi_nodes_meta_mapping,
            num_prompt_blocks=num_prompt_blocks,
            remote_block_size=self.block_size,
        )

    def _port_offset_from_handshake_metadata(
        self,
        rank_metadata: KVConnectorHandshakeMetadata,
        metadata_key: int | tuple[int, ...],
    ) -> int:
        kv_port = self.vllm_config.kv_transfer_config.kv_port
        handshake_port = getattr(rank_metadata, "handshake_port", 0)
        if handshake_port > 0:
            return handshake_port - kv_port
        if isinstance(metadata_key, int):
            return metadata_key
        raise ValueError(
            f"HIXL handshake metadata missing handshake_port for worker key {metadata_key}"
        )

    def set_xfer_handshake_metadata_from_workers(
        self,
        metadata: Mapping[int | tuple[int, ...], KVConnectorHandshakeMetadata],
    ) -> None:
        """Build host mapping for one DP group that may span multiple nodes."""
        if not metadata:
            return

        updated_mapping: dict[str, dict[str, Any]] = {}
        kv_port = self.vllm_config.kv_transfer_config.kv_port
        for metadata_key, rank_metadata in metadata.items():
            port_offset = self._port_offset_from_handshake_metadata(
                rank_metadata, metadata_key
            )
            updated_mapping[str(port_offset)] = {
                "host": rank_metadata.local_ip,
                "engine_id": rank_metadata.engine_id,
                "handshake_port": kv_port + port_offset,
            }

        self.multi_nodes_meta_mapping.update(updated_mapping)
        logger.info(
            "HIXLConnector set_xfer_handshake_metadata: worker_count=%d, "
            "updated_entries=%d, total_mapping_entries=%d.",
            len(metadata),
            len(updated_mapping),
            len(self.multi_nodes_meta_mapping),
        )
        # The full mapping exposes every node's host/port; keep it at DEBUG.
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug(
                "HIXLConnector set_xfer_handshake_metadata: updated=%s, "
                "multi_nodes_meta_mapping=%s.",
                updated_mapping,
                self.multi_nodes_meta_mapping,
            )

    def set_xfer_handshake_metadata(
        self, metadata: Mapping[int | tuple[int, ...], KVConnectorHandshakeMetadata]
    ) -> None:
        """Legacy int-keyed entry point (port offset keys)."""
        self.set_xfer_handshake_metadata_from_workers(metadata)
