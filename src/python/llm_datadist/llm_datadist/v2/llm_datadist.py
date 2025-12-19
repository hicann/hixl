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
import copy
from typing import Dict, List, Tuple, Optional, Union
import atexit
from llm_datadist import llm_datadist_wrapper
from llm_datadist.utils.utils import check_isinstance, check_dict, check_uint64, check_int32, check_uint32
from llm_datadist.utils import log
from llm_datadist.status import (code_2_status, handle_llm_status, raise_if_false, raise_if_true, LLMStatusCode,
                                 LLMException)
from llm_datadist.configs import LLMRole, LLMClusterInfo
from llm_datadist.v2.config import EngineConfig
from llm_datadist.v2.llm_types import RegisterMemStatus, int_to_mem_status_dict
from llm_datadist.v2.cache_manager import CacheManager
from llm_datadist.v2.llm_utils import parse_listen_ip_info

__all__ = ['LLMDataDist']
_MAX_CLUSTER_NAME = 128
_MAX_NODE_NUM = 4

class LLMDataDist(object):
    llm_engine_instance = None
    """
    LLMDataDist

    Args:
        role: role of LLMDataDist
        cluster_id: cluster_id of LLMDataDist
    """

    def __init__(self, role: LLMRole, cluster_id: int):
        check_isinstance("role", role, LLMRole)
        self._kv_cache_manager = None
        self._cache_manager = None
        self._role = role
        check_uint64("cluster_id", cluster_id)
        self._cluster_id = cluster_id
        self._llm_datadist = None
        self._engine_config = None
        self._is_initialized = False
        self._engine_options: Dict[str, str] = {}
        self._enable_cache_mgr = False
        self._enable_free_comm = False
        self._enable_local_comm_res = False

    def _check_flow_graph_max_size(self, options: Dict[str, str]) -> None:
        value = options.get("ge.flowGraphMemMaxSize", None)
        if value is None:
            return
        check_isinstance('ge.flowGraphMemMaxSize', value, str)
        raise_if_false(len(value.split(",")) == 1, "ge.flowGraphMemMaxSize only support one mem pool in llm datadist")
        raise_if_false(value.isdigit(), "e.ge.flowGraphMemMaxSize must be digit, config value={0}", value)

    def init(self, options: Dict[str, str]) -> None:
        """
        初始化LLM DataDist

        Args:
            options: Engine相关options
        """
        if self._is_initialized:
            return
        raise_if_false(LLMDataDist.llm_engine_instance is None, 'Cannot init multiple LLMDataDists',
                       status_code=LLMStatusCode.LLM_FAILED)
        check_isinstance("options", options, dict)
        self._check_flow_graph_max_size(options)
        self._engine_options = options
        self._engine_options['llm.Role'] = self._role_to_str(self._role)
        self._enable_local_comm_res = "llm.LocalCommRes" in options
        if self._enable_local_comm_res and "llm.EnableCacheManager" not in options:
            log.info('cache manager is enabled by default when local_comm_res is set in LLMConfig')
            self._engine_options["llm.EnableCacheManager"] = "1"
        if self._enable_local_comm_res and "llm.EnableRemoteCacheAccessible" not in options:
            log.info('remote cache accessible is enabled by default when local_comm_res is set in LLMConfig')
            self._engine_options["llm.EnableRemoteCacheAccessible"] = "1"
        self._enable_cache_mgr = (
            "llm.EnableCacheManager" in self._engine_options and
            self._engine_options["llm.EnableCacheManager"] == "1"
        )
        log.info('LLMDatadist init options = %s', self._engine_options)
        if self._enable_local_comm_res:
            self._check_is_cache_mgr_mode('llm.LocalCommRes')

        if self._enable_cache_mgr:
            if 'llm.listenIpInfo' in options:
                listen_ip_info = options['llm.listenIpInfo']
                ip, port = parse_listen_ip_info(listen_ip_info)
                self._engine_options['llm.ListenIp'] = ip
                self._engine_options['llm.ListenPort'] = str(port)
            self._llm_datadist = llm_datadist_wrapper
            ret = self._llm_datadist.initialize_v2(self._cluster_id, self._engine_options)
            handle_llm_status(ret, '[LLMDataDist.init]', f'Failed to initialize llm datadist, options = {options}')
            self._cache_manager = CacheManager(self._llm_datadist, options)
        else:
            from llm_datadist_v1 import llm_wrapper
            from llm_datadist_v1.kv_cache_manager import KvCacheManager
            self._llm_datadist = llm_wrapper
            EngineConfig.gen_cluster_info_if_not_exist(self._cluster_id, self._role, self._engine_options)
            ret = self._llm_datadist.initialize(self._cluster_id, self._engine_options)
            handle_llm_status(ret, '[LLMDataDist.init]', f'Failed to initialize llm datadist, options = {options}')
            self._kv_cache_manager = KvCacheManager(self._llm_datadist, self._role)
        LLMDataDist.llm_engine_instance = self
        self._is_initialized = True

    def _check_is_cache_mgr_mode(self, func_name):
        raise_if_false(self._enable_cache_mgr,
                       '{0} is not supported when llm.EnableCacheManager is not configured.',
                       func_name)

    def _check_is_not_cache_mgr_mode(self, func_name):
        raise_if_false(not self._enable_cache_mgr,
                       '{0} is not supported when llm.EnableCacheManager is configured.',
                       func_name)

    def link(self, comm_name: str, cluster_rank_info: Dict[int, int], rank_table: str) -> int:
        """
        :param cluster_rank_info:
        :param rank_table:
        :return: comm id
        """
        self._check_is_inited()
        self._check_is_cache_mgr_mode('link')
        check_isinstance("comm_name", comm_name, str, allow_none=False)
        raise_if_true(len(comm_name) == 0, "comm_name can not be empty")
        raise_if_false(len(comm_name) < _MAX_CLUSTER_NAME, "comm_name length should be smaller than 128.")
        check_isinstance("cluster_rank_info", cluster_rank_info, dict)
        check_dict("cluster_rank_info", cluster_rank_info, int, int)
        raise_if_false(len(cluster_rank_info) <= _MAX_NODE_NUM, "cluster_rank_info size can not be greater than 4.")
        for k, v in cluster_rank_info.items():
            check_uint64("cluster_rank_info key", k)
            check_uint32("cluster_rank_info value", v)
        check_isinstance("rank_table", rank_table, str)
        ranks = list(cluster_rank_info.values())
        raise_if_false(len(ranks) > 1, "cluster num must be bigger than 1.")
        ranks_copy = copy.copy(ranks)
        ranks_copy.sort()
        raise_if_false(ranks == ranks_copy, "rank in cluster_rank_info must be ordered")
        cluster_ids = list(cluster_rank_info.keys())
        raise_if_false(len(cluster_ids) == len(set(cluster_ids)),
                       "cluster id in cluster_rank_info can not be duplicated.")
        ret, comm_id = self._llm_datadist.link(comm_name, cluster_rank_info, rank_table)
        handle_llm_status(ret, '[link]', f'cluster_rank_info = {cluster_rank_info}')
        self.cache_manager.set_is_call_linked()
        return comm_id

    def unlink(self, comm_id: int):
        """
        :param comm_id:
        :return:
        """
        self._check_is_inited()
        self._check_is_cache_mgr_mode('unlink')
        check_isinstance('comm_id', comm_id, int)
        check_uint64("comm_id", comm_id)
        ret = self._llm_datadist.unlink(comm_id)
        handle_llm_status(ret, '[unlink]', f'comm_id = {comm_id}')

    def query_register_mem_status(self, comm_id: int) -> RegisterMemStatus:
        """
        :param comm_id:
        :return:
        """
        self._check_is_inited()
        self._check_is_cache_mgr_mode('query_register_mem_status')
        check_isinstance('comm_id', comm_id, int)
        check_uint64("comm_id", comm_id)
        ret, status = self._llm_datadist.query_register_mem_status(comm_id)
        handle_llm_status(ret, '[query_register_mem_status]', f'comm_id = {comm_id}')
        return int_to_mem_status_dict[status] if status in int_to_mem_status_dict else RegisterMemStatus.FAILED

    @property
    def cache_manager(self) -> CacheManager:
        """
        获取KvCacheManager

        Returns:
            KvCacheManager
        """
        self._check_is_inited()
        self._check_is_cache_mgr_mode('cache_manager')
        return self._cache_manager

    def finalize(self) -> None:
        """
        释放LLM DataDist相关资源
        """
        if not self._is_initialized:
            return
        if self._enable_cache_mgr:
            self._llm_datadist.finalize_v2()
        else:
            self._llm_datadist.finalize()
        if self._kv_cache_manager is not None:
            self._kv_cache_manager._initialized = False
        if self._cache_manager is not None:
            self._cache_manager._initialized = False
        self._is_initialized = False
        LLMDataDist.llm_engine_instance = None

    def _cluster_config(self):
        if self._engine_config is None:
            self._engine_config = EngineConfig.from_engine_options(self._role == LLMRole.PROMPT, self._engine_options)
        return self._engine_config.cluster_config

    def check_link_status(self, remote_cluster_id: int):
        self._check_is_inited()
        self._check_is_not_cache_mgr_mode('check_link_status')
        check_uint64("remote_cluster_id", remote_cluster_id)
        ret = self._llm_datadist.check_link_status(remote_cluster_id)
        handle_llm_status(ret, '[check_link_status]', f"remote_cluster_id is {remote_cluster_id}")
        log.info('[check_link_status] success')

    def link_clusters(self, clusters: Union[List[LLMClusterInfo], Tuple[LLMClusterInfo]], timeout=3000):
        self._check_is_inited()
        check_int32("timeout", timeout)
        raise_if_false(timeout > 0, "Param timeout should be greater than 0.")
        check_isinstance("clusters", clusters, [list, tuple], LLMClusterInfo)
        if self._enable_cache_mgr:
            cluster_list = []
            for cluster in clusters:
                if not self._enable_local_comm_res or not self._engine_options['llm.LocalCommRes']:
                    raise_if_false(cluster.local_ip_info_list,
                                   "local_ip_info_list is empty. Call append_local_ip_info to add local_ip_info "
                                   "when local_comm_res option is unspecified or empty.")
                check_uint64("remote_cluster_id", cluster.remote_cluster_id)
                cluster_list.append((cluster.remote_cluster_id, 0, cluster.local_ip_info_list, cluster.remote_ip_info_list))
            ret, rets = self._llm_datadist.link_clusters_v2(cluster_list, timeout)
        else:
            cluster_list = [(cluster.remote_cluster_id,
                             0,
                             cluster.local_ip_info_list,
                             cluster.remote_ip_info_list) for cluster in clusters]
            ret, rets = self._llm_datadist.link_clusters(cluster_list, timeout)
        return code_2_status(ret), [code_2_status(cluster_ret) for cluster_ret in rets]

    def unlink_clusters(self, clusters: Union[List[LLMClusterInfo], Tuple[LLMClusterInfo]], timeout=3000, force=False):
        self._check_is_inited()
        check_int32("timeout", timeout)
        raise_if_false(timeout > 0, "Param timeout should be greater than 0.")
        check_isinstance("clusters", clusters, [list, tuple], LLMClusterInfo)
        check_isinstance("force", force, bool)
        if self._enable_cache_mgr:
            cluster_list = []
            for cluster in clusters:
                check_uint64("remote_cluster_id", cluster.remote_cluster_id)
                cluster_list.append((cluster.remote_cluster_id, 0, [], cluster.remote_ip_info_list))
            ret, rets = self._llm_datadist.unlink_clusters_v2(cluster_list, timeout, force)
        else:
            cluster_list = []
            for cluster in clusters:
                check_uint64("remote_cluster_id", cluster.remote_cluster_id)
                cluster_list.append((cluster.remote_cluster_id, 0, cluster.local_ip_info_list,
                                     cluster.remote_ip_info_list))
            ret, rets = self._llm_datadist.unlink_clusters(cluster_list, timeout, force)
        return code_2_status(ret), [code_2_status(cluster_ret) for cluster_ret in rets]

    def switch_role(self, role: LLMRole, switch_options: Optional[Dict[str, str]] = None):
        self._check_is_inited()
        if self._enable_cache_mgr:
            check_isinstance('role', role, LLMRole)
            role_str = self._role_to_str(role)
            log.info(f'[switch_role] [{self._role.name}->{role.name}] start, switch_options = {switch_options}')
            check_isinstance('switch_options', switch_options, dict)
            options = switch_options.copy() if switch_options is not None else {}
            if switch_options is not None and 'llm.listenIpInfo' in switch_options:
                listen_ip_info = switch_options['llm.listenIpInfo']
                ip, port = parse_listen_ip_info(listen_ip_info)
                options['llm.ListenIp'] = ip
                options['llm.ListenPort'] = str(port)
            ret = self._llm_datadist.switch_role_v2(role_str, options)
            handle_llm_status(ret,
                            '[switch_role]',
                            f'Failed to switch role, role = {role}, options = {options}')
            log.info(f'[switch_role] [{self._role.name}->{role.name}] success')
            self._role = role
        else:
            check_isinstance('role', role, LLMRole)
            raise_if_false(self._role != role, f'role not changed, role = {role.name}')
            role_str = self._role_to_str(role)
            log.info(f'[switch_role] [{self._role.name}->{role.name}] start, switch_options = {switch_options}')
            check_isinstance('switch_options', switch_options, dict)
            options = switch_options.copy() if switch_options is not None else {}
            if role == LLMRole.PROMPT:
                raise_if_false('llm.listenIpInfo' in switch_options,
                            'Failed to switch to Prompt, option "llm.listenIpInfo" was specified')
                listen_ip_info = switch_options['llm.listenIpInfo']
                ip, port = EngineConfig.parse_listen_ip_info(listen_ip_info)
                options['llm.ListenIp'] = str(ip)
                options['llm.ListenPort'] = str(port)
            ret = self._llm_datadist.switch_role(role_str, options)
            handle_llm_status(ret,
                            '[switch_role]',
                            f'Failed to switch role, role = {role}, options = {options}')
            self._kv_cache_manager._switch_role(role)
            log.info(f'[switch_role] [{self._role.name}->{role.name}] success')
            self._role = role

    @staticmethod
    def _role_to_str(role: LLMRole) -> str:
        role_mapping = {
            LLMRole.PROMPT: 'Prompt',
            LLMRole.DECODER: 'Decoder',
            LLMRole.MIX: 'Mix',
        }
        return role_mapping[role]

    def _check_is_inited(self):
        if not self._is_initialized:
            raise RuntimeError('llm datadist is not initialized')
    
    @property
    def kv_cache_manager(self) -> 'KvCacheManager':
        """
        获取KvCacheManager

        Returns:
            KvCacheManager
        """
        self._check_is_inited()
        self._check_is_not_cache_mgr_mode('kv_cache_manager')
        return self._kv_cache_manager

    @property
    def cluster_id(self):
        return self._cluster_id


def _shutdown_handler():
    if LLMDataDist.llm_engine_instance is not None:
        log.info('[shutdown_handler] finalize llm datadist')
        try:
            LLMDataDist.llm_engine_instance.finalize()
        except LLMException as e:
            log.warn(f'error occurred while finalize llm datadist: {e} '
                     f'may cause by already finalized by another framework')


atexit.register(_shutdown_handler)
