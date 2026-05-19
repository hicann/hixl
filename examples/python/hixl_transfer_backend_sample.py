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

import argparse
import os
import logging
import datetime
import torch
import torch.distributed as dist
import torch_npu
import torchair
from llm_datadist import LLMDataDist, LLMRole, LLMConfig, CacheDesc, DataType, BlocksCacheKey, \
    Placement, LLMClusterInfo, LLMStatusCode

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
    llm_config.transfer_backend = "hixl"
    if role == LLMRole.PROMPT:
        llm_config.listen_ip_info = f"{local_host_ip}:26000"
    else:
        llm_config.listen_ip_info = f"{local_host_ip}:26001"
    llm_options = llm_config.generate_options()
    datadist.init(llm_options)
    logging.info(f"init {role} success, cluster_id={cluster_id}")
    return datadist


def run_prompt_sample(datadist, local_host_ip, remote_host_ip):
    # 注册内存
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(num_tensors=NUM_TENSORS, shape=[BLOCKS_NUM, KV_SHAPE], data_type=DataType.DT_FLOAT,
                           placement=Placement.DEVICE)
    tensor = torch.ones(BLOCKS_NUM, KV_SHAPE, dtype=torch.float).npu()
    tensor2 = torch.ones(BLOCKS_NUM, KV_SHAPE, dtype=torch.float).npu()
    addr = int(tensor.data_ptr())
    addr2 = int(tensor2.data_ptr())
    cache = cache_manager.register_blocks_cache(cache_desc, [addr, addr2], BlocksCacheKey(PROMPT_CLUSTER_ID, 0))
    logging.info('register_blocks_cache success')

    dist.barrier() # register end

    # 传输前向对端发起建链
    cluster = LLMClusterInfo()
    cluster.remote_cluster_id = DECODER_CLUSTER_ID
    cluster.append_local_ip_info(local_host_ip, 26000)
    cluster.append_remote_ip_info(remote_host_ip, 26001)
    ret, _ = datadist.link_clusters([cluster], 5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("link failed")

    # 拉取内存
    cache_manager.pull_blocks(BlocksCacheKey(DECODER_CLUSTER_ID, 0), cache, src_blocks=[2], dst_blocks=[2])

    # 等对端推送完成
    dist.barrier()

    logging.info(f'after push and pull, tensor={tensor.cpu()}')
    logging.info(f'after push and pull, tensor2={tensor2.cpu()}')

    expect_tensor = torch.full((BLOCKS_NUM, KV_SHAPE), 0, dtype=torch.float).npu()
    if not torch.equal(expect_tensor, tensor):
        raise Exception(f"check tensor val failed, tensor={tensor.cpu()}, expect_tensor={expect_tensor.cpu()}")
    if not torch.equal(expect_tensor, tensor2):
        raise Exception(f"check tensor val failed, tensor2={tensor2.cpu()}, expect_tensor={expect_tensor.cpu()}")
    logging.info('check tensor val success')

    # 断链
    cluster = LLMClusterInfo()
    cluster.remote_cluster_id = DECODER_CLUSTER_ID
    ret, _ = datadist.unlink_clusters([cluster], 5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("unlink failed")

    # wait peer unlink end
    dist.barrier()

    cache_manager.unregister_cache(cache.cache_id)
    datadist.finalize()
    logging.info('[finalize] success')


def run_decoder_sample(datadist, local_host_ip, remote_host_ip):
    # 注册内存
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(num_tensors=NUM_TENSORS, shape=[BLOCKS_NUM, KV_SHAPE], data_type=DataType.DT_FLOAT,
                           placement=Placement.DEVICE)
    tensor = torch.full((BLOCKS_NUM, KV_SHAPE), 0, dtype=torch.float).npu()
    tensor2 = torch.full((BLOCKS_NUM, KV_SHAPE), 0, dtype=torch.float).npu()
    addr = int(tensor.data_ptr())
    addr2 = int(tensor2.data_ptr())
    cache = cache_manager.register_blocks_cache(cache_desc, [addr, addr2], BlocksCacheKey(DECODER_CLUSTER_ID, 0))
    logging.info('register_blocks_cache success')
    dist.barrier() # register end

    # 传输前向对端发起建链
    cluster = LLMClusterInfo()
    cluster.remote_cluster_id = PROMPT_CLUSTER_ID
    cluster.append_local_ip_info(local_host_ip, 26001)
    cluster.append_remote_ip_info(remote_host_ip, 26000)
    ret, _ = datadist.link_clusters([cluster], 5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("link failed")

    # 推送内存
    cache_manager.push_blocks(BlocksCacheKey(PROMPT_CLUSTER_ID, 0), cache, src_blocks=[0, 1], dst_blocks=[0, 1])

    dist.barrier() # push_blocks end

    # 断链
    cluster = LLMClusterInfo()
    cluster.remote_cluster_id = PROMPT_CLUSTER_ID
    ret, _ = datadist.unlink_clusters([cluster], 5000)
    if ret != LLMStatusCode.LLM_SUCCESS:
        raise Exception("unlink failed")

    # wait peer unlink end
    dist.barrier()

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
        run_prompt_sample(datadist, args.local_host_ip, args.remote_host_ip)
    else:
        run_decoder_sample(datadist, args.local_host_ip, args.remote_host_ip)
    logging.info('Sample end')
