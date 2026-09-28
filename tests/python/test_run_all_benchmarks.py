# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

import importlib.util
import sys
from pathlib import Path

import pytest

MODULE_PATH = Path(__file__).parents[2] / "benchmarks" / "run_all_benchmarks.py"
SPEC = importlib.util.spec_from_file_location("run_all_benchmarks", MODULE_PATH)
run_all_benchmarks = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = run_all_benchmarks
SPEC.loader.exec_module(run_all_benchmarks)


@pytest.mark.parametrize(
    "comm_ok, kv_ok, comm_requested, kv_requested, expected",
    [
        (0, 0, True, True, 1),
        (0, 0, True, False, 1),
        (0, 0, False, True, 1),
        (0, 0, False, False, 0),
        (1, 0, True, True, 0),
        (0, 1, True, True, 0),
    ],
)
def test_benchmark_exit_code_reports_missing_success(
    comm_ok, kv_ok, comm_requested, kv_requested, expected
):
    assert (
        run_all_benchmarks.benchmark_exit_code(
            comm_ok, kv_ok, comm_requested, kv_requested
        )
        == expected
    )
