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

import unittest
import time
from llm_datadist import *

class LlmLocalCommResSt(unittest.TestCase):

    def setUp(self) -> None:
        print(f"Begin {self.__class__.__name__}.{self._testMethodName}")
        config = LlmConfig()
        config.device_id = 0
        config.rdma_service_level = 100
        config.rdma_traffic_class = 100
        config.listen_ip_info = "127.0.0.1:26008"
        config.local_comm_res = '''
        {
            "server_count": "1",
            "server_list": [{
                "device": [{
                "device_id": "0",
                "device_ip": "1.1.1.1"
                }],
                "server_id": "127.0.0.1"
            }],
            "status": "completed",
            "version": "1.0"
        }
        '''
        engine_options = config.generate_options()
        self.llm_datadist = LLMDataDist(LLMRole.PROMPT, 1)
        self.llm_datadist.init(engine_options)
        time.sleep(1) # wait listen
        self.has_exception = False

    def tearDown(self) -> None:
        print(f"End {self.__class__.__name__}.{self._testMethodName}")
        self.llm_datadist.finalize()

    def create_link_cluster(self):
        cluster = LLMClusterInfo()
        cluster.remote_cluster_id = 1
        cluster.append_local_ip_info("127.0.0.1", 26008)
        cluster.append_remote_ip_info("127.0.0.1", 26008)
        ret, rets = self.llm_datadist.link_clusters([cluster], 5000)
        self.assertEqual(ret, LLMStatusCode.LLM_SUCCESS)

    def test_unlink_cluster(self):
        self.create_link_cluster()
        cluster = LLMClusterInfo()
        cluster.remote_cluster_id = 1
        cluster.append_local_ip_info("127.0.0.1", 26008)
        cluster.append_remote_ip_info("127.0.0.1", 26008)
        ret, rets = self.llm_datadist.unlink_clusters([cluster], 5000)
        self.assertEqual(ret, LLMStatusCode.LLM_SUCCESS)

    def test_remap_registered_memory(self):
        self.create_link_cluster()
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

    def test_local_comm_res_switch_role(self):
        try:
            self.llm_datadist.switch_role(LLMRole.DECODER)
            options = { 'llm.listenIpInfo': '127.0.0.1:26008'}
            self.llm_datadist.switch_role(LLMRole.PROMPT, options)
            options = { 'llm.listenIpInfo': '127.0.0.1:26009'}
            self.llm_datadist.switch_role(LLMRole.PROMPT, options)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_local_comm_res_register_cache(self):
        cache_mgr = self.llm_datadist.cache_manager
        cache_desc = CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE)
        cache = cache_mgr.register_cache(cache_desc, [1])
        print(cache.cache_desc)
        print(cache.tensor_addrs)
        self.assertEqual(cache.cache_id, 1)

        try:
            cache_mgr.unregister_cache(1)
        except Exception as e:
            print(f"{type(e).__name__} - {str(e)}")
            import traceback
            print(traceback.format_exc())
            self.has_exception = True
        self.assertEqual(self.has_exception, False)
