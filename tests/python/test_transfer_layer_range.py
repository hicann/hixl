#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import unittest
from types import SimpleNamespace

from llm_datadist.status import LLMStatusCode
from llm_datadist.v2.llm_types import (
    CacheDesc,
    CacheKeyByIdAndIndex,
    CacheTask,
    DataType,
    LayerSynchronizer,
    Placement,
    TransferWithCacheKeyConfig,
)
from llm_datadist.v2.llm_utils import (
    TransferAsyncThread,
    TransferCacheJob,
    TransferCacheParameters,
)


class RecordingLayerSynchronizer(LayerSynchronizer):
    def __init__(self):
        self.calls = []

    def synchronize_layer(self, layer_index: int, timeout_in_millis):
        self.calls.append(layer_index)
        return True


class TransferLayerRangeUt(unittest.TestCase):
    @staticmethod
    def _create_job(src_layer_range, dst_layer_range, transfer_cache_func):
        transfer_config = TransferWithCacheKeyConfig(
            CacheKeyByIdAndIndex(cluster_id=1, cache_id=1),
            src_layer_range,
            dst_layer_range,
        )
        src_cache = SimpleNamespace(
            cache_id=1,
            cache_desc=CacheDesc(4, [1, 2], DataType.DT_INT8, Placement.DEVICE),
        )
        synchronizer = RecordingLayerSynchronizer()
        params = TransferCacheParameters(src_cache, [transfer_config])
        job = TransferCacheJob(params, synchronizer, transfer_cache_func)
        job.init()
        return job, synchronizer

    @staticmethod
    def _run_transfer(src_layer_range, dst_layer_range):
        transfer_calls = []

        def transfer_cache_func(_, transfer_config, __):
            transfer_calls.append((transfer_config[2], transfer_config[8]))
            return int(LLMStatusCode.LLM_SUCCESS)

        job, synchronizer = TransferLayerRangeUt._create_job(
            src_layer_range, dst_layer_range, transfer_cache_func
        )
        thread = TransferAsyncThread(job, LLMStatusCode.LLM_FAILED)
        thread.start()
        task = CacheTask(thread)
        return (
            synchronizer.calls,
            transfer_calls,
            task.synchronize(),
            task.get_results(),
        )

    def test_nonzero_source_range_uses_relative_destination_index(self):
        sync_calls, transfer_calls, status, results = self._run_transfer(
            range(1, 2), range(0, 1)
        )

        self.assertEqual(sync_calls, [1])
        self.assertEqual(transfer_calls, [(1, 0)])
        self.assertEqual(status, LLMStatusCode.LLM_SUCCESS)
        self.assertEqual(results, [LLMStatusCode.LLM_SUCCESS])

    def test_zero_based_range_remains_successful(self):
        sync_calls, transfer_calls, status, results = self._run_transfer(
            range(0, 1), range(0, 1)
        )

        self.assertEqual(sync_calls, [0])
        self.assertEqual(transfer_calls, [(0, 0)])
        self.assertEqual(status, LLMStatusCode.LLM_SUCCESS)
        self.assertEqual(results, [LLMStatusCode.LLM_SUCCESS])

    def test_transfer_thread_exception_is_reported(self):
        def transfer_cache_func(*_):
            raise RuntimeError("transfer failed")

        job, _ = self._create_job(range(0, 1), range(0, 1), transfer_cache_func)
        thread = TransferAsyncThread(job, LLMStatusCode.LLM_FAILED)
        thread.start()
        task = CacheTask(thread)

        self.assertEqual(task.synchronize(), LLMStatusCode.LLM_FAILED)
        self.assertEqual(task.get_results(), [LLMStatusCode.LLM_FAILED])


if __name__ == "__main__":
    unittest.main()
