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
from types import SimpleNamespace

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


def test_single_device_kv_only_reaches_kv_runner(monkeypatch):
    args = SimpleNamespace(
        device_ids="0",
        target_host="127.0.0.1",
        skip_comm=True,
        skip_kv=False,
        platform="a2",
        output=None,
    )
    received = []
    monkeypatch.setattr(run_all_benchmarks, "parse_all_benchmark_args", lambda: args)
    monkeypatch.setattr(run_all_benchmarks, "detect_platform", lambda _platform: "a2")
    monkeypatch.setattr(run_all_benchmarks, "find_binary", lambda *_args: __file__)
    monkeypatch.setattr(
        run_all_benchmarks, "run_kv_benchmarks", lambda spec: received.append(spec) or 1
    )
    monkeypatch.setattr(run_all_benchmarks, "render_perf_report", lambda *_args: None)
    assert run_all_benchmarks.main() == 0
    assert received[0].devices == [0]


def test_communication_benchmark_still_requires_two_devices(caplog):
    args = SimpleNamespace(skip_comm=False, target_host="127.0.0.1")
    assert run_all_benchmarks._validate_args(args, [0]) == 1
    assert "at least 2 device ID(s)" in caplog.text
    assert run_all_benchmarks._validate_args(args, [0, 1]) is None


def test_kv_only_requires_at_least_one_device(caplog):
    args = SimpleNamespace(skip_comm=True, target_host="127.0.0.1")
    assert run_all_benchmarks._validate_args(args, []) == 1
    assert "at least 1 device ID(s)" in caplog.text
    assert run_all_benchmarks._validate_args(args, [0]) is None
