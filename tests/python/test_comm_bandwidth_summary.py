# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Verify comm benchmark AVG/SUM summaries without starting NPU processes."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


def load_launcher():
    source = (
        Path(__file__).resolve().parents[2]
        / "benchmarks"
        / "comm_benchmark"
        / "scripts"
        / "run_comm_benchmark.py"
    )
    spec = importlib.util.spec_from_file_location("comm_bw_summary_launcher", source)
    module = importlib.util.module_from_spec(spec)
    sys.modules["comm_bw_summary_launcher"] = module
    spec.loader.exec_module(module)
    return module


COMM = load_launcher()
CSV_HEADER = (
    "benchmark,pattern,model,token_length,block_size,batch_size,threads,transport,direction,"
    "initiator_memory,target_memory,bandwidth_gbps,ops_per_sec,avg_latency_us,p99_us,"
    "error_count,consistency\n"
)


def write_csv(path, block_size, bandwidths):
    with path.open("w", encoding="utf-8") as csv_file:
        csv_file.write(CSV_HEADER)
        for bw in bandwidths:
            csv_file.write(
                f"hixl_comm_bench,single,,,{block_size},1,1,roce,D2rD,device,device,{bw},0,0,0,0,not_checked\n"
            )


class CommBandwidthSummaryTest(unittest.TestCase):
    def test_sum_is_sum_of_per_initiator_means(self):
        with tempfile.TemporaryDirectory() as tmp:
            csv_a = Path(tmp) / "comm_result_1.csv"
            csv_b = Path(tmp) / "comm_result_2.csv"
            write_csv(csv_a, 1048576, [10.0, 12.0])
            write_csv(csv_b, 1048576, [6.0, 6.0])
            with patch.object(COMM.log, "info") as info:
                COMM.print_average_summary({csv_a, csv_b})
            messages = [str(call.args[0]) for call in info.call_args_list if call.args]
            avg_lines = [m for m in messages if m.startswith("[AVG]")]
            sum_lines = [m for m in messages if m.startswith("[SUM]")]
            self.assertTrue(any("bandwidth=8.500 GB/s" in m for m in avg_lines))
            self.assertTrue(
                any(
                    "initiator_count=2" in m and "bandwidth=17.000 GB/s" in m
                    for m in sum_lines
                )
            )

    def test_single_csv_skips_sum(self):
        with tempfile.TemporaryDirectory() as tmp:
            csv_a = Path(tmp) / "comm_result_1.csv"
            write_csv(csv_a, 1048576, [12.0])
            with patch.object(COMM.log, "info") as info:
                COMM.print_average_summary({csv_a})
            messages = [str(call.args[0]) for call in info.call_args_list if call.args]
            self.assertTrue(any(m.startswith("[AVG]") for m in messages))
            self.assertFalse(any(m.startswith("[SUM]") for m in messages))
