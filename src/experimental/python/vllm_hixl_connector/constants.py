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

"""Shared constants for the HIXL vLLM connector."""

GET_META_MSG = b"get_meta_msg"
DONE_RECVING_MSG = b"done_recving_msg"


HIXL_ENGINE_BACKEND_CS = "hixl_cs"
HIXL_ENGINE_BACKEND_COMM = "comm"
HIXL_ENGINE_BACKENDS = frozenset({HIXL_ENGINE_BACKEND_CS, HIXL_ENGINE_BACKEND_COMM})
HIXL_CS_LOCAL_COMM_RES = '{"version":"1.3"}'
HIXL_OPTION_LOCAL_COMM_RES = "LocalCommRes"
HIXL_OPTION_GLOBAL_RESOURCE_CONFIG = "GlobalResourceConfig"
HIXL_PROTOCOL_DESC_FLAT = "comm_resource_config.protocol_desc"
LISTEN_PORT_OFFSET = 10000
DEFAULT_LINK_TIMEOUT_MS = 5000
DEFAULT_TRANSFER_TIMEOUT_MS = 60_000
# Extra grace past transfer_timeout_ms during which an overdue READ is still
# drained rather than abandoned. See _await_transfer.
ORPHAN_DRAIN_MS = 30_000
TERMINAL_FAIL = frozenset({"FAILED", "TIMEOUT"})


# A busy peer can otherwise keep a global executor worker forever when the
# number of peers is larger than max_workers. Yield after a small FIFO batch so
# other peers already waiting in the global executor queue can make progress.
MAX_REQUESTS_PER_PEER_HANDLER = 5

# Bounded retries (~1 s at 10 ms apart) for sending a ZMQ ACK on the P-side
# ROUTER loop. A vanished or busy peer must not stall the whole loop, or
# GET_META / DONE processing would stop for this rank.
ACK_SEND_MAX_RETRIES = 100

# Capacity of the remote-metadata caches (base addresses, strides, ports),
# keyed by remote engine_id x handshake_port. Sized to cover the largest
# expected cluster (number of P engines x ports per engine); beyond it the
# oldest entry is evicted and re-fetched by a fresh handshake on demand.
REMOTE_META_CACHE_MAX_ENTRIES = 16000
