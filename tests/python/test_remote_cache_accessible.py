#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import os.path
import time
import unittest
import ctypes
from typing import Optional
import traceback

from llm_datadist import *
from llm_datadist.v2.llm_types import RegisterMemStatus, Cache, TransferWithCacheKeyConfig
from llm_datadist.utils.utils import (check_uint64, check_int64, check_int32,
                                      check_uint32, check_list_int32, check_uint16)

_INVALID_ID = 2 ** 64 - 1


class LayerSynchronizerImpl(LayerSynchronizer):
    def __init__(self, ret=True):
        self._ret = ret

    def synchronize_layer(self, layer_index: int, timeout_in_millis: Optional[int]) -> bool:
        if layer_index == 0:
            time.sleep(0.3)
        return self._ret


class LlmCacheManagerSt(unittest.TestCase):

    def setUp(self) -> None:
        print("Begin ", self._testMethodName)
        config = LlmConfig()
        config.device_id = 0
        config.enable_cache_manager = True
        config.enable_remote_cache_accessible = True
        config.mem_pool_cfg = "{\"memory_size\": 102428800}"
        config.device_id = 0
        engine_options = config.generate_options()
        self.llm_datadist = LLMDataDist(LLMRole.PROMPT, 1)
        self.llm_datadist.init(engine_options)
        self.has_exception = False

    def tearDown(self) -> None:
        print("End ", self._testMethodName)
        self.llm_datadist.finalize()

    def create_link(self):
        comm_id = self.llm_datadist.link("link", {1: 0, 2: 1}, '{"server_list":[{"device":[{}]}]}')
        self.assertEqual(comm_id, 1)
        time.sleep(0.1)
        ret = self.llm_datadist.query_register_mem_status(comm_id)
        self.assertEqual(ret, RegisterMemStatus.OK)

    def test_remote_cache_accessible_config(self):
        config = LlmConfig()
        config.device_id = 0
        config.enable_cache_manager = True
        self.assertEqual(config.enable_cache_manager, True)
        self.assertEqual(config.gen_options().get('llm.EnableRemoteCacheAccessible'), None)
        config.enable_remote_cache_accessible = False
        self.assertEqual(config.enable_remote_cache_accessible, False)
        self.assertEqual(config.gen_options().get('llm.EnableRemoteCacheAccessible'), "0")
        config.enable_remote_cache_accessible = True
        self.assertEqual(config.gen_options().get('llm.EnableRemoteCacheAccessible'), "1")

    def test_unlink(self):
        self.create_link()
        comm_id = self.llm_datadist.link("link2", {1: 0, 2: 1}, '{"server_list":[{"device":[{}]}]}')
        self.assertEqual(comm_id, 2)
        time.sleep(0.01)
        ret = self.llm_datadist.query_register_mem_status(comm_id)
        self.assertEqual(ret, RegisterMemStatus.OK)
        try:
            self.llm_datadist.unlink(1)
        except:
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_transfer_cache_with_cache_key(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(2, [2, 8], DataType.DT_INT8, Placement.DEVICE)
        kv_cache = cache_mgr.allocate_blocks_cache(cache_desc)
        cache_key = BlocksCacheKey(cluster_id=2, model_id=0)
        print(cache_key)
        dst_kv_cache = cache_mgr.allocate_blocks_cache(cache_desc, cache_key)

        transfer_config_1 = TransferWithCacheKeyConfig(BlocksCacheKey(cluster_id=2, model_id=0), range(0, 1),
                                                       range(0, 1))
        print(transfer_config_1)
        cache_task = cache_mgr.transfer_cache_async(kv_cache, LayerSynchronizerImpl(True), [transfer_config_1])
        ret = cache_task.synchronize()
        rets = cache_task.get_results()
        self.assertEqual(ret, LLMStatusCode.LLM_SUCCESS)

    def test_transfer_cache_with_cache_id(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(2, [2, 8], DataType.DT_INT8, Placement.DEVICE)
        kv_cache = cache_mgr.allocate_blocks_cache(cache_desc)
        dst_kv_cache = cache_mgr.allocate_blocks_cache(cache_desc)

        transfer_config_1 = TransferWithCacheKeyConfig(
            CacheKeyByIdAndIndex(cluster_id=2, cache_id=dst_kv_cache.cache_id), range(0, 1), range(0, 1))
        transfer_config_1.cache_key = CacheKeyByIdAndIndex(cluster_id=2, cache_id=dst_kv_cache.cache_id)
        transfer_config_1.src_layer_range = range(0, 1)
        transfer_config_1.dst_layer_range = range(0, 1)
        transfer_config_1.src_batch_index = 0
        cache_task = cache_mgr.transfer_cache_async(kv_cache, LayerSynchronizerImpl(True), [transfer_config_1])
        ret = cache_task.synchronize()
        rets = cache_task.get_results()
        self.assertEqual(ret, LLMStatusCode.LLM_SUCCESS)

    def test_register_host_remote_accessible(self):
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.HOST)
        cache = cache_mgr.register_cache(cache_desc, [1], remote_accessible=True)
        self.assertEqual(cache.cache_id, 1)

    def test_register_after_link(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache = cache_mgr.register_cache(cache_desc, [1])
        self.assertEqual(cache.cache_id, 1)

    def test_push_cache(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(2, [2, 8], DataType.DT_INT8, Placement.DEVICE)
        kv_cache = cache_mgr.allocate_cache(cache_desc)
        dst_kv_cache = cache_mgr.allocate_cache(cache_desc)
        has_err = False
        try:
            cache_mgr.push_cache(CacheKeyByIdAndIndex(cluster_id=2, cache_id=dst_kv_cache.cache_id), kv_cache,
                                 0, range(0, 1), range(0, 1), -1)
        except:
            has_err = True
        self.assertEqual(has_err, False)
        try:
            # invalid tensor_num_per_layer
            cache_mgr.push_cache(CacheKeyByIdAndIndex(cluster_id=2, cache_id=dst_kv_cache.cache_id), kv_cache,
                                 0, range(0, 1), range(0, 1), -1, -1)
        except:
            has_err = True
        self.assertEqual(has_err, True)

    def test_push_blocks(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(2, [2, 8], DataType.DT_INT8, Placement.DEVICE)
        kv_cache = cache_mgr.allocate_blocks_cache(cache_desc)
        dst_kv_cache = cache_mgr.allocate_blocks_cache(cache_desc, BlocksCacheKey(cluster_id=2))
        has_err = False
        try:
            cache_mgr.push_blocks(BlocksCacheKey(cluster_id=2), kv_cache,
                                  [0, 1], [0, 1], src_layer_range=range(0, 1), dst_layer_range=range(0, 1))
        except:
            has_err = True
        self.assertEqual(has_err, False)

    def test_pull_cache_with_correct_key(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(2, [2, 8], DataType.DT_INT8, Placement.DEVICE)
        kv_cache = cache_mgr.allocate_cache(cache_desc)
        has_err = False
        try:
            cache_mgr.pull_cache(CacheKeyByIdAndIndex(cluster_id=2, cache_id=kv_cache.cache_id), kv_cache,
                                 0, -1)
        except Exception as e:
            has_err = True
            print(e)
        self.assertEqual(has_err, False)