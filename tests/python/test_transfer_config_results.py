# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Keep each transfer configuration's result separate from its destination."""

import unittest

from llm_datadist.data_type import DataType
from llm_datadist.status import LLMStatusCode
from llm_datadist.v2.cache_manager import CacheManager
from llm_datadist.v2.llm_types import (
    CacheDesc,
    LayerSynchronizer,
    Placement,
    TransferConfig,
)


class ReadyLayers(LayerSynchronizer):
    def __init__(self, blocked_layer=None):
        self.blocked_layer = blocked_layer

    def synchronize_layer(self, layer_index, timeout_in_millis):
        return layer_index != self.blocked_layer


class RecordingBackend:
    """Only the native device registration/transfer boundary is substituted."""

    def __init__(self, fail_call=None):
        self.fail_call = fail_call
        self.calls = []

    def register_cache(self, descriptor, addrs, keys, remote_accessible):
        return LLMStatusCode.LLM_SUCCESS.value, (77, [addrs])

    def transfer_cache_v2(self, task_id, transfer_config, block_config):
        self.calls.append(transfer_config)
        status = (
            LLMStatusCode.LLM_FAILED
            if len(self.calls) == self.fail_call
            else LLMStatusCode.LLM_SUCCESS
        )
        return status.value


class TransferConfigResultsTest(unittest.TestCase):
    def task(self, configs, fail_call=None, blocked_layer=None):
        backend = RecordingBackend(fail_call)
        manager = CacheManager(backend, {})
        descriptor = CacheDesc(6, [1, 4], DataType.DT_FLOAT16, Placement.DEVICE)
        cache = manager.register_cache(descriptor, [10, 20, 30, 40, 50, 60])
        task = manager.transfer_cache_async(cache, ReadyLayers(blocked_layer), configs)
        return task, backend

    @staticmethod
    def config(cluster_id, layer):
        return TransferConfig(
            cluster_id, [100 + layer * 200, 200 + layer * 200], range(layer, layer + 1)
        )

    def test_same_destination_keeps_completed_config_success(self):
        task, backend = self.task([self.config(1, 0), self.config(1, 1)], fail_call=2)
        self.assertEqual(
            task.get_results(), [LLMStatusCode.LLM_SUCCESS, LLMStatusCode.LLM_FAILED]
        )
        self.assertEqual([call[2] for call in backend.calls], [0, 1])
        self.assertEqual(task.synchronize(), LLMStatusCode.LLM_FAILED)

    def test_same_destination_keeps_unstarted_config_none(self):
        task, backend = self.task([self.config(1, 0), self.config(1, 1)], fail_call=1)
        self.assertEqual(task.get_results(), [LLMStatusCode.LLM_FAILED, None])
        self.assertEqual(len(backend.calls), 1)

    def test_completed_destination_does_not_mark_later_config_success(self):
        task, backend = self.task(
            [self.config(1, 0), self.config(1, 2), self.config(2, 1)], fail_call=2
        )
        self.assertEqual(
            task.get_results(),
            [LLMStatusCode.LLM_SUCCESS, None, LLMStatusCode.LLM_FAILED],
        )
        self.assertEqual([call[4] for call in backend.calls], [1, 2])

    def test_same_layer_configurations_keep_independent_results(self):
        second = TransferConfig(1, [300, 400], range(0, 1))
        task, backend = self.task([self.config(1, 0), second], fail_call=2)
        self.assertEqual(
            task.get_results(), [LLMStatusCode.LLM_SUCCESS, LLMStatusCode.LLM_FAILED]
        )
        self.assertEqual([call[2] for call in backend.calls], [0, 0])

    def test_same_destination_success_control(self):
        task, backend = self.task([self.config(1, 0), self.config(1, 1)])
        self.assertEqual(task.get_results(), [LLMStatusCode.LLM_SUCCESS] * 2)
        self.assertEqual(task.synchronize(), LLMStatusCode.LLM_SUCCESS)
        self.assertEqual(len(backend.calls), 2)

    def test_distinct_destination_failure_control(self):
        task, backend = self.task([self.config(1, 0), self.config(2, 1)], fail_call=2)
        self.assertEqual(
            task.get_results(), [LLMStatusCode.LLM_SUCCESS, LLMStatusCode.LLM_FAILED]
        )
        self.assertEqual(len(backend.calls), 2)

    def test_layer_sync_failure_preserves_completed_config(self):
        task, backend = self.task(
            [self.config(1, 0), self.config(1, 1)], blocked_layer=1
        )
        self.assertEqual(
            task.get_results(),
            [LLMStatusCode.LLM_SUCCESS, LLMStatusCode.LLM_PARAM_INVALID],
        )
        self.assertEqual(task.synchronize(), LLMStatusCode.LLM_PARAM_INVALID)
        self.assertEqual(len(backend.calls), 1)


if __name__ == "__main__":
    unittest.main()
