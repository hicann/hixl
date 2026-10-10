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

"""Wire and per-step data structures of the HIXL connector."""

from collections import OrderedDict
from dataclasses import dataclass
from typing import Any, TypedDict
import msgspec
from vllm.distributed.kv_transfer.kv_connector.utils import BlockIds
from vllm.distributed.kv_transfer.kv_connector.v1.base import KVConnectorMetadata

from .constants import REMOTE_META_CACHE_MAX_ENTRIES


class RemotePortInfo(TypedDict):
    num: int
    host: str


class HIXLAgentMetadata(msgspec.Struct, omit_defaults=True, dict=True):
    engine_id: str
    # HIXL listen port written by the local transport. The field name is part
    # of the wire protocol (msgspec encodes field names); peers must read this
    # value, not recompute it.
    listen_port: int
    kv_group2layeridx: dict[int, tuple[dict[str, Any], list[int]]]
    block_size: int
    kv_caches_base_addr: list[list[int]]
    block_size_scale: list[list[int]]
    num_blocks: int
    block_lens: list[list[int]]
    block_strides: list[list[int]]
    local_ip: str = ""
    handshake_port: int = 0


@dataclass
class ReqMeta:
    local_block_ids: BlockIds
    num_external_tokens: int
    num_computed_tokens: int
    remote_block_ids: BlockIds

    remote_host: str
    remote_port: int
    remote_engine_id: str
    remote_request_id: str
    remote_pcp_size: int
    remote_dcp_size: int
    remote_ptp_size: int | None
    remote_multi_nodes_meta_mapping: dict[str, dict[str, Any]]
    num_prompt_blocks: int
    remote_block_size: int
    local_full_block_ids: BlockIds = tuple()


@dataclass(frozen=True)
class GroupPull:
    group_id: int
    remote_tp_offset: int
    num_group_pulls: int
    prefill_pp_rank: int = 0
    is_group_transfer_end: bool = False


@dataclass(frozen=True)
class GroupTransferInfo:
    tokens_per_block: int
    blocks_per_window: int
    is_state_group: bool


@dataclass
class SizedDict(OrderedDict):
    def __init__(self, max_size=REMOTE_META_CACHE_MAX_ENTRIES, *args, **kwargs):
        self.max_size = max_size
        super().__init__(*args, **kwargs)

    def __setitem__(self, key, value):
        super().__setitem__(key, value)
        if len(self) > self.max_size:
            self.popitem(last=False)

    def get_or_create(self, key):
        """Return the entry for ``key``, inserting an empty dict if absent.

        ``__getitem__`` keeps plain dict semantics (a missing key raises
        KeyError) so an unexpected read cannot silently create an entry
        and consume eviction capacity; callers that intend to create an
        entry use this method explicitly.
        """
        if key not in self:
            self[key] = {}
        return self[key]


class HIXLConnectorMetadata(KVConnectorMetadata):
    def __init__(self):
        self.requests: dict[str, ReqMeta] = {}
        self.requests_to_send: dict[str, float] = {}
        self.reqs_in_batch: set[str] = set()

    def add_new_req(
        self,
        request_id: str,
        local_block_ids: BlockIds,
        num_external_tokens: int,
        kv_transfer_params: dict[str, Any],
        local_full_block_ids: BlockIds | None = None,
    ):
        self.requests[request_id] = ReqMeta(
            local_block_ids=local_block_ids,
            num_external_tokens=num_external_tokens,
            num_computed_tokens=kv_transfer_params.get("num_computed_tokens", 0),
            remote_block_ids=kv_transfer_params["remote_block_ids"],
            remote_engine_id=kv_transfer_params["remote_engine_id"],
            remote_request_id=kv_transfer_params["remote_request_id"],
            remote_host=kv_transfer_params["remote_host"],
            remote_port=kv_transfer_params["remote_port"],
            remote_pcp_size=kv_transfer_params.get("remote_pcp_size", 1),
            remote_dcp_size=kv_transfer_params.get("remote_dcp_size", 1),
            remote_ptp_size=kv_transfer_params.get("remote_ptp_size"),
            remote_multi_nodes_meta_mapping=kv_transfer_params.get(
                "remote_multi_nodes_meta_mapping", {}
            ),
            num_prompt_blocks=kv_transfer_params.get("num_prompt_blocks", 0),
            remote_block_size=kv_transfer_params.get("remote_block_size", 0),
            local_full_block_ids=local_full_block_ids or tuple(),
        )
