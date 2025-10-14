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

# content of test_sample.py
import os.path
import time
import unittest
import ctypes
from llm_datadist.utils.utils import (check_uint64, check_int64,check_int32,
                                      check_uint32, check_list_int32, check_uint16, check_uint8)
import numpy as np


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
