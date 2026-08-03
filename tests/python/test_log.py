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

import threading
import unittest

from llm_datadist.utils.log import _find_caller


class TestLogUtils(unittest.TestCase):
    def test_find_caller_thread_stack_top(self):
        result_holder = []

        def target():
            def inner():
                result_holder.append(_find_caller())

            inner()

        t = threading.Thread(target=target)
        t.start()
        t.join()
        self.assertEqual(len(result_holder), 1)


if __name__ == "__main__":
    unittest.main()
