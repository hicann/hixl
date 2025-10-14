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
import os
from typing import Dict
from llm_datadist.configs import LLMRole
from llm_datadist.utils import log
from llm_datadist.utils.utils import check_isinstance
from llm_datadist.status import raise_if_false
from llm_datadist.configs import LlmConfig, trans_str_ip


class ClusterConfig:
    def __init__(self, rank_id_to_device_id: Dict[int, int]):
        self.rank_id_to_device_id = rank_id_to_device_id

    @classmethod
    def from_engine_options(cls, engine_options: Dict[str, str]) -> 'ClusterConfig':
        logical_device_id_to_rank_id = cls._parse_rank_mapping(engine_options)
        logical_device_id_to_device_id = cls._parse_numa_config(engine_options)
        local_rank_ids = []
        if 'ge.exec.rankId' in engine_options:
            rank_id = int(engine_options['ge.exec.rankId'])
            log.info(f'get rank_id by option ge.exec.rankId={rank_id}')
            local_rank_ids.append(rank_id)
        else:
            for logical_device_id in logical_device_id_to_device_id.keys():
                if logical_device_id in logical_device_id_to_rank_id:
                    rank_id = logical_device_id_to_rank_id[logical_device_id]
                    log.info(f'append rank_id = {rank_id}, logical_device_id = {logical_device_id}')
                    local_rank_ids.append(rank_id)
        rank_id_to_device_id: Dict[int, int] = {}
        for logical_device_id, rank_id in logical_device_id_to_rank_id.items():
            if rank_id in local_rank_ids:
                device_id = logical_device_id_to_device_id[logical_device_id]
                rank_id_to_device_id[rank_id] = device_id
        raise_if_false(len(rank_id_to_device_id) > 0,
                       f'rank_id_to_device_id is empty, '
                       f'logical_device_id_to_rank_id = {logical_device_id_to_rank_id}, '
                       f'logical_device_id_to_device_id = {logical_device_id_to_device_id}')
        return cls(rank_id_to_device_id)

    @staticmethod
    def _parse_rank_mapping(engine_options: Dict[str, str]) -> Dict[str, int]:
        raise_if_false('llm.ClusterInfo' in engine_options, "option 'llm.ClusterInfo' is not defined")
        cluster_info = json.loads(engine_options['llm.ClusterInfo'])
        logical_device_id_to_rank_id = {}
        if 'ge.exec.deviceId' in engine_options and 'ge.exec.rankId' in engine_options:
            rank_ids = [int(engine_options['ge.exec.rankId'])]
            log.info(f'both ge.exec.rankId and ge.exec.deviceId are defined, rank_id = {rank_ids[0]}')
        else:
            rank_ids = [i for i in range(len(cluster_info['logic_device_id']))]
        for rank_id, logical_device_id in zip(rank_ids, cluster_info['logic_device_id']):
            logical_device_id_to_rank_id[logical_device_id] = rank_id
        return logical_device_id_to_rank_id

    @staticmethod
    def _parse_numa_config(engine_options) -> Dict[str, int]:
        raise_if_false('ge.resourceConfigPath' in engine_options, "option 'ge.resourceConfigPath' is not defined")
        numa_config_path = engine_options['ge.resourceConfigPath']
        with open(numa_config_path) as f:
            numa_config = json.load(f)
        logical_device_id_to_device_id: Dict[str, int] = {}
        for custer_idx, cluster in enumerate(numa_config['cluster']):
            for node_idx, cluster_node in enumerate(cluster['cluster_nodes']):
                is_local = cluster_node.get('is_local', False)
                if not is_local:
                    continue
                for item_idx, item in enumerate(cluster_node['item_list']):
                    device_id = int(item['item_id'])
                    logical_device_id = ':'.join([str(custer_idx), str(node_idx), str(item_idx), '0'])
                    logical_device_id_to_device_id[logical_device_id] = device_id
        return logical_device_id_to_device_id


class EngineConfig:
    def __init__(self, is_prompt: bool, cluster_config: ClusterConfig) -> None:
        self.is_prompt = is_prompt
        self.cluster_config = cluster_config

    @classmethod
    def from_engine_options(cls, is_prompt: bool, engine_options: Dict[str, str]) -> 'EngineConfig':
        cluster_config = ClusterConfig.from_engine_options(engine_options)
        return cls(is_prompt, cluster_config)

    @staticmethod
    def gen_numa_config(device_id, deploy_res_path: str) -> str:
        node_type = 'FakeNodeType'
        item_type = 'FakeItemType'
        item_list = []
        for dev_id in device_id.split(";"):
            item_list.append({'item_id': int(dev_id), 'device_id': int(dev_id), 'ipaddr': '192.168.0.1'})
        numa_config = {
            'cluster': [
                {
                    'cluster_nodes': [
                        {
                            'node_id': 0,
                            'node_type': node_type,
                            'ipaddr': '127.0.0.1',
                            'port': -1,
                            'is_local': True,
                            'data_panel': {
                                'avail_ports': '65000~65535'
                            },
                            'item_list': item_list
                        }
                    ],
                }
            ],
            'node_def': [
                {
                    'node_type': node_type,
                    'resource_type': 'Aarch',
                    'support_links': '[HCCS,PCIE,ROCE]',
                    'item_type': item_type
                }
            ],
            'item_def': [
                {
                    'item_type': item_type,
                    'resource_type': 'Ascend',
                    'memory': '[DDR:64GB]',
                    'aic_type': '[FakeAicType]',
                }
            ]
        }
        if deploy_res_path is not None:
            check_isinstance('llm.deployResPath', deploy_res_path, str)
            numa_config['cluster'][0]['cluster_nodes'][0]['deploy_res_path'] = deploy_res_path
        return json.dumps(numa_config)

    @staticmethod
    def gen_cluster_info_if_not_exist(cluster_id: int, role: LLMRole, engine_options: Dict[str, str]) -> None:
        if 'llm.ClusterInfo' in engine_options:
            return
        device_num = len(engine_options["ge.exec.deviceId"].split(";"))
        cluster_info = {
            'cluster_id': cluster_id,
            'logic_device_id': [f'0:0:{i}:0' for i in range(device_num)]
        }
        if role == LLMRole.PROMPT:
            raise_if_false('llm.listenIpInfo' in engine_options,
                           'neither llm.ClusterInfo nor llm.listenIp was specified')
            listen_ip_info = engine_options['llm.listenIpInfo']
            check_isinstance('listen_ip_info', listen_ip_info, [str])
            sub_ip_infos = listen_ip_info.split(";")
            raise_if_false(len(sub_ip_infos) == device_num,
                           f'listen ip info num:{len(sub_ip_infos)} in '
                           f'llm.listenIpInfo is not equal to device num:{device_num}.')
            cluster_info['listen_ip_info'] = []
            for sub_ip_info in sub_ip_infos:
                ip_and_port = sub_ip_info.split(':')
                raise_if_false(len(ip_and_port) == 2,
                               f'llm.listenIpInfo "{ip_and_port}" is invalid, format should be "ip:port"')
                cluster_info['listen_ip_info'].append({'ip': ip_and_port[0], 'port': int(ip_and_port[1])})
        llm_config = LlmConfig()
        llm_config.cluster_info = json.dumps(cluster_info)
        converted_options = llm_config.gen_options()
        engine_options['llm.ClusterInfo'] = converted_options['llm.ClusterInfo']
        # create numa config if needed
        if 'RESOURCE_CONFIG_PATH' in os.environ:
            numa_config_path = os.getenv('RESOURCE_CONFIG_PATH', "")
        else:
            raise_if_false('ge.exec.deviceId' in engine_options,
                           'neither llm.ClusterInfo nor ge.exec.deviceId was specified')
            device_id = engine_options['ge.exec.deviceId']
            check_isinstance('ge.exec.deviceId', device_id, [str])
            for dev_id in device_id.split(";"):
                raise_if_false(dev_id.isdigit(),
                               f'ge.exec.deviceId is invalid, value="{device_id}",'
                               f' it should be composed of numbers, separated by semicolons.')
            raise_if_false(len(device_id.split(";")) > 0, f'ge.exec.deviceId is invalid, value="{device_id}",'
                                                          f' At least one device id is required.')
            deploy_res_path = engine_options.get('llm.deployResPath', None)
            numa_config_str = EngineConfig.gen_numa_config(device_id, deploy_res_path)
            numa_config_path = f'/tmp/stub_numa_config_{role.name.lower()}_{"_".join(device_id.split(";"))}.json'
            with open(numa_config_path, 'w') as f:
                f.write(numa_config_str)
        engine_options['ge.resourceConfigPath'] = numa_config_path
        log.info('using numa config: %s', numa_config_path)

    @staticmethod
    def parse_listen_ip_info(listen_ip_info: str) -> tuple[int, int]:
        check_isinstance('listen_ip_info', listen_ip_info, [str])
        ip_and_port = listen_ip_info.split(':')
        raise_if_false(len(ip_and_port) == 2,
                       f'llm.listenIpInfo "{listen_ip_info}" is invalid, format should be "ip:port"')
        ip_int = trans_str_ip(ip_and_port[0])
        port = int(ip_and_port[1])
        return ip_int, port
