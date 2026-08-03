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


if __name__ == "__main__":
    unittest.main()
