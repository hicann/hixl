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

import argparse
import time
import logging
from llm_datadist import LLMDataDist, LLMRole, LLMConfig, CacheDesc, DataType, BlocksCacheKey, \
    Placement, LLMClusterInfo, LLMStatusCode
import torch
import torch_npu
import torchair
import socket
import struct

logging.basicConfig(format='%(asctime)s %(message)s', level=logging.INFO)

NUM_TENSORS = 1
BLOCKS_NUM = 3
KV_SHAPE = 10

def ip_port_to_int(ip_port):
    ip, port_str = ip_port.split(':')
    port = int(port_str)
    if not (0 <= port <= 65535):
        raise ValueError("端口号必须在0-65535之间")
    # 将IP转换为4字节二进制
    ip_bytes = socket.inet_aton(ip)

    # 将4字节IP转换为32位整数
    ip_int = struct.unpack('!I', ip_bytes)[0]

    # 组合IP整数(32位)和端口(16位)为一个48位整数
    result = (ip_int << 16) | port
    return result


def init_llm_datadist(args) -> LLMDataDist:
    datadist = LLMDataDist(role, ip_port_to_int(args.local_ip_port))
    llm_config = LLMConfig()
    llm_config.device_id = args.device_id
    llm_config.local_comm_res = ""
    if args.role == 'p':
        llm_config.listen_ip_info = args.local_ip_port
    llm_options = llm_config.generate_options()
    datadist.init(llm_options)
    logging.info(f"init {role} success, cluster_id={ip_port_to_int(args.local_ip_port)}")
    return datadist


def run_prompt_sample(datadist, args):
    # 1. 注册内存
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(num_tensors=NUM_TENSORS, shape=[BLOCKS_NUM, KV_SHAPE], data_type=DataType.DT_FLOAT,
                           placement=Placement.DEVICE)
    tensor = torch.full((BLOCKS_NUM, KV_SHAPE), ip_port_to_int(args.local_ip_port), dtype=torch.float).npu()

    addr = int(tensor.data_ptr())
    cache = cache_manager.register_blocks_cache(cache_desc, [addr],
                                                BlocksCacheKey(ip_port_to_int(args.local_ip_port), 0))
    logging.info('register_blocks_cache success')
    logging.info(f'before decoder pull, tensor={tensor.cpu()}')

    time.sleep(30)
    cache_manager.unregister_cache(cache.cache_id)
    datadist.finalize()
    logging.info('[finalize] success')


def run_decoder_sample(datadist, args):
    # 1. 注册内存
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(num_tensors=NUM_TENSORS, shape=[BLOCKS_NUM, KV_SHAPE], data_type=DataType.DT_FLOAT,
                           placement=Placement.DEVICE)
    remote_list = args.remote_ip_port.split(';')

    tensor = torch.full((BLOCKS_NUM, KV_SHAPE), 0, dtype=torch.float).npu()
    addr = int(tensor.data_ptr())
    cache = cache_manager.register_blocks_cache(cache_desc, [addr],
                                                BlocksCacheKey(ip_port_to_int(args.local_ip_port), 0))
    logging.info('register_blocks_cache success')

    time.sleep(5) # register end

    # 2. 向所有prompt建链
    cluster_list = []
    for remote in remote_list:
        cluster = LLMClusterInfo()
        cluster.remote_cluster_id = ip_port_to_int(remote)
        cluster.append_local_ip_info(args.local_ip_port.split(':')[0], 0)
        cluster.append_remote_ip_info(remote.split(':')[0], int(remote.split(':')[1]))
        cluster_list.append(cluster)
    ret, _ = datadist.link_clusters(cluster_list, 5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("link failed")

    # 3. 向prompt pull blocks
    for i, remote in enumerate(remote_list):
        cache_manager.pull_blocks(BlocksCacheKey(ip_port_to_int(remote), 0),
                                  cache, src_blocks=[0, 1], dst_blocks=[0, 2])
        logging.info(f'after decoder pull from {ip_port_to_int(remote)}, tensor={tensor.cpu()}')

    # 4. 断链
    ret, _ = datadist.unlink_clusters(cluster_list, 5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("unlink failed")

    cache_manager.unregister_cache(cache.cache_id)
    datadist.finalize()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--device_id", type=int, default=0, help='device id')
    parser.add_argument("--role", type=str, default=1, help='role type, support p/d')
    parser.add_argument("--local_ip_port", type=str, help='local ip port, eg:10.10.10.1:26000')
    parser.add_argument("--remote_ip_port", type=str,
                        help='remote host ip list, eg:10.10.10.2:26000;10.10.10.3:26000')
    args = parser.parse_args()
    if args.role not in ['p', 'd']:
        raise RuntimeError("Not supported cluster id")
    if args.device_id not in [0, 1, 2, 3, 4, 5, 6, 7]:
        raise RuntimeError("Not supported device id")
    if args.local_ip_port is None:
        raise RuntimeError("local_ip_port is not set")
    if args.role == 'd':
        if args.remote_ip_port is None:
            raise RuntimeError("remote_ip_port is not set")
    logging.info(f'Sample start, device_id = {args.device_id}, role = {args.role}')

    torch.npu.set_device(args.device_id)
    role = LLMRole.PROMPT if args.role == 'p' else LLMRole.DECODER
    datadist = init_llm_datadist(args)
    if role == LLMRole.PROMPT:
        run_prompt_sample(datadist, args)
    else:
        run_decoder_sample(datadist, args)
    logging.info('Sample end')
