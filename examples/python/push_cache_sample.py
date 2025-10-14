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
import os
import logging
import datetime
from llm_datadist import LLMDataDist, LLMRole, LLMConfig, CacheDesc, DataType, CacheKeyByIdAndIndex, \
    Placement, LLMClusterInfo, LLMStatusCode
import torch
import torch.distributed as dist
import torch_npu
import torchair

logging.basicConfig(format='%(asctime)s %(message)s', level=logging.INFO)

NUM_TENSORS = 2
BLOCKS_NUM = 3
KV_SHAPE = 10
PROMPT_CLUSTER_ID = 0
DECODER_CLUSTER_ID = 1

def init_process_group(rank, world_size, master_ip, backend='gloo'):
    os.environ['MASTER_ADDR'] = master_ip
    os.environ['MASTER_PORT'] = '29500'

    logging.info(f"init group begin, rank={rank}, world_size={world_size}, master_ip={master_ip}")
    dist.init_process_group(backend=backend, rank=rank, world_size=world_size, timeout=datetime.timedelta(seconds=30))
    logging.info(f"init group success")


def init_llm_datadist(role: LLMRole, cluster_id, device_id: int, local_host_ip, remote_host_ip) -> LLMDataDist:
    init_process_group(cluster_id, 2, min(local_host_ip, remote_host_ip))
    datadist = LLMDataDist(role, cluster_id)
    llm_config = LLMConfig()
    llm_config.device_id = device_id
    llm_config.local_comm_res = ""
    if role == LLMRole.PROMPT:
        llm_config.listen_ip_info = f"{local_host_ip}:26000"
    llm_options = llm_config.generate_options()
    datadist.init(llm_options)
    logging.info(f"init {role} success, cluster_id={cluster_id}")
    return datadist


def run_prompt_sample(datadist):
    # 1. 注册内存
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(num_tensors=NUM_TENSORS, shape=[BLOCKS_NUM, KV_SHAPE], data_type=DataType.DT_FLOAT,
                           placement=Placement.DEVICE)
    tensor = torch.full((BLOCKS_NUM, KV_SHAPE), 0, dtype=torch.float).npu()
    tensor2 = torch.full((BLOCKS_NUM, KV_SHAPE), 0, dtype=torch.float).npu()
    addr = int(tensor.data_ptr())
    addr2 = int(tensor2.data_ptr())
    cache = cache_manager.register_cache(cache_desc, [addr, addr2])
    logging.info('register_blocks_cache success')

    dist.barrier() # register end

    # 2. 等decoder push cache
    dist.barrier() # decoder push cache end

    logging.info(f'after decoder push, tensor={tensor.cpu()}')
    logging.info(f'after decoder push, tensor2={tensor2.cpu()}')

    dist.barrier() # wait unlink end

    cache_manager.unregister_cache(cache.cache_id)
    datadist.finalize()
    logging.info('[finalize] success')


def run_decoder_sample(datadist, local_host_ip, remote_host_ip):
    # 1. 注册内存
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(num_tensors=NUM_TENSORS, shape=[BLOCKS_NUM, KV_SHAPE], data_type=DataType.DT_FLOAT,
                           placement=Placement.DEVICE)
    tensor = torch.full((BLOCKS_NUM, KV_SHAPE), 1, dtype=torch.float).npu()
    tensor2 = torch.full((BLOCKS_NUM, KV_SHAPE), 1, dtype=torch.float).npu()
    addr = int(tensor.data_ptr())
    addr2 = int(tensor2.data_ptr())
    cache = cache_manager.register_cache(cache_desc, [addr, addr2])
    logging.info('register_blocks_cache success')
    dist.barrier() # register end

    # 2. 向prompt建链
    cluster = LLMClusterInfo()
    cluster.remote_cluster_id = PROMPT_CLUSTER_ID
    cluster.append_local_ip_info(local_host_ip, 26000)
    cluster.append_remote_ip_info(remote_host_ip, 26000)
    ret, _ = datadist.link_clusters([cluster], 5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("link failed")

    # 3. 向prompt push cache
    cache_manager.push_cache(CacheKeyByIdAndIndex(cluster_id=PROMPT_CLUSTER_ID, cache_id=1, batch_index=0), cache,
                             src_batch_index=0)

    dist.barrier() # push cache end

    # 4. 断链
    cluster = LLMClusterInfo()
    cluster.remote_cluster_id = PROMPT_CLUSTER_ID
    cluster.append_remote_ip_info(remote_host_ip, 26000)
    ret, _ = datadist.unlink_clusters([cluster], 5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("unlink failed")
    dist.barrier() # unlink end

    cache_manager.unregister_cache(cache.cache_id)
    datadist.finalize()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--device_id", type=int, default=0, help='device id')
    parser.add_argument("--role", type=str, default=1, help='role type, support p/d')
    parser.add_argument("--local_host_ip", type=str, help='local host ip')
    parser.add_argument("--remote_host_ip", type=str, help='remote host ip')
    args = parser.parse_args()
    if args.role not in ['p', 'd']:
        raise RuntimeError("Not supported cluster id")
    if args.device_id not in [0, 1, 2, 3, 4, 5, 6, 7]:
        raise RuntimeError("Not supported device id")
    if args.role == 'd':
        if args.local_host_ip is None:
            raise RuntimeError("local_host_ip is not set")
        if args.remote_host_ip is None:
            raise RuntimeError("remote_host_ip is not set")
    logging.info(f'Sample start, device_id = {args.device_id}, role = {args.role}')

    torch.npu.set_device(args.device_id)
    role = LLMRole.PROMPT if args.role == 'p' else LLMRole.DECODER
    cluster_id = PROMPT_CLUSTER_ID if args.role == 'p' else DECODER_CLUSTER_ID
    datadist = init_llm_datadist(role, cluster_id, args.device_id, args.local_host_ip, args.remote_host_ip)
    if role == LLMRole.PROMPT:
        run_prompt_sample(datadist)
    else:
        run_decoder_sample(datadist, args.local_host_ip, args.remote_host_ip)
    logging.info('Sample end')
