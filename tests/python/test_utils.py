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
# fmt: off

# content of test_sample.py
import unittest
import ctypes
from unittest.mock import MagicMock, patch
from llm_datadist.utils.utils import (check_uint64, check_int64,check_int32,
                                      check_uint32, check_list_int32, check_uint16, check_uint8,
                                      check_dict, check_isinstance)
from llm_datadist.v2.llm_types import (CacheDesc, DataType, Placement, Cache, CacheKeyByIdAndIndex,
                                      TransferConfig, TransferWithCacheKeyConfig, LayerSynchronizer, CacheTask)
from llm_datadist.v2.llm_utils import (pack_cache_desc, TransferCacheParameters, TransferCacheJob,
                                     TransferAsyncThread, transfer_cache_async)
from llm_datadist.status import LLMException, LLMStatusCode


class TensorUt(unittest.TestCase):
    def setUp(self) -> None:
        print("Begin ", self._testMethodName)

    def tearDown(self) -> None:
        print("End ", self._testMethodName)


    def test_check_exception(self):
        with self.assertRaises(ValueError):
            _ = check_uint64("cluster", -1)
        with self.assertRaises(ValueError):
            _ = check_int64("cluster", ctypes.c_uint64(2**64 - 1).value)
        with self.assertRaises(ValueError):
            _ = check_int32("cluster", ctypes.c_uint64(2**64 - 1).value)
        with self.assertRaises(ValueError):
            _ = check_uint32("cluster", -1)
        with self.assertRaises(ValueError):
            _ = check_list_int32("cluster_ids", [0, 1, ctypes.c_uint64(2**64 - 1).value])
        with self.assertRaises(ValueError):
            _ = check_uint16("cluster", -1)
        with self.assertRaises(ValueError):
            _ = check_uint8("cluster", -1)

    def test_check_bool_rejected(self):
        with self.assertRaises(TypeError):
            _ = check_uint64("cluster", True)
        with self.assertRaises(TypeError):
            _ = check_int64("cluster", True)
        with self.assertRaises(TypeError):
            _ = check_int32("cluster", True)
        with self.assertRaises(TypeError):
            _ = check_uint32("cluster", True)
        with self.assertRaises(TypeError):
            _ = check_uint32("cluster", False)
        with self.assertRaises(TypeError):
            _ = check_uint16("cluster", True)
        with self.assertRaises(TypeError):
            _ = check_uint8("cluster", True)
        with self.assertRaises(TypeError):
            _ = check_isinstance("cluster", True, [int], allow_none=False)
        with self.assertRaises(TypeError):
            _ = check_isinstance("device_id", [0, True], [list, tuple], int)
        with self.assertRaises(TypeError):
            _ = check_dict("cluster_rank_info", {0: True}, int, int)
        with self.assertRaises(TypeError):
            _ = check_dict("cluster_rank_info", {True: 0}, int, int)
        self.assertIs(check_isinstance("enable_switch_role", True, [bool]), True)
        self.assertIs(check_isinstance("cluster", 1, [int]), 1)

    def test_pack_cache_desc_preserves_batch_dim_index(self):
        cache_desc = CacheDesc(1, [2, 3], DataType.DT_INT8, Placement.DEVICE, batch_dim_index=1)
        self.assertEqual(pack_cache_desc(cache_desc), (1, DataType.DT_INT8.value, -1, 1, [2, 3],
                                                        Placement.DEVICE.value, False))


class AsyncCacheBatchBoundsUt(unittest.TestCase):
    def setUp(self):
        desc = CacheDesc(4, [2, 4], DataType.DT_INT8, Placement.HOST)
        self.cache = Cache(0, desc, [1, 2, 3, 4], None, True, False)
        self.synchronizer = MagicMock(spec=LayerSynchronizer)
        self.synchronizer.synchronize_layer.return_value = True
        self.transfer = MagicMock(return_value=LLMStatusCode.LLM_SUCCESS.value)

    @staticmethod
    def config(index, remote=False):
        if remote:
            key = CacheKeyByIdAndIndex(1, 0, 0)
            return TransferWithCacheKeyConfig(key, range(0, 2), range(0, 2), index)
        return TransferConfig(1, [10, 11, 12, 13], src_batch_index=index)

    def test_invalid_cache_batch_rejected_before_start(self):
        for remote in (False, True):
            for index in (2, 3, 2**32 - 1):
                with self.subTest(remote=remote, index=index):
                    params = TransferCacheParameters(self.cache, [self.config(index, remote)])
                    with patch.object(TransferAsyncThread, "start") as start:
                        with self.assertRaisesRegex(LLMException, r"src_batch_index .* out of range: \[0, 2\)"):
                            transfer_cache_async(params, self.synchronizer, self.transfer,
                                                 enable_remote_cache=remote)
                        start.assert_not_called()
        self.synchronizer.synchronize_layer.assert_not_called()
        self.transfer.assert_not_called()

    def test_valid_cache_batch_boundaries_transfer(self):
        for remote in (False, True):
            for index in (0, 1):
                with self.subTest(remote=remote, index=index):
                    self.transfer.reset_mock()
                    params = TransferCacheParameters(self.cache, [self.config(index, remote)])
                    job = TransferCacheJob(params, self.synchronizer, self.transfer)
                    job.init()
                    job.transfer_layers()
                    self.assertEqual(job.get_results(), [LLMStatusCode.LLM_SUCCESS])
                    self.assertEqual(self.transfer.call_count, 2)
                    for call in self.transfer.call_args_list:
                        self.assertEqual(call.args[1][1], index)

    def test_valid_cache_batch_starts_async_task(self):
        params = TransferCacheParameters(self.cache, [self.config(1)])
        with patch.object(TransferAsyncThread, "start") as start:
            task = transfer_cache_async(params, self.synchronizer, self.transfer)
            self.assertIsInstance(task, CacheTask)
            start.assert_called_once()

    def test_blocks_retain_zero_batch_requirement(self):
        for index in (1, 2):
            with self.subTest(index=index):
                params = TransferCacheParameters(self.cache, [self.config(index)], [0], [0])
                with patch.object(TransferAsyncThread, "start") as start:
                    with self.assertRaisesRegex(LLMException, "!= 0 while src is blocks"):
                        transfer_cache_async(params, self.synchronizer, self.transfer)
                    start.assert_not_called()
        params = TransferCacheParameters(self.cache, [self.config(0)], [0], [0])
        with patch.object(TransferAsyncThread, "start") as start:
            self.assertIsInstance(transfer_cache_async(params, self.synchronizer, self.transfer), CacheTask)
            start.assert_called_once()
