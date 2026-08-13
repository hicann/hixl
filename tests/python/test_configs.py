#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import unittest

from llm_datadist.configs import LlmConfig
from llm_datadist.status import LLMException


class TestLlmConfig(unittest.TestCase):
    def setUp(self):
        self.config = LlmConfig()

    def test_gen_options_with_list_device_id(self):
        self.config.device_id = [0, 1]
        options = self.config.gen_options()
        self.assertEqual(options["ge.exec.deviceId"], "0;1")
        self.assertEqual(options["ge.session_device_id"], "0")

    def test_device_id_setter_with_list(self):
        self.config.device_id = [0, 1, 2]
        self.assertEqual(self.config.device_id, [0, 1, 2])

    def test_device_id_setter_with_tuple(self):
        self.config.device_id = (3, 4)
        self.assertEqual(self.config.device_id, (3, 4))

    def test_mem_utilization_valid_range(self):
        self.config.mem_utilization = 0.5
        self.assertEqual(self.config.mem_utilization, 0.5)
        self.config.mem_utilization = 0.0
        self.assertEqual(self.config.mem_utilization, 0.0)
        self.config.mem_utilization = 1.0
        self.assertEqual(self.config.mem_utilization, 1.0)

    def test_mem_utilization_out_of_range(self):
        with self.assertRaises(LLMException):
            self.config.mem_utilization = 1.5
        with self.assertRaises(LLMException):
            self.config.mem_utilization = -0.1

    def test_global_resource_config_setter_valid_str(self):
        json_str = '{"comm_resource_config.listen_port": 26666}'
        self.config.global_resource_config = json_str
        self.assertEqual(self.config.global_resource_config, json_str)

    def test_global_resource_config_setter_non_str_raises(self):
        with self.assertRaises(TypeError):
            self.config.global_resource_config = 123
        with self.assertRaises(TypeError):
            self.config.global_resource_config = {"key": "value"}
        with self.assertRaises(TypeError):
            self.config.global_resource_config = [1, 2]

    def test_global_resource_config_not_set_excluded_from_options(self):
        self.config.device_id = 0
        options = self.config.gen_options()
        self.assertNotIn("llm.GlobalResourceConfig", options)

    def test_global_resource_config_empty_str_in_options(self):
        self.config.device_id = 0
        self.config.global_resource_config = ""
        options = self.config.gen_options()
        self.assertEqual(options["llm.GlobalResourceConfig"], "")

    def test_global_resource_config_in_options(self):
        self.config.device_id = 0
        json_str = '{"comm_resource_config.listen_port": 26666, "comm_resource_config.max_active_channels": 128}'
        self.config.global_resource_config = json_str
        options = self.config.gen_options()
        self.assertEqual(options["llm.GlobalResourceConfig"], json_str)


if __name__ == "__main__":
    unittest.main()
