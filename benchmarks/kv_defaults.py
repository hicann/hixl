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

"""Default KV benchmark parameters per Ascend platform.

Exported constants and helpers used by both the KV benchmark runner
(``run_kv_benchmark.py``) and the all-in-one orchestrator
(``run_all_benchmarks.py``).

Key exports:
    KV_PROCESS_COUNT_BY_PLATFORM : dict[str, int]
        Number of KV ranks per platform — A2/A3/A5: 8.
    KV_TRANSPORT_BY_PLATFORM : dict[str, str]
        Default transport per platform — A2: rdma, A3/A5: fabric_mem.
    default_kv_transport(platform_id) -> str
        Return the recommended transport for *platform_id*.
    default_kv_device_ids(platform_id) -> str
        Return a comma-separated device-id string for *platform_id*.
    resolve_kv_devices(platform_id, user_devs, process_count=None) -> list[int]
        Resolve device IDs from platform defaults + user overrides.
"""

from __future__ import annotations

KV_PROCESS_COUNT_BY_PLATFORM: dict[str, int] = {
    "a2": 8,
    "a3": 8,
    "a5": 8,
}

KV_TRANSPORT_BY_PLATFORM: dict[str, str] = {
    "a2": "rdma",
    "a3": "fabric_mem",
    "a5": "fabric_mem",
}


def default_kv_transport(platform_id: str) -> str:
    return KV_TRANSPORT_BY_PLATFORM.get(platform_id, "rdma")


def default_kv_device_ids(platform_id: str) -> str:
    count = KV_PROCESS_COUNT_BY_PLATFORM.get(platform_id, 8)
    return ",".join(str(i) for i in range(count))


def resolve_kv_devices(
    platform_id: str,
    user_devs: list[int],
    process_count: int | None = None,
) -> list[int]:
    """Return up to process_count device IDs for the KV benchmark."""
    platform_default = KV_PROCESS_COUNT_BY_PLATFORM.get(platform_id, 8)
    want = process_count if process_count is not None else platform_default
    if len(user_devs) >= want:
        return user_devs[:want]
    # Legacy A3 auto-expand only when caller uses the platform default width.
    if platform_id == "a3" and process_count is None:
        return list(range(platform_default))
    return user_devs
