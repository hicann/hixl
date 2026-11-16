# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
"""Validate batch invariants when changing a transfer configuration's key."""

import unittest

from llm_datadist.status import LLMException
from llm_datadist.v2.llm_types import (
    BlocksCacheKey,
    CacheKeyByIdAndIndex,
    TransferWithCacheKeyConfig,
)


class TransferCacheKeyUpdateTest(unittest.TestCase):
    def test_rejects_blocks_key_when_source_batch_is_nonzero(self):
        original = CacheKeyByIdAndIndex(1, 2)
        config = TransferWithCacheKeyConfig(original, range(0, 1), range(0, 1), 1)

        with self.assertRaisesRegex(LLMException, "src_batch_index should be 0"):
            config.cache_key = BlocksCacheKey(1)

        self.assertIs(config.cache_key, original)
        self.assertEqual(config.src_batch_index, 1)

    def test_blocks_key_is_allowed_when_source_batch_is_zero(self):
        config = TransferWithCacheKeyConfig(
            CacheKeyByIdAndIndex(1, 2), range(0, 1), range(0, 1), 0
        )
        key = BlocksCacheKey(1)
        config.cache_key = key
        self.assertIs(config.cache_key, key)

    def test_nonzero_batch_still_allows_regular_cache_key(self):
        config = TransferWithCacheKeyConfig(
            CacheKeyByIdAndIndex(1, 2), range(0, 1), range(0, 1), 1
        )
        key = CacheKeyByIdAndIndex(1, 3)
        config.cache_key = key
        self.assertIs(config.cache_key, key)

    def test_rejects_nonzero_batch_after_blocks_key(self):
        key = BlocksCacheKey(1)
        config = TransferWithCacheKeyConfig(key, range(0, 1), range(0, 1), 0)

        with self.assertRaisesRegex(LLMException, "src_batch_index should be 0"):
            config.src_batch_index = 1

        self.assertIs(config.cache_key, key)
        self.assertEqual(config.src_batch_index, 0)


if __name__ == "__main__":
    unittest.main()
