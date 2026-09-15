# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Cache kind must follow the API, independently of descriptor reuse."""

from concurrent.futures import ThreadPoolExecutor
from itertools import product
from threading import Event
import unittest
from unittest.mock import Mock

from llm_datadist.status import LLMException, LLMStatusCode
from llm_datadist.v2.cache_manager import CacheManager
from llm_datadist.v2.llm_types import CacheDesc, DataType, Placement
from llm_datadist.v2.llm_utils import pack_cache_desc


BLOCK_APIS = ("allocate_blocks_cache", "register_blocks_cache")
REGULAR_APIS = ("allocate_cache", "register_cache")
API_PAIRS = tuple(product(BLOCK_APIS, REGULAR_APIS))
SUCCESS = (LLMStatusCode.LLM_SUCCESS.value, (7, [[4096]]))


class CacheDescriptorStateTest(unittest.TestCase):
    def setUp(self):
        self.backend = Mock()
        self.backend.allocate_cache_v2.return_value = SUCCESS
        self.backend.register_cache.return_value = SUCCESS
        self.manager = CacheManager(self.backend, {"llm.MemPoolConfig": "configured"})

    @staticmethod
    def descriptor():
        return CacheDesc(1, [2, 4], DataType.DT_INT8, Placement.DEVICE, 1)

    def call(self, api, desc):
        method = getattr(self.manager, api)
        return method(desc, [4096]) if api.startswith("register") else method(desc)

    def native_method(self, api):
        return (
            self.backend.register_cache
            if api.startswith("register")
            else self.backend.allocate_cache_v2
        )

    def assert_kind(self, api, cache, expected):
        packed = self.native_method(api).call_args.args[0]
        self.assertEqual(packed[-1], expected)
        self.assertEqual(cache.is_blocks_cache, expected)
        self.assertEqual(
            packed[:6],
            (1, DataType.DT_INT8.value, -1, 1, [2, 4], Placement.DEVICE.value),
        )

    def test_successful_block_operation_does_not_change_input(self):
        for block_api in BLOCK_APIS:
            with self.subTest(api=block_api):
                desc = self.descriptor()
                before = pack_cache_desc(desc)
                cache = self.call(block_api, desc)
                self.assert_kind(block_api, cache, True)
                self.assertEqual(pack_cache_desc(desc), before)

    def test_regular_cache_after_block_cache_uses_regular_native_kind(self):
        for block_api, regular_api in API_PAIRS:
            with self.subTest(block=block_api, regular=regular_api):
                desc = self.descriptor()
                self.call(block_api, desc)
                regular = self.call(regular_api, desc)
                self.assert_kind(regular_api, regular, False)

    def test_failure_status_does_not_contaminate_retry(self):
        for block_api, regular_api in API_PAIRS:
            with self.subTest(block=block_api, regular=regular_api):
                desc = self.descriptor()
                native = self.native_method(block_api)
                native.return_value = (LLMStatusCode.LLM_FAILED.value, ())
                try:
                    with self.assertRaises(LLMException):
                        self.call(block_api, desc)
                finally:
                    native.return_value = SUCCESS
                self.assertFalse(desc._is_blocks)
                self.assert_kind(regular_api, self.call(regular_api, desc), False)

    def test_native_exception_does_not_contaminate_retry(self):
        for block_api, regular_api in API_PAIRS:
            with self.subTest(block=block_api, regular=regular_api):
                desc = self.descriptor()
                native = self.native_method(block_api)
                native.side_effect = RuntimeError("injected native failure")
                try:
                    with self.assertRaisesRegex(
                        RuntimeError, "injected native failure"
                    ):
                        self.call(block_api, desc)
                finally:
                    native.side_effect = None
                self.assertFalse(desc._is_blocks)
                self.assert_kind(regular_api, self.call(regular_api, desc), False)

    def test_existing_regular_cache_descriptor_is_not_changed(self):
        for block_api, regular_api in API_PAIRS:
            with self.subTest(block=block_api, regular=regular_api):
                desc = self.descriptor()
                regular = self.call(regular_api, desc)
                before = pack_cache_desc(regular.cache_desc)
                self.call(block_api, desc)
                self.assertEqual(pack_cache_desc(regular.cache_desc), before)
                self.assertFalse(regular.is_blocks_cache)

    def test_returned_block_descriptor_can_be_reused_for_regular_cache(self):
        for block_api, regular_api in API_PAIRS:
            with self.subTest(block=block_api, regular=regular_api):
                block = self.call(block_api, self.descriptor())
                regular = self.call(regular_api, block.cache_desc)
                self.assert_kind(regular_api, regular, False)
                self.assertTrue(block.is_blocks_cache)

    def test_api_kind_overrides_legacy_descriptor_flag(self):
        for api, initial_kind in product(BLOCK_APIS + REGULAR_APIS, (False, True)):
            with self.subTest(api=api, initial_kind=initial_kind):
                desc = self.descriptor()
                desc._is_blocks = initial_kind
                result = self.call(api, desc)
                self.assert_kind(api, result, api in BLOCK_APIS)
                self.assertEqual(desc._is_blocks, initial_kind)

    def test_packing_default_preserves_legacy_behavior(self):
        for initial_kind in (False, True):
            with self.subTest(initial_kind=initial_kind):
                desc = self.descriptor()
                desc._is_blocks = initial_kind
                original = pack_cache_desc(desc)
                self.assertEqual(original[-1], initial_kind)
                for override in (False, True):
                    packed = pack_cache_desc(desc, is_blocks=override)
                    self.assertEqual(packed[:6], original[:6])
                    self.assertEqual(packed[-1], override)
                    self.assertEqual(desc._is_blocks, initial_kind)

    def test_interleaved_calls_keep_independent_native_kinds(self):
        for block_api, regular_api in API_PAIRS:
            with self.subTest(block=block_api, regular=regular_api):
                desc = self.descriptor()
                entered, release = Event(), Event()
                observed = []

                def block_native(packed, *args):
                    observed.append(packed[-1])
                    entered.set()
                    if not release.wait(timeout=5):
                        raise RuntimeError("test release timed out")
                    return SUCCESS

                native = self.native_method(block_api)
                native.side_effect = block_native
                with ThreadPoolExecutor(max_workers=1) as executor:
                    future = executor.submit(self.call, block_api, desc)
                    try:
                        self.assertTrue(entered.wait(timeout=5))
                        native.side_effect = None
                        regular = self.call(regular_api, desc)
                        self.assert_kind(regular_api, regular, False)
                    finally:
                        native.side_effect = None
                        release.set()
                    self.assertTrue(future.result(timeout=5).is_blocks_cache)
                self.assertEqual(observed, [True])
                self.assertFalse(desc._is_blocks)


if __name__ == "__main__":
    unittest.main()
