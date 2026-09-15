# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Reject stale managers before reaching the current native engine."""

import importlib
import unittest
from unittest.mock import Mock, patch

from llm_datadist.configs import LLMRole
from llm_datadist.status import LLMException, LLMStatusCode
from llm_datadist.v2.cache_manager import CacheManager
from llm_datadist.v2.llm_datadist import LLMDataDist
from llm_datadist.v2.llm_types import (
    BlocksCacheKey,
    Cache,
    CacheDesc,
    CacheKey,
    CacheKeyByIdAndIndex,
    DataType,
    LayerSynchronizer,
    MemInfo,
    Memtype,
    Placement,
    TransferConfig,
)


OPTIONS = {
    "llm.EnableCacheManager": "1",
    "llm.MemPoolConfig": "configured",
    "llm.EnableRemoteCacheAccessible": "1",
}
DATADIST_MODULE = importlib.import_module("llm_datadist.v2.llm_datadist")


class CacheManagerLifecycleTest(unittest.TestCase):
    def setUp(self):
        self.backend = Mock()
        for name in (
            "initialize_v2",
            "deallocate_cache_v2",
            "copy_cache_v2",
            "pull_cache_v2",
            "unregister_cache",
            "remove_cache_key_v2",
            "remap_registered_memory",
            "swap_blocks_v2",
            "transfer_cache_v2",
        ):
            getattr(self.backend, name).return_value = LLMStatusCode.LLM_SUCCESS.value
        for name in ("allocate_cache_v2", "register_cache"):
            getattr(self.backend, name).return_value = (
                LLMStatusCode.LLM_SUCCESS.value,
                (7, [[4096, 8192]]),
            )
        native_patch = patch.object(
            DATADIST_MODULE, "llm_datadist_wrapper", self.backend
        )
        native_patch.start()
        self.addCleanup(native_patch.stop)
        singleton_patch = patch.object(LLMDataDist, "llm_engine_instance", None)
        singleton_patch.start()
        self.addCleanup(singleton_patch.stop)
        async_patch = patch("llm_datadist.v2.cache_manager.transfer_cache_async")
        self.async_start = async_patch.start()
        self.addCleanup(async_patch.stop)

    @staticmethod
    def operations(manager):
        desc = CacheDesc(2, [4, 2], DataType.DT_INT8)
        host_desc = CacheDesc(2, [4, 2], DataType.DT_INT8, Placement.HOST)
        desc._size = host_desc._size = 8
        regular = Cache(7, desc, [4096, 8192], manager, False, False)
        blocks = Cache(8, desc, [4096, 8192], manager, False, True)
        host = Cache(9, host_desc, [12288, 16384], manager, False, True)
        key = CacheKeyByIdAndIndex(1, 7)
        block_key = BlocksCacheKey(1)
        return {
            "allocate_cache": (desc,),
            "allocate_blocks_cache": (desc,),
            "register_cache": (desc, [4096, 8192]),
            "register_blocks_cache": (desc, [4096, 8192]),
            "deallocate_cache": (regular,),
            "deallocate_blocks_cache": (blocks,),
            "copy_cache": (regular, regular),
            "copy_blocks": (blocks, {0: [1]}),
            "pull_cache": (key, regular),
            "pull_blocks": (block_key, blocks, [0], [1]),
            "push_cache": (key, regular),
            "push_blocks": (block_key, blocks, [0], [1]),
            "unregister_cache": (7,),
            "remove_cache_key": (CacheKey(prompt_cluster_id=1, req_id=2),),
            "remap_registered_memory": (MemInfo(Memtype.MEM_TYPE_DEVICE, 4096, 8),),
            "swap_blocks": (blocks, host, {0: 1}),
            "transfer_cache_async": (
                regular,
                Mock(spec=LayerSynchronizer),
                [TransferConfig(1, [4096, 8192])],
            ),
        }

    def start_engine(self):
        engine = LLMDataDist(LLMRole.PROMPT, 0)
        engine.init(OPTIONS)
        return engine

    def assert_expired_operations(self, manager):
        for name in self.operations(manager):
            with self.subTest(operation=name):
                args = self.operations(manager)[name]
                self.backend.reset_mock()
                self.async_start.reset_mock()
                with self.assertRaises(LLMException) as caught:
                    getattr(manager, name)(*args)
                self.assertEqual(
                    caught.exception.status_code, LLMStatusCode.LLM_ENGINE_FINALIZED
                )
                self.assertEqual(self.backend.mock_calls, [])
                self.async_start.assert_not_called()

    def test_operations_reject_manager_after_public_finalize(self):
        engine = self.start_engine()
        manager = engine.cache_manager
        engine.finalize()
        self.assertFalse(manager._initialized)
        self.assert_expired_operations(manager)

    def test_old_manager_cannot_access_reinitialized_same_engine(self):
        engine = self.start_engine()
        old_manager = engine.cache_manager
        engine.finalize()
        engine.init(OPTIONS)
        try:
            self.assertIsNot(engine.cache_manager, old_manager)
            self.assert_expired_operations(old_manager)
            current = engine.cache_manager.allocate_cache(
                self.operations(engine.cache_manager)["allocate_cache"][0]
            )
            self.assertEqual(current.cache_id, 7)
        finally:
            engine.finalize()

    def test_old_manager_cannot_access_replacement_engine(self):
        old_engine = self.start_engine()
        old_manager = old_engine.cache_manager
        old_engine.finalize()
        replacement = self.start_engine()
        try:
            self.assert_expired_operations(old_manager)
            self.assertTrue(replacement.cache_manager._initialized)
        finally:
            replacement.finalize()

    def test_all_operations_still_reach_backend_while_initialized(self):
        manager = CacheManager(self.backend, OPTIONS)
        for name in self.operations(manager):
            with self.subTest(operation=name):
                args = self.operations(manager)[name]
                self.backend.reset_mock()
                self.async_start.reset_mock()
                getattr(manager, name)(*args)
                if name == "transfer_cache_async":
                    self.async_start.assert_called_once()
                else:
                    self.assertTrue(self.backend.mock_calls)

    def test_repeated_finalize_keeps_manager_expired(self):
        engine = self.start_engine()
        manager = engine.cache_manager
        engine.finalize()
        engine.finalize()
        self.backend.finalize_v2.assert_called_once()
        self.assert_expired_operations(manager)


if __name__ == "__main__":
    unittest.main()
