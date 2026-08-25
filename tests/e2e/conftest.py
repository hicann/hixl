#!/usr/bin/env python3
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

import logging
import os

import pytest

from .utils import get_npu_count

logging.basicConfig(
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    level=logging.INFO,
)

logging.getLogger("torch._inductor").setLevel(logging.WARNING)
logging.getLogger("torch").setLevel(logging.WARNING)

MIN_NPU_COUNT = int(os.environ.get("HIXL_E2E_MIN_NPU", "2"))


def _check_npu_count():
    count = get_npu_count()
    if count < MIN_NPU_COUNT:
        pytest.skip(
            f"Need >= {MIN_NPU_COUNT} NPUs, found {count}. "
            f"Set HIXL_E2E_MIN_NPU to override."
        )


@pytest.fixture(scope="session", autouse=True)
def check_npu_environment():
    _check_npu_count()


def pytest_collection_modifyitems(config, items):
    for item in items:
        if item.get_closest_marker("requires_fabric_mem"):
            npu_name = _get_npu_name()
            if "Ascend910" in npu_name:
                item.add_marker(
                    pytest.mark.skip(reason=f"FabricMem requires A3, got {npu_name}")
                )


def _get_npu_name():
    import subprocess

    try:
        result = subprocess.run(
            ["npu-smi", "info", "-l"], capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                if "Name" in line:
                    return line.split(":")[-1].strip()
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return "unknown"
