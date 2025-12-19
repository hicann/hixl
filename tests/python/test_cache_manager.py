#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import os.path
import time
import unittest
import ctypes

from llm_datadist import *
from llm_datadist.v2.llm_types import RegisterMemStatus, Cache, Memtype, MemInfo

_INVALID_ID = 2 ** 64 - 1


class LlmCacheManagerSt(unittest.TestCase):

    def setUp(self) -> None:
        print("Begin ", self._testMethodName)
        config = LlmConfig()
        config.device_id = 0
        config.enable_cache_manager = True
        config.mem_pool_cfg = "{\"memory_size\": 102428800}"
        config.sync_kv_timeout = "3000"
        config.rdma_service_level = 100
        config.rdma_traffic_class = 100
        engine_options = config.generate_options()
        self.llm_datadist = LLMDataDist(LLMRole.PROMPT, 3)
        self.llm_datadist.init(engine_options)
        self.has_exception = False

    def tearDown(self) -> None:
        print("End ", self._testMethodName)
        self.llm_datadist.finalize()

    def create_link(self):
        comm_id = self.llm_datadist.link("link", {3: 0, 2: 1}, '{"server_list":[{"device":[{}]}]}')
        self.assertEqual(comm_id, 1)
        time.sleep(0.1)
        ret = self.llm_datadist.query_register_mem_status(comm_id)
        self.assertEqual(ret, RegisterMemStatus.OK)

    def test_register_cache(self):
        cache_mgr = self.llm_datadist.cache_manager
        with self.assertRaises(ValueError):
            cache_desc = CacheDesc(-1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        with self.assertRaises(ValueError):
            cache_desc = CacheDesc(-1, [2, 4], DataType.DT_INT8, Placement.DEVICE,
                                   seq_len_dim_index=ctypes.c_uint64(2 ** 64 - 1).value)
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache = cache_mgr.register_cache(cache_desc, [1])
        print(cache.cache_desc)
        print(cache.tensor_addrs)
        self.assertEqual(cache.cache_id, 1)

        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        blocks_cache_key = BlocksCacheKey(1, 100)
        blocks_cache = cache_mgr.register_blocks_cache(cache_desc, [1], blocks_cache_key)
        self.assertEqual(blocks_cache.cache_id, 2)

    def test_copy_blocks_cache(self):
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        blocks_cache_key = BlocksCacheKey(2, 0)
        try:
            blocks_cache = cache_mgr.allocate_blocks_cache(cache_desc, blocks_cache_key)
            cache_mgr.copy_blocks(blocks_cache, {0: [1]})
            cache_mgr.deallocate_blocks_cache(blocks_cache)
        except:
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_copy_cache(self):
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        try:
            src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
            dst_cache = cache_mgr.allocate_cache(cache_desc)
            cache_mgr.copy_cache(dst_cache, src_cache)
            cache_mgr.deallocate_cache(dst_cache)
            cache_mgr.deallocate_cache(src_cache)
            cache_mgr.remove_cache_key(CacheKey(2, 0, 0))
            cache_mgr.remove_cache_key(CacheKey(2, 1, 0))
        except:
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_remap_registered_memory(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, 1234, 1)
        mem_infos = [mem_info]
        print(f"mem_info={mem_info}")
        try:
            cache_mgr.remap_registered_memory(mem_info)
            cache_mgr.remap_registered_memory(mem_infos)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)
        # invalid mem_infos
        try:
            cache_mgr.remap_registered_memory("some val")
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, True)
        # invalid mem_type
        mem_info = MemInfo(Memtype.MEM_TYPE_HOST, 1234, 1)
        try:
            cache_mgr.remap_registered_memory(mem_info)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, True)
        # invalid addr
        mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, 0, 1)
        try:
            cache_mgr.remap_registered_memory(mem_info)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, True)
        # invalid size
        mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, 1234, -1)
        try:
            cache_mgr.remap_registered_memory(mem_info)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, True)

    def test_pull_blocks_2_blocks(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        src_blocks_cache_key = BlocksCacheKey(2, 0)
        try:
            src_blocks_cache = cache_mgr.allocate_blocks_cache(cache_desc, src_blocks_cache_key)
            dst_blocks_cache = cache_mgr.allocate_blocks_cache(cache_desc)
            cache_mgr.pull_blocks(src_blocks_cache_key, dst_blocks_cache, [0], [0])
            cache_mgr.deallocate_blocks_cache(dst_blocks_cache)
            cache_mgr.deallocate_blocks_cache(src_blocks_cache)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_pull_cache_2_blocks(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        try:
            src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
            dst_blocks_cache = cache_mgr.allocate_blocks_cache(cache_desc)
            cache_mgr.pull_blocks(cache_keys[0], dst_blocks_cache, [], [0])
            cache_mgr.deallocate_blocks_cache(dst_blocks_cache)
            cache_mgr.deallocate_cache(src_cache)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_pull_cache_2_cache(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        try:
            src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
            dst_cache = cache_mgr.allocate_cache(cache_desc)
            with self.assertRaises(LLMException) as ex:
                cache_mgr.pull_cache(cache_keys[0], dst_cache, 0, 4, src_layer_range=range(1), dst_layer_range=range(1))
            self.assertEqual(ex.exception.status_code, LLMStatusCode.LLM_PARAM_INVALID)
            cache_mgr.pull_cache(cache_keys[0], dst_cache, 0, 4)
            cache_mgr.deallocate_cache(dst_cache)
            cache_mgr.deallocate_cache(src_cache)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    @staticmethod
    def _allocate_npu_cache(kv_cache_manager, block_size, num_block, num_tensors):
        npu_cache_desc = CacheDesc(num_tensors=num_tensors, shape=[num_block, block_size],
                                   data_type=DataType.DT_FLOAT16)
        npu_cache_key = BlocksCacheKey(0, 0)
        cache = kv_cache_manager.allocate_blocks_cache(npu_cache_desc, npu_cache_key)
        return cache, npu_cache_key

    @staticmethod
    def _allocate_cpu_cache(kv_cache_manager, block_size, num_block, num_tensors):
        # DT没有友好的方式创建cpu tensor，用npu接口模拟
        cpu_cache_desc = CacheDesc(num_tensors=num_tensors, shape=[num_block, block_size],
                                   data_type=DataType.DT_FLOAT16)
        cpu_cache_key = BlocksCacheKey(1, 1)
        cache = kv_cache_manager.allocate_blocks_cache(cpu_cache_desc, cpu_cache_key)
        cpu_cache_desc = CacheDesc(num_tensors=num_tensors, shape=[num_block, block_size],
                                   data_type=DataType.DT_FLOAT16, placement=Placement.HOST)
        return Cache.create_cpu_cache(cpu_cache_desc, cache.tensor_addrs), cache

    def test_swap_blocks(self):
        cache_manager = self.llm_datadist.cache_manager
        # allocate npu cache
        npu_cache, npu_cache_key = self._allocate_npu_cache(cache_manager, 64 * 1024, 10, 10)
        cpu_cache, tmp_cache = self._allocate_cpu_cache(cache_manager, 64 * 1024, 20, 10)
        src_to_dst = {3: 4, 0: 0, 1: 1, 2: 2, 5: 6, 6: 7, 7: 8, 9: 9}
        try:
            cache_manager.swap_blocks(npu_cache, cpu_cache, src_to_dst)
            cache_manager.swap_blocks(cpu_cache, npu_cache, src_to_dst)
            cache_manager.deallocate_blocks_cache(npu_cache)
            cache_manager.deallocate_blocks_cache(cpu_cache)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_switch_role(self):
        try:
            self.llm_datadist.switch_role(LLMRole.DECODER)
        except:
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_cache_key(self):
        with self.assertRaisesRegex(LLMException, "cluster_id is required"):
            CacheKey(req_id=1, model_id=1, prefix_id=1)

        with self.assertRaisesRegex(LLMException, "req_id is required"):
            CacheKey(cluster_id=1, model_id=1, prefix_id=1)

        with self.assertRaisesRegex(LLMException, "Unsupported"):
            CacheKey(cluster_id=1, req_id=1, prefix_id1=1)

        cache_key = CacheKey(1, 1, 1, 1)
        self.assertEqual(cache_key.cluster_id, 1)
        self.assertEqual(cache_key.req_id, 1)
        self.assertEqual(cache_key.model_id, 1)
        self.assertEqual(cache_key.prefix_id, 1)

        cache_key = CacheKey(cluster_id=1, req_id=1, model_id=1, prefix_id=1)
        self.assertEqual(cache_key.cluster_id, 1)
        self.assertEqual(cache_key.req_id, 1)
        self.assertEqual(cache_key.model_id, 1)
        self.assertEqual(cache_key.prefix_id, 1)

        with self.assertRaises(ValueError):
            cache_key = CacheKey(cluster_id=-1, req_id=1, model_id=1, prefix_id=1)

        cache_key = CacheKey(prompt_cluster_id=1, req_id=1, model_id=1, prefix_id=1)
        self.assertEqual(cache_key.cluster_id, 1)
        self.assertEqual(cache_key.req_id, 1)
        self.assertEqual(cache_key.model_id, 1)
        self.assertEqual(cache_key.prefix_id, 1)

        with self.assertRaisesRegex(TypeError, "only support"):
            BlocksCacheKey("aa")

        with self.assertRaisesRegex(LLMException, "cluster_id is required"):
            BlocksCacheKey(model_id=1)

        cache_key = BlocksCacheKey(1, 1)
        self.assertEqual(cache_key.cluster_id, 1)
        self.assertEqual(cache_key.model_id, 1)

        cache_key = BlocksCacheKey(prompt_cluster_id=1, model_id=1)
        self.assertEqual(cache_key.cluster_id, 1)
        self.assertEqual(cache_key.model_id, 1)

    # keep last
    def test_unlink_suc(self):
        self.create_link()
        try:
            self.llm_datadist.unlink(1)
        except:
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_transfer_cache(self):
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(8, [2, 8], DataType.DT_INT8, Placement.DEVICE)
        kv_cache = cache_mgr.allocate_cache(cache_desc)

        dst_addrs_1 = [10000000, 20000000, 30000000, 40000000, 50000000, 60000000]
        transfer_config_1 = TransferConfig(1, dst_addrs_1, range(0, 3))
        dst_addrs_2 = [10000000, 20000000, 30000000, 40000000]
        transfer_config_2 = TransferConfig(2, dst_addrs_2, range(2, 4))
        transfer_configs = [transfer_config_1, transfer_config_2]
        with self.assertRaises(TypeError):
            _ = cache_mgr.transfer_cache_async(None, None, transfer_configs)
        with self.assertRaises(TypeError):
            _ = cache_mgr.transfer_cache_async("cache", None, transfer_configs)
        with self.assertRaises(TypeError):
            _ = cache_mgr.transfer_cache_async(kv_cache, None, transfer_configs)
        cache_mgr.deallocate_cache(kv_cache)

    def test_pull_cache_2_cache_with_default_range(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        try:
            src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
            dst_cache = cache_mgr.allocate_cache(cache_desc)
            cache_mgr.pull_cache(cache_keys[0], dst_cache, 0, 4)
            cache_mgr.deallocate_cache(dst_cache)
            cache_mgr.deallocate_cache(src_cache)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_pull_cache_with_tensor_number(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(10, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]

        src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
        dst_cache = cache_mgr.allocate_cache(cache_desc)

        cache_mgr.pull_cache(cache_keys[0], dst_cache, 0, 4, src_layer_range=range(0,1), dst_layer_range=range(0,1),
                             tensor_num_per_layer=2)
        
        with self.assertRaises(ValueError):
            cache_mgr.pull_cache(cache_keys[0], dst_cache, 0, 4, src_layer_range=range(0,1), dst_layer_range=range(0,1),
                                 tensor_num_per_layer=-1)

        with self.assertRaises(LLMException):
            cache_mgr.pull_cache(cache_keys[0], dst_cache, 0, 4, src_layer_range=range(0,1), dst_layer_range=range(0,1),
                                 tensor_num_per_layer=0)

        with self.assertRaises(TypeError):
            cache_mgr.pull_cache(cache_keys[0], dst_cache, 0, 4, src_layer_range=range(0,1), dst_layer_range=range(0,1),
                                 tensor_num_per_layer=1.0)

        with self.assertRaises(TypeError):
            cache_mgr.pull_cache(cache_keys[0], dst_cache, 0, 4, src_layer_range=range(0,1), dst_layer_range=range(0,1),
                                 tensor_num_per_layer='x')

        cache_mgr.deallocate_cache(dst_cache)
        cache_mgr.deallocate_cache(src_cache)

    def test_pull_block_with_tensor_number(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(10, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
        dst_blocks_cache = cache_mgr.allocate_blocks_cache(cache_desc)

        cache_mgr.pull_blocks(cache_keys[0], dst_blocks_cache, [], [0],
                              src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=2)

        with self.assertRaises(ValueError):
            cache_mgr.pull_blocks(cache_keys[0], dst_blocks_cache, [], [0],
                                    src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=-1)

        with self.assertRaises(LLMException):
            cache_mgr.pull_blocks(cache_keys[0], dst_blocks_cache, [], [0],
                                  src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=0)

        with self.assertRaises(TypeError):
            cache_mgr.pull_blocks(cache_keys[0], dst_blocks_cache, [], [0],
                                  src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=1.0)

        with self.assertRaises(TypeError):
            cache_mgr.pull_blocks(cache_keys[0], dst_blocks_cache, [], [0],
                                  src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer='x')

        cache_mgr.deallocate_blocks_cache(dst_blocks_cache)
        cache_mgr.deallocate_cache(src_cache)

class LlmCacheManagerDecoderSt(unittest.TestCase):

    def setUp(self) -> None:
        print("Begin ", self._testMethodName)
        config = LlmConfig()
        config.device_id = 0
        config.enable_cache_manager = True
        config.mem_pool_cfg = "{\"memory_size\": 102428800}"
        config.sync_kv_timeout = "3000"
        config.rdma_service_level = 100
        config.rdma_traffic_class = 100
        config._enable_remote_cache_accessible = True
        engine_options = config.generate_options()
        self.llm_datadist = LLMDataDist(LLMRole.DECODER, 3)
        self.llm_datadist.init(engine_options)
        self.has_exception = False

    def tearDown(self) -> None:
        self.has_exception = False
        print("End ", self._testMethodName)
        self.llm_datadist.finalize()

    def create_link(self):
        comm_id = self.llm_datadist.link("link", {3: 0, 2: 1}, '{"server_list":[{"device":[{}]}]}')
        self.assertEqual(comm_id, 1)
        time.sleep(0.1)
        ret = self.llm_datadist.query_register_mem_status(comm_id)
        self.assertEqual(ret, RegisterMemStatus.OK)

    def test_push_cache_with_default_range(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        cache_key_by_idx = CacheKeyByIdAndIndex(2, 0, 0)
        try:
            src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
            dst_cache = cache_mgr.allocate_cache(cache_desc)
            cache_mgr.push_cache(cache_key_by_idx, dst_cache)
            cache_mgr.deallocate_cache(dst_cache)
            cache_mgr.deallocate_cache(src_cache)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_push_block_with_default_range(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        dst_cache_key = BlocksCacheKey(2, 0)
        try:
            src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
            dst_blocks_cache = cache_mgr.allocate_blocks_cache(cache_desc, dst_cache_key)
            cache_mgr.push_blocks(dst_cache_key, dst_blocks_cache, [0], [0])
            cache_mgr.deallocate_blocks_cache(dst_blocks_cache)
            cache_mgr.deallocate_cache(src_cache)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)
        
    def test_push_cache_with_tensor_number(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(10, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        cache_key_by_idx = CacheKeyByIdAndIndex(2, 0, 0)
        src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
        dst_cache = cache_mgr.allocate_cache(cache_desc)
        cache_mgr.push_cache(cache_key_by_idx, dst_cache, src_batch_index=0,
                             src_layer_range=range(0,1), dst_layer_range=range(0,1),
                             size=-1, tensor_num_per_layer=2)

        with self.assertRaises(ValueError):
            cache_mgr.push_cache(cache_key_by_idx, dst_cache, src_batch_index=0,
                             src_layer_range=range(0,1), dst_layer_range=range(0,1),
                             size=-1, tensor_num_per_layer=-1)

        with self.assertRaises(LLMException):
            cache_mgr.push_cache(cache_key_by_idx, dst_cache, src_batch_index=0,
                             src_layer_range=range(0,1), dst_layer_range=range(0,1),
                             size=-1, tensor_num_per_layer=0)

        with self.assertRaises(LLMException):
            cache_mgr.push_cache(cache_key_by_idx, dst_cache, src_batch_index=0,
                             src_layer_range=range(0,1), dst_layer_range=range(0,1),
                             size=-1, tensor_num_per_layer=3)

        with self.assertRaises(TypeError):
            cache_mgr.push_cache(cache_key_by_idx, dst_cache, src_batch_index=0,
                             src_layer_range=range(0,1), dst_layer_range=range(0,1),
                             size=-1, tensor_num_per_layer=2.0)

        with self.assertRaises(TypeError):
            cache_mgr.push_cache(cache_key_by_idx, dst_cache, src_batch_index=0,
                             src_layer_range=range(0,1), dst_layer_range=range(0,1),
                             size=-1, tensor_num_per_layer='x')

        cache_mgr.deallocate_cache(dst_cache)
        cache_mgr.deallocate_cache(src_cache)


    def test_push_block_with_tensor_number(self):
        self.create_link()
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(10, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache_keys = [CacheKey(2, 0, 0), CacheKey(2, 1, 0)]
        dst_cache_key = BlocksCacheKey(2, 0)

        src_cache = cache_mgr.allocate_cache(cache_desc, cache_keys)
        dst_blocks_cache = cache_mgr.allocate_blocks_cache(cache_desc, dst_cache_key)
        cache_mgr.push_blocks(dst_cache_key, dst_blocks_cache, [0], [0],
                              src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=2)

        with self.assertRaises(ValueError):
            cache_mgr.push_blocks(dst_cache_key, dst_blocks_cache, [0], [0],
                              src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=-1)

        with self.assertRaises(LLMException):
            cache_mgr.push_blocks(dst_cache_key, dst_blocks_cache, [0], [0],
                              src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=0)
        
        with self.assertRaises(LLMException):
            cache_mgr.push_blocks(dst_cache_key, dst_blocks_cache, [0], [0],
                              src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=3)

        with self.assertRaises(TypeError):
            cache_mgr.push_blocks(dst_cache_key, dst_blocks_cache, [0], [0],
                              src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer=2.0)

        with self.assertRaises(TypeError):
            cache_mgr.push_blocks(dst_cache_key, dst_blocks_cache, [0], [0],
                              src_layer_range=range(0,1), dst_layer_range=range(0,1), tensor_num_per_layer='x')

        cache_mgr.deallocate_blocks_cache(dst_blocks_cache)
        cache_mgr.deallocate_cache(src_cache)