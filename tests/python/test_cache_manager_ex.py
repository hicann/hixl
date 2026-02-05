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

import os.path
import time
import unittest
import ctypes

from llm_datadist import *
from llm_datadist.v2.llm_types import RegisterMemStatus, Cache, Memtype, MemInfo

_INVALID_ID = 2 ** 64 - 1


class LlmCacheManagerStEx(unittest.TestCase):
    def test_check_flow_graph_mem_max_size_ok(self):
        cluster_id = 0
        llm_engine = LLMDataDist(LLMRole.PROMPT, cluster_id)
        llm_config = LLMConfig()
        llm_config.device_id = 0
        llm_config.listen_ip_info = "127.0.0.1:26000"
        llm_config.ge_options = {"ge.flowGraphMemMaxSize": "1.23"}
        init_options = llm_config.generate_options()

        has_err = False
        try:
            llm_engine.init(init_options)
        except LLMException:
            has_err = True
        self.assertEqual(has_err, True)

    def test_check_flow_graph_mem_max_size(self):
        cluster_id = 0
        llm_engine = LLMDataDist(LLMRole.PROMPT, cluster_id)
        llm_config = LLMConfig()
        llm_config.device_id = 0
        llm_config.listen_ip_info = "127.0.0.1:26000"
        llm_config.ge_options = {"ge.flowGraphMemMaxSize": "-1"}
        init_options = llm_config.generate_options()

        has_err = False
        try:
            llm_engine.init(init_options)
        except LLMException:
            has_err = True
        self.assertEqual(has_err, True)

    def test_hixl_transfer_backend(self):
        cluster_id = 0
        llm_engine = LLMDataDist(LLMRole.PROMPT, cluster_id)
        llm_config = LLMConfig()
        llm_config.device_id = 0
        llm_config.listen_ip_info = "127.0.0.1:26000"
        llm_config.transfer_backend = "hixl"
        llm_config.enable_remote_cache_accessible = False
        llm_config.ge_options = {"ge.flowGraphMemMaxSize": "-1"}
        init_options = llm_config.generate_options()

        has_err = False
        try:
            llm_engine.init(init_options)
        except LLMException:
            has_err = True
        self.assertEqual(has_err, True)


