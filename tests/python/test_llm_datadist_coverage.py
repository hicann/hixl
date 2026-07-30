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

import sys
import unittest
from unittest.mock import MagicMock, patch

import llm_datadist as ld
from llm_datadist import (
    LLMClusterInfo,
    LLMDataDist,
    LLMException,
    LLMRole,
    LLMStatusCode,
    LlmConfig,
)
from llm_datadist.v2.llm_datadist import _shutdown_handler
from llm_datadist import llm_datadist_wrapper


class LlmDatadistLazyLoadSt(unittest.TestCase):
    """Tests for llm_datadist/__init__.py __getattr__ lazy-load paths."""

    def test_getattr_invalid_name(self):
        with self.assertRaises(AttributeError) as ex:
            getattr(ld, "NonExistentAttr")
        self.assertIn("has no attribute", str(ex.exception))

    def test_getattr_kv_cache_manager_import(self):
        with self.assertRaises(ImportError):
            getattr(ld, "KvCacheManager")

    def test_getattr_tensor_import(self):
        with self.assertRaises(ImportError):
            getattr(ld, "Tensor")

    def test_getattr_tensor_desc_import(self):
        with self.assertRaises(ImportError):
            getattr(ld, "TensorDesc")


class LlmDatadistInitValidationSt(unittest.TestCase):
    """Tests for init / _setup_engine_option validation paths (no hardware needed)."""

    def setUp(self):
        LLMDataDist.llm_engine_instance = None

    def _base_config(self):
        config = LlmConfig()
        config.device_id = 0
        return config

    def _full_config(self, cluster_id):
        config = self._base_config()
        config.enable_cache_manager = True
        config.mem_pool_cfg = '{"memory_size": 102428800}'
        config.sync_kv_timeout = "3000"
        config.rdma_service_level = 100
        config.rdma_traffic_class = 100
        return config

    def test_init_already_initialized(self):
        config = self._full_config(10)
        llm = LLMDataDist(LLMRole.PROMPT, 10)
        llm.init(config.generate_options())
        try:
            llm.init(config.generate_options())
        finally:
            llm.finalize()

    def test_init_multiple_instances(self):
        config = self._full_config(11)
        llm1 = LLMDataDist(LLMRole.PROMPT, 11)
        llm1.init(config.generate_options())
        try:
            llm2 = LLMDataDist(LLMRole.DECODER, 12)
            with self.assertRaises(LLMException) as ex:
                llm2.init(config.generate_options())
            self.assertEqual(ex.exception.status_code, LLMStatusCode.LLM_FAILED)
        finally:
            llm1.finalize()

    def test_flow_graph_max_size_non_str(self):
        config = self._base_config()
        config.enable_cache_manager = True
        config.mem_pool_cfg = '{"memory_size": 102428800}'
        options = config.generate_options()
        options["ge.flowGraphMemMaxSize"] = 123
        llm = LLMDataDist(LLMRole.PROMPT, 13)
        with self.assertRaises(TypeError):
            llm.init(options)

    def test_flow_graph_max_size_multiple_pools(self):
        config = self._base_config()
        config.enable_cache_manager = True
        config.mem_pool_cfg = '{"memory_size": 102428800}'
        config.ge_options = {"ge.flowGraphMemMaxSize": "1024,2048"}
        llm = LLMDataDist(LLMRole.PROMPT, 14)
        with self.assertRaises(LLMException):
            llm.init(config.generate_options())

    def test_transfer_backend_without_cache_mgr(self):
        config = self._base_config()
        config.transfer_backend = "hixl"
        config.listen_ip_info = "127.0.0.1:26000"
        llm = LLMDataDist(LLMRole.PROMPT, 15)
        with self.assertRaises(LLMException) as ex:
            llm.init(config.generate_options())
        self.assertIn("EnableCacheManager", str(ex.exception))

    def test_transfer_backend_without_listen_ip(self):
        config = self._base_config()
        config.enable_cache_manager = True
        config.mem_pool_cfg = '{"memory_size": 102428800}'
        config.transfer_backend = "hixl"
        config.enable_remote_cache_accessible = True
        llm = LLMDataDist(LLMRole.PROMPT, 16)
        with self.assertRaises(LLMException) as ex:
            llm.init(config.generate_options())
        self.assertIn("listenIpInfo", str(ex.exception))

    def test_transfer_backend_remote_cache_disabled(self):
        config = self._base_config()
        config.enable_cache_manager = True
        config.mem_pool_cfg = '{"memory_size": 102428800}'
        config.transfer_backend = "hixl"
        config.enable_remote_cache_accessible = False
        config.listen_ip_info = "127.0.0.1:26000"
        llm = LLMDataDist(LLMRole.PROMPT, 17)
        with self.assertRaises(LLMException) as ex:
            llm.init(config.generate_options())
        self.assertIn("EnableRemoteCacheAccessible", str(ex.exception))

    def test_finalize_not_initialized(self):
        llm = LLMDataDist(LLMRole.PROMPT, 18)
        llm.finalize()

    def test_shutdown_handler_no_instance(self):
        LLMDataDist.llm_engine_instance = None
        _shutdown_handler()

    def test_role_to_str_all_roles(self):
        self.assertEqual(LLMDataDist._role_to_str(LLMRole.PROMPT), "Prompt")
        self.assertEqual(LLMDataDist._role_to_str(LLMRole.DECODER), "Decoder")
        self.assertEqual(LLMDataDist._role_to_str(LLMRole.MIX), "Mix")


class LlmDatadistCoverageSt(unittest.TestCase):
    """Coverage tests for llm_datadist.py (needs hardware)."""

    def setUp(self) -> None:
        print(f"Begin {self.__class__.__name__}.{self._testMethodName}")
        config = LlmConfig()
        config.device_id = 0
        config.enable_cache_manager = True
        config.mem_pool_cfg = '{"memory_size": 102428800}'
        config.sync_kv_timeout = "3000"
        config.rdma_service_level = 100
        config.rdma_traffic_class = 100
        engine_options = config.generate_options()
        self.llm_datadist = LLMDataDist(LLMRole.PROMPT, 3)
        self.llm_datadist.init(engine_options)
        self.has_exception = False

    def tearDown(self) -> None:
        print(f"End {self.__class__.__name__}.{self._testMethodName}")
        self.llm_datadist.finalize()

    def test_cluster_id_property(self):
        self.assertEqual(self.llm_datadist.cluster_id, 3)

    def test_kv_cache_manager_in_cache_mgr_mode(self):
        with self.assertRaises(LLMException):
            _ = self.llm_datadist.kv_cache_manager

    def test_check_link_status_in_cache_mgr_mode(self):
        with self.assertRaises(LLMException):
            self.llm_datadist.check_link_status(1)

    def test_link_empty_comm_name(self):
        with self.assertRaises(LLMException):
            self.llm_datadist.link("", {3: 0, 2: 1}, "{}")

    def test_link_comm_name_too_long(self):
        long_name = "a" * 128
        with self.assertRaises(LLMException):
            self.llm_datadist.link(long_name, {3: 0, 2: 1}, "{}")

    def test_link_cluster_rank_info_not_dict(self):
        with self.assertRaises(TypeError):
            self.llm_datadist.link("link", [3, 2], "{}")

    def test_link_too_many_nodes(self):
        with self.assertRaises(LLMException):
            self.llm_datadist.link("link", {1: 0, 2: 1, 3: 2, 4: 3, 5: 4}, "{}")

    def test_link_single_rank(self):
        with self.assertRaises(LLMException):
            self.llm_datadist.link("link", {3: 0}, "{}")

    def test_link_unordered_ranks(self):
        with self.assertRaises(LLMException):
            self.llm_datadist.link("link", {3: 1, 2: 0}, "{}")

    def test_link_rank_table_not_str(self):
        with self.assertRaises(TypeError):
            self.llm_datadist.link("link", {3: 0, 2: 1}, 123)

    def test_unlink_bad_comm_id_type(self):
        with self.assertRaises(TypeError):
            self.llm_datadist.unlink("not_int")

    def test_link_clusters_invalid_timeout(self):
        cluster = LLMClusterInfo()
        cluster.remote_cluster_id = 1
        cluster.append_local_ip_info("127.0.0.1", 26008)
        cluster.append_remote_ip_info("127.0.0.1", 26008)
        with self.assertRaises(LLMException):
            self.llm_datadist.link_clusters([cluster], 0)

    def test_link_clusters_bad_clusters_type(self):
        with self.assertRaises(TypeError):
            self.llm_datadist.link_clusters("not_list", 5000)

    def test_unlink_clusters_invalid_timeout(self):
        cluster = LLMClusterInfo()
        cluster.remote_cluster_id = 1
        cluster.append_local_ip_info("127.0.0.1", 26008)
        cluster.append_remote_ip_info("127.0.0.1", 26008)
        with self.assertRaises(LLMException):
            self.llm_datadist.unlink_clusters([cluster], 0)

    def test_unlink_clusters_bad_force_type(self):
        cluster = LLMClusterInfo()
        cluster.remote_cluster_id = 1
        cluster.append_local_ip_info("127.0.0.1", 26008)
        cluster.append_remote_ip_info("127.0.0.1", 26008)
        with self.assertRaises(TypeError):
            self.llm_datadist.unlink_clusters([cluster], 5000, force="not_bool")

    def test_switch_role_without_options(self):
        try:
            self.llm_datadist.switch_role(LLMRole.DECODER)
        except Exception:
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_switch_role_with_listen_ip_info(self):
        try:
            self.llm_datadist.switch_role(LLMRole.DECODER)
            options = {"llm.listenIpInfo": "127.0.0.1:26009"}
            self.llm_datadist.switch_role(LLMRole.PROMPT, options)
        except Exception:
            self.has_exception = True
        self.assertEqual(self.has_exception, False)

    def test_switch_role_bad_role_type(self):
        with self.assertRaises(TypeError):
            self.llm_datadist.switch_role("not_a_role")

    def test_switch_role_bad_options_type(self):
        with self.assertRaises(TypeError):
            self.llm_datadist.switch_role(LLMRole.DECODER, "not_dict")

    def test_shutdown_handler_with_instance(self):
        _shutdown_handler()
        self.assertIsNone(LLMDataDist.llm_engine_instance)

    def test_not_inited_raises(self):
        llm = LLMDataDist(LLMRole.PROMPT, 99)
        with self.assertRaises(RuntimeError):
            llm.link("link", {3: 0, 2: 1}, "{}")
        with self.assertRaises(RuntimeError):
            llm.unlink(1)
        with self.assertRaises(RuntimeError):
            _ = llm.cache_manager


class LlmDatadistV1ModeMockSt(unittest.TestCase):
    """Coverage for v1 (non-cache-mgr) code paths using mocks (no hardware needed)."""

    def setUp(self):
        LLMDataDist.llm_engine_instance = None
        self.llm = LLMDataDist(LLMRole.MIX, 100)
        self.llm._is_initialized = True
        self.llm._enable_cache_mgr = False
        self.llm._llm_datadist = MagicMock()
        self.llm._kv_cache_manager = MagicMock()
        self.llm._engine_options = {}

    def tearDown(self):
        LLMDataDist.llm_engine_instance = None

    def _make_cluster(self):
        cluster = LLMClusterInfo()
        cluster.remote_cluster_id = 1
        cluster.append_local_ip_info("127.0.0.1", 26008)
        cluster.append_remote_ip_info("127.0.0.1", 26008)
        return cluster

    def test_v1_switch_role_to_decoder(self):
        self.llm._llm_datadist.switch_role.return_value = int(
            llm_datadist_wrapper.kSuccess
        )
        self.llm.switch_role(LLMRole.DECODER)
        self.assertEqual(self.llm._role, LLMRole.DECODER)

    def test_v1_switch_role_to_prompt_with_listen_ip(self):
        self.llm._llm_datadist.switch_role.return_value = int(
            llm_datadist_wrapper.kSuccess
        )
        options = {"llm.listenIpInfo": "127.0.0.1:26009"}
        self.llm.switch_role(LLMRole.PROMPT, options)
        self.assertEqual(self.llm._role, LLMRole.PROMPT)

    def test_v1_switch_role_to_prompt_without_listen_ip(self):
        with self.assertRaises(LLMException):
            self.llm.switch_role(LLMRole.PROMPT, {})

    def test_v1_switch_role_same_role(self):
        with self.assertRaises(LLMException):
            self.llm.switch_role(LLMRole.MIX)

    def test_v1_check_link_status(self):
        self.llm._llm_datadist.check_link_status.return_value = int(
            llm_datadist_wrapper.kSuccess
        )
        self.llm.check_link_status(1)

    def test_v1_link_clusters(self):
        self.llm._llm_datadist.link_clusters.return_value = (
            int(llm_datadist_wrapper.kSuccess),
            [int(llm_datadist_wrapper.kSuccess)],
        )
        ret, rets = self.llm.link_clusters([self._make_cluster()], 5000)
        self.assertEqual(ret, LLMStatusCode.LLM_SUCCESS)

    def test_v1_unlink_clusters(self):
        self.llm._llm_datadist.unlink_clusters.return_value = (
            int(llm_datadist_wrapper.kSuccess),
            [int(llm_datadist_wrapper.kSuccess)],
        )
        ret, rets = self.llm.unlink_clusters([self._make_cluster()], 5000)
        self.assertEqual(ret, LLMStatusCode.LLM_SUCCESS)

    def test_v1_kv_cache_manager(self):
        self.assertEqual(self.llm.kv_cache_manager, self.llm._kv_cache_manager)

    def test_link_clusters_empty_local_ip_info_list(self):
        self.llm._enable_cache_mgr = True
        self.llm._enable_local_comm_res = False
        cluster = LLMClusterInfo()
        cluster.remote_cluster_id = 1
        with self.assertRaises(LLMException) as ex:
            self.llm.link_clusters([cluster], 5000)
        self.assertIn("local_ip_info_list is empty", str(ex.exception))

    @patch("llm_datadist.v2.llm_datadist.EngineConfig")
    def test_cluster_config(self, mock_engine_config_cls):
        mock_config = MagicMock()
        mock_engine_config_cls.from_engine_options.return_value = mock_config
        result = self.llm._cluster_config()
        self.assertEqual(result, mock_config.cluster_config)

    def test_shutdown_handler_finalize_exception(self):
        mock_instance = MagicMock()
        mock_instance.finalize.side_effect = LLMException("test error")
        LLMDataDist.llm_engine_instance = mock_instance
        _shutdown_handler()

    @patch("llm_datadist.v2.llm_datadist.EngineConfig")
    def test_init_v1_mode(self, mock_engine_config_cls):
        mock_v1 = MagicMock()
        mock_v1.llm_wrapper.initialize.return_value = int(llm_datadist_wrapper.kSuccess)
        mock_kv_mgr_module = MagicMock()
        with patch.dict(
            sys.modules,
            {
                "llm_datadist_v1": mock_v1,
                "llm_datadist_v1.kv_cache_manager": mock_kv_mgr_module,
            },
        ):
            llm = LLMDataDist(LLMRole.PROMPT, 200)
            llm.init({"ge.exec.deviceId": "0"})
            self.assertTrue(llm._is_initialized)
            llm.finalize()
