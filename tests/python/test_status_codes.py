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

from llm_datadist import llm_datadist_wrapper
from llm_datadist.status import LLMStatusCode, code_2_status


class LLMStatusCodeTest(unittest.TestCase):
    def test_retained_status_codes_keep_native_mapping(self):
        retained_codes = {
            0x5010B001: LLMStatusCode.LLM_WAIT_PROCESS_TIMEOUT,
            0x5010B006: LLMStatusCode.LLM_ENGINE_FINALIZED,
            0x5010B00E: LLMStatusCode.LLM_DEVICE_OUT_OF_MEMORY,
        }
        for raw_code, expected_status in retained_codes.items():
            self.assertEqual(code_2_status(raw_code), expected_status)

    def test_removed_status_codes_are_not_exposed(self):
        removed_statuses = {
            "LLM_REQUEST_ALREADY_COMPLETED": 0x5010B004,
            "LLM_SEQ_LENOVERLIMIT": 0x5010B011,
            "LLM_NO_FREE_BLOCK": 0x5010B012,
            "LLM_BLOCKS_OUT_OF_MEMORY": 0x5010B013,
        }
        for status_name, raw_code in removed_statuses.items():
            self.assertFalse(hasattr(LLMStatusCode, status_name))
            self.assertEqual(code_2_status(raw_code), LLMStatusCode.LLM_FAILED)

        removed_bindings = {
            "kLLMRequestAlreadyCompleted",
            "kLLMSeqLenOverLimit",
            "kLLMNoFreeBlock",
            "kLLMBlocksOutOfMemory",
        }
        for binding_name in removed_bindings:
            self.assertFalse(hasattr(llm_datadist_wrapper, binding_name))


if __name__ == "__main__":
    unittest.main()
