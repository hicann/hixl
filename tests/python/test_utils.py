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
from llm_datadist.utils.utils import (check_uint64, check_int64,check_int32,
                                      check_uint32, check_list_int32, check_uint16, check_uint8,
                                      check_dict, check_isinstance)
from llm_datadist.v2.llm_types import CacheDesc, DataType, Placement
from llm_datadist.v2.llm_utils import pack_cache_desc


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
