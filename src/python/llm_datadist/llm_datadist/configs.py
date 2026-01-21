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

import json
import socket
from enum import Enum
from typing import List, Tuple, Union

from .data_type import DataType
from .utils.utils import check_isinstance, check_dict, check_uint64, check_int32, check_uint32, check_uint16
from .status import raise_if_false

_INVALID_ID = 2 ** 64 - 1
_INT32_MAX = 2 ** 32 - 1


class LLMRole(Enum):
    PROMPT = 1
    DECODER = 2
    MIX = 3


def trans_str_ip(ip):
    if isinstance(ip, str):
        try:
            ip_bytes = socket.inet_aton(ip)
            return int.from_bytes(ip_bytes, byteorder="little")
        except:
            raise RuntimeError(f"Can not parse ip str:{ip}")
    return ip


class LLMClusterInfo(object):
    def __init__(self):
        self._remote_cluster_id = None
        self._remote_role_type = None
        self._local_ip_info_list: List[Tuple[int, int]] = []
        self._remote_ip_info_list: List[Tuple[int, int]] = []

    def _check_inputs(self, ip, port):
        check_isinstance("ip", ip, [str, int])
        check_uint16("port", port)
        return trans_str_ip(ip)

    @property
    def remote_role_type(self):
        return self._remote_role_type

    @property
    def remote_cluster_id(self):
        return self._remote_cluster_id

    @property
    def local_ip_info_list(self):
        return self._local_ip_info_list

    @property
    def remote_ip_info_list(self):
        return self._remote_ip_info_list

    @remote_role_type.setter
    def remote_role_type(self, remote_role_type: Union[LLMRole, int]):
        check_isinstance("remote_role_type", remote_role_type, [LLMRole, int])
        self._remote_role_type = remote_role_type

    @remote_cluster_id.setter
    def remote_cluster_id(self, remote_cluster_id):
        check_uint64("remote_cluster_id", remote_cluster_id)
        self._remote_cluster_id = remote_cluster_id

    def append_local_ip_info(self, ip: Union[str, int], port: int):
        """
        添加本地IP信息
        Args:
            ip: IP
            port: 端口
        """
        ip = self._check_inputs(ip, port)
        self._local_ip_info_list.append((ip, port))

    def append_remote_ip_info(self, ip: Union[str, int], port: int):
        """
        添加对端IP信息
        Args:
            ip: IP
            port: 端口
        """
        ip = self._check_inputs(ip, port)
        self._remote_ip_info_list.append((ip, port))


class LlmConfig(object):
    def __init__(self):
        self._options = {}
        self._listen_ip_info = ""
        self._device_id = None
        self._sync_kv_timeout = None
        self._deploy_res_path = ""
        self._ge_options = {}
        self._enable_switch_role = False
        self._link_total_time = None
        self._link_retry_count = None

        # below is offline
        self._cluster_info = ""
        self._output_max_size = ""
        self._mem_utilization = 0.95
        self._buf_pool_cfg = ""
        self._mem_pool_cfg = ""
        self._host_mem_pool_cfg = ""
        self._enable_cache_manager = None
        self._enable_remote_cache_accessible = None
        self._rdma_traffic_class = None
        self._rdma_service_level = None
        self._local_comm_res = None

    def generate_options(self):
        """
        生成LLM DataDist配置项
        Returns:
            配置项dict
        """
        return self.gen_options()

    def gen_options(self):
        if self.ge_options:
            self._options.update(self.ge_options)
        if self.listen_ip_info:
            self._options["llm.listenIpInfo"] = str(self.listen_ip_info)
        if self.device_id is not None:
            if isinstance(self.device_id, int):
                self._options["ge.exec.deviceId"] = str(self.device_id)
                self._options["ge.session_device_id"] = str(self.device_id)
            else:
                self._options["ge.session_device_id"] = str(self.device_id[0])
                self._options["ge.exec.deviceId"] = ";".join([str(dev) for dev in self.device_id])
        if self.sync_kv_timeout is not None:
            self._options["llm.SyncKvCacheWaitTime"] = str(self.sync_kv_timeout)
        if self.deploy_res_path:
            self._options["llm.deployResPath"] = str(self.deploy_res_path)
        if self.buf_pool_cfg:
            self._options["llm.BufPoolCfg"] = str(self.buf_pool_cfg)
        if self._mem_pool_cfg:
            self._options["llm.MemPoolConfig"] = str(self._mem_pool_cfg)
        if self._host_mem_pool_cfg:
            self._options["llm.HostMemPoolConfig"] = str(self._host_mem_pool_cfg)
        if self._enable_cache_manager is not None:
            self._options["llm.EnableCacheManager"] = "1" if self._enable_cache_manager else "0"
        if self._enable_remote_cache_accessible is not None:
            self._options["llm.EnableRemoteCacheAccessible"] = "1" if self._enable_remote_cache_accessible else "0"

        # below is offline
        if self._cluster_info:
            self._options["llm.ClusterInfo"] = str(self.cluster_info)
        if self._output_max_size:
            self._options["llm.OutputMaxSize"] = str(self.output_max_size)
        if self._enable_switch_role:
            self._options["llm.EnableSwitchRole"] = "1"
        if self._link_total_time is not None:
            self._options["llm.LinkTotalTime"] = str(self.link_total_time)
        if self._link_retry_count is not None:
            self._options["llm.LinkRetryCount"] = str(self.link_retry_count)
        if self._mem_utilization is not None:
            self._options["llm.MemoryUtilization"] = str(self.mem_utilization)
        if self.rdma_traffic_class is not None:
            self._options["llm.RdmaTrafficClass"] = str(self.rdma_traffic_class)
        if self.rdma_service_level is not None:
            self._options["llm.RdmaServiceLevel"] = str(self.rdma_service_level)
        if self._local_comm_res is not None:
            self._options["llm.LocalCommRes"] = str(self.local_comm_res)
        return self.options

    @property
    def ge_options(self):
        return self._ge_options

    @ge_options.setter
    def ge_options(self, ge_options):
        check_isinstance("ge_options", ge_options, dict)
        check_dict("ge_options", ge_options, str, str)
        self._ge_options = ge_options

    @property
    def device_id(self):
        return self._device_id

    @device_id.setter
    def device_id(self, device_id):
        check_isinstance("device_id", device_id, [list, tuple, int])
        if isinstance(device_id, list) or isinstance(device_id, tuple):
            check_isinstance("device_id", device_id, [list, tuple], int)
            [raise_if_false(dev_id >= 0, "device_id should be greater than or equal to zero.") for dev_id in device_id]
            [check_int32('device_id', dev_id) for dev_id in device_id]
        else:
            check_isinstance("device_id", device_id, int)
            raise_if_false(device_id >= 0, "device_id should be greater than or equal to zero.")
            check_int32('device_id', device_id)
        self._device_id = device_id

    @property
    def listen_ip_info(self):
        return self._listen_ip_info

    @listen_ip_info.setter
    def listen_ip_info(self, listen_ip_info):
        check_isinstance("listen_ip_info", listen_ip_info, str)
        self._listen_ip_info = listen_ip_info

    @property
    def deploy_res_path(self):
        return self._deploy_res_path

    @deploy_res_path.setter
    def deploy_res_path(self, deploy_res_path):
        check_isinstance("deploy_res_path", deploy_res_path, str)
        self._deploy_res_path = deploy_res_path

    @property
    def buf_pool_cfg(self):
        return self._buf_pool_cfg

    @buf_pool_cfg.setter
    def buf_pool_cfg(self, buf_pool_cfg):
        check_isinstance("buf_pool_cfg", buf_pool_cfg, str)
        self._buf_pool_cfg = buf_pool_cfg

    @property
    def output_max_size(self):
        return self._output_max_size

    @output_max_size.setter
    def output_max_size(self, output_max_size):
        check_isinstance("output_max_size", output_max_size, int)
        self._output_max_size = output_max_size

    @property
    def mem_utilization(self):
        return self._mem_utilization

    @mem_utilization.setter
    def mem_utilization(self, mem_utilization):
        check_isinstance("mem_utilization", mem_utilization, float)
        raise_if_false(((mem_utilization >= 0.0) and (mem_utilization <= 1.0)),
                       f"mem_utilization must be in range [0,1], current:{mem_utilization}")
        self._mem_utilization = mem_utilization

    @property
    def options(self):
        return self._options

    @property
    def cluster_info(self):
        return self._cluster_info

    @property
    def sync_kv_timeout(self):
        return self._sync_kv_timeout

    @cluster_info.setter
    def cluster_info(self, cluster_info):
        check_isinstance("cluster_info", cluster_info, str)
        cluster_info_dict = json.loads(cluster_info)
        if "listen_ip_info" in cluster_info_dict:
            for ip_info in cluster_info_dict["listen_ip_info"]:
                ip_info["ip"] = trans_str_ip(ip_info["ip"])
        self._cluster_info = json.dumps(cluster_info_dict)

    @sync_kv_timeout.setter
    def sync_kv_timeout(self, sync_kv_timeout):
        check_isinstance("sync_kv_timeout", sync_kv_timeout, [int, str])
        if isinstance(sync_kv_timeout, str):
            raise_if_false(sync_kv_timeout.isdigit(), "sync_kv_timeout must be digit.")
        raise_if_false(int(sync_kv_timeout) > 0, "sync_kv_timeout should be greater than zero.")
        check_int32('sync_kv_timeout', int(sync_kv_timeout))
        self._sync_kv_timeout = sync_kv_timeout

    @property
    def enable_switch_role(self):
        return self._enable_switch_role

    @enable_switch_role.setter
    def enable_switch_role(self, enable_switch_role: bool):
        check_isinstance("enable_switch_role", enable_switch_role, [bool])
        self._enable_switch_role = enable_switch_role

    @property
    def link_total_time(self):
        return 0 if self._link_total_time is None else self._link_total_time
    
    @link_total_time.setter
    def link_total_time(self, link_total_time: int):
        check_isinstance("link_total_time", link_total_time, int)
        raise_if_false(0 <= link_total_time <= _INT32_MAX, f"link_total_time should be an integer between [0, 2^32-1], given value is {link_total_time}")
        self._link_total_time = link_total_time

    @property
    def link_retry_count(self):
        return 1 if self._link_retry_count is None else self._link_retry_count
    
    @link_retry_count.setter
    def link_retry_count(self, link_retry_count):
        check_isinstance("link_retry_count", link_retry_count, int)
        raise_if_false(1 <= link_retry_count <= 100,
                       f"link_retry_count should be an integer between [1, 100], given value is {link_retry_count}")
        self._link_retry_count = link_retry_count

    @property
    def enable_cache_manager(self):
        return False if self._enable_cache_manager is None else self._enable_cache_manager

    @enable_cache_manager.setter
    def enable_cache_manager(self, enable_cache_manager: bool):
        check_isinstance("enable_cache_manager", enable_cache_manager, [bool])
        self._enable_cache_manager = enable_cache_manager

    @property
    def enable_remote_cache_accessible(self):
        return False if self._enable_remote_cache_accessible is None else self._enable_remote_cache_accessible

    @enable_remote_cache_accessible.setter
    def enable_remote_cache_accessible(self, enable_remote_cache_accessible: bool):
        check_isinstance("enable_remote_cache_accessible", enable_remote_cache_accessible, [bool])
        self._enable_remote_cache_accessible = enable_remote_cache_accessible

    @property
    def mem_pool_cfg(self) -> str:
        return self._mem_pool_cfg

    @mem_pool_cfg.setter
    def mem_pool_cfg(self, mem_pool_cfg: str):
        check_isinstance("mem_pool_cfg", mem_pool_cfg, str)
        self._mem_pool_cfg = mem_pool_cfg

    @property
    def host_mem_pool_cfg(self) -> str:
        return self._host_mem_pool_cfg

    @host_mem_pool_cfg.setter
    def host_mem_pool_cfg(self, host_mem_pool_cfg: str):
        check_isinstance("host_mem_pool_cfg", host_mem_pool_cfg, str)
        self._host_mem_pool_cfg = host_mem_pool_cfg

    @property
    def rdma_traffic_class(self) -> str:
        return self._rdma_traffic_class

    @rdma_traffic_class.setter
    def rdma_traffic_class(self, rdma_traffic_class: int):
        check_uint32("rdma_traffic_class", rdma_traffic_class)
        self._rdma_traffic_class = rdma_traffic_class

    @property
    def rdma_service_level(self) -> str:
        return self._rdma_service_level

    @rdma_service_level.setter
    def rdma_service_level(self, rdma_service_level: int):
        check_uint32("rdma_service_level", rdma_service_level)
        self._rdma_service_level = rdma_service_level

    @property
    def local_comm_res(self):
        return "" if self._local_comm_res is None else self._local_comm_res

    @local_comm_res.setter
    def local_comm_res(self, local_comm_res):
        check_isinstance("local_comm_res", local_comm_res, str)
        self._local_comm_res = local_comm_res
