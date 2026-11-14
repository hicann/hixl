# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
"""Execute the full custom-partition helper without importing vLLM/native."""

import importlib.util
import logging
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]


def _load_custom_partition_helper():
    # Only external vLLM entry points are boundaries; execute this entire production module.
    fake_modules = {
        "vllm": types.ModuleType("vllm"),
        "vllm.distributed": types.ModuleType("vllm.distributed"),
        "vllm.distributed.utils": types.ModuleType("vllm.distributed.utils"),
        "vllm.logger": types.ModuleType("vllm.logger"),
        "vllm.utils": types.ModuleType("vllm.utils"),
        "vllm.utils.network_utils": types.ModuleType("vllm.utils.network_utils"),
        # get_prefill_pp_indices 不依赖 numpy/zmq，用 Mock 顶掉顶层 import 与函数签名里的类型注解。
        "numpy": Mock(),
        "numpy.typing": Mock(),
        "zmq": Mock(),
    }
    fake_modules["vllm.distributed.utils"].get_pp_indices = Mock(
        side_effect=AssertionError(
            "native vLLM fallback must not be used by custom partitions"
        )
    )
    fake_modules["vllm.logger"].logger = logging.getLogger("hixl_host_partitions")
    fake_modules["vllm.utils.network_utils"].make_zmq_socket = Mock(
        side_effect=AssertionError("Host partition tests do not open network sockets")
    )
    with patch.dict(sys.modules, fake_modules):
        source = ROOT / "src/experimental/python/vllm_hixl_connector/utils.py"
        spec = importlib.util.spec_from_file_location("hixl_host_utils", source)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module.get_prefill_pp_indices


class TestNegativePpPartitions(unittest.TestCase):
    def test_negative_layer_counts_cannot_define_pp_intervals(self):
        helper = _load_custom_partition_helper()
        for partition in ["-1,5", "5,-1", "2,-1,3"]:
            with self.subTest(partition=partition):
                counts = partition.split(",")
                with self.assertRaisesRegex(ValueError, "non-negative"):
                    helper(4, 0, len(counts), partition)

    def test_non_negative_partition_intervals_cover_each_model_layer_once(self):
        helper = _load_custom_partition_helper()
        for partition in ["2,2", "0,4", "1,0,3"]:
            with self.subTest(partition=partition):
                counts = [int(item) for item in partition.split(",")]
                intervals = [
                    helper(4, rank, len(counts), partition)
                    for rank in range(len(counts))
                ]
                actual = [
                    layer for start, end in intervals for layer in range(start, end)
                ]
                self.assertEqual(actual, list(range(4)))
                self.assertEqual([end - start for start, end in intervals], counts)

    def test_existing_size_and_total_checks_still_reject_invalid_configs(self):
        helper = _load_custom_partition_helper()
        for partition, size in [("2,2", 3), ("1,2", 2)]:
            with self.subTest(partition=partition, size=size):
                with self.assertRaises(ValueError):
                    helper(4, 0, size, partition)


if __name__ == "__main__":
    unittest.main()
