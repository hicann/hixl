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
import json
import logging
import time
from llm_datadist import LLMDataDist, LLMRole, LLMConfig, CacheDesc, CacheKey, Cache, DataType, RegisterMemStatus, Placement
import torch
import torch_npu
import torchair

PROMPT_HOST_IP = '10.0.0.0'
PROMPT_IP_LIST = ['192.168.1.1', '192.168.1.2', '192.168.1.3', '192.168.1.4',
                  '192.168.1.5', '192.168.1.6', '192.168.1.7', '192.168.1.8']
DECODER_HOST_IP = '10.0.0.1'
DECODER_IP_LIST = ['192.168.2.1', '192.168.2.2', '192.168.2.3', '192.168.2.4',
                  '192.168.2.5', '192.168.2.6', '192.168.2.7', '192.168.2.8']

logging.basicConfig(format='%(asctime)s %(message)s', level=logging.INFO)


def init_llm_datadist(role: LLMRole, cluster_id, device_id: int) -> LLMDataDist:
    datadist = LLMDataDist(role, cluster_id)
    llm_config = LLMConfig()
    llm_config.device_id = device_id
    llm_config.enable_cache_manager = True
    llm_config.mem_pool_cfg = '{"memory_size": 1073741824}'
    llm_options = llm_config.generate_options()
    datadist.init(llm_options)
    return datadist


def link(datadist, device_id):
    rank_table_dict = {
        "server_count": "2",
        "status": "completed",
        "version": "1.2",
        "server_list": [
            {
                "device": [
                    {
                        "device_id": str(device_id),
                        "device_ip": PROMPT_IP_LIST[device_id],
                        "rank_id": "0"
                    }
                ],
                "host_ip": PROMPT_HOST_IP,
                "server_id": "1"
            },
            {
                "device": [
                    {
                        "device_id": str(device_id),
                        "device_ip": DECODER_IP_LIST[device_id],
                        "rank_id": "1"
                    }
                ],
                "host_ip": DECODER_HOST_IP,
                "server_id": "2"
            }
        ]
    }
    # 当前展示两个节点cluster id分别为1和2, rank id分别为0和1
    cluster_rank_info = {1: 0, 2: 1}
    rank_table = json.dumps(rank_table_dict)
    comm_id = datadist.link("link", cluster_rank_info, rank_table)
    while True:
        ret = datadist.query_register_mem_status(comm_id)
        if ret == RegisterMemStatus.OK:
            logging.info('query_register_mem_status ok')
            break
        elif ret == RegisterMemStatus.FAILED:
            logging.info('query_register_mem_status failed')
            raise RuntimeError("link failed")
        logging.info("need check again")
        time.sleep(1)
    return comm_id


def run_decoder_sample(datadist, device_id: int):
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(num_tensors=4, shape=[2, 1024 * 1024], data_type=DataType.DT_FLOAT16,
                           placement=Placement.DEVICE)
    tensors = []
    addrs = []
    for i in range(4):
        tensor = torch.ones(2, 1024 * 1024, dtype=torch.float16).npu()
        addrs.append(tensor.data_ptr())
        tensors.append(tensor)
    cache = cache_manager.register_blocks_cache(cache_desc, addrs)
    logging.info('[register_blocks_cache] success')

    comm_id = link(datadist, device_id)

    # wait prompt prepared
    time.sleep(5)
    cache_key = CacheKey(prompt_cluster_id=1, req_id=0, model_id=0)
    cache_manager.pull_blocks(cache_key, cache, src_blocks=[], dst_blocks=[0])
    logging.info(f"after pull, tensor={tensors[0].cpu()}")

    datadist.unlink(comm_id)
    datadist.finalize()


def run_prompt_sample(datadist, device_id: int):
    comm_id = link(datadist, device_id)
    # 通过cache_manager分配kv cache
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(num_tensors=4, shape=[2, 1024 * 1024], data_type=DataType.DT_FLOAT16,
                           placement=Placement.DEVICE)
    cache_key = CacheKey(prompt_cluster_id=1, req_id=0, model_id=0)
    cache = cache_manager.allocate_cache(cache_desc, [cache_key])
    logging.info('[allocate_cache] success')
    tensor_addrs = cache.tensor_addrs
    # 构造对应前端框架(如torch)的Tensor
    tensors = torchair.llm_datadist.create_npu_tensors(cache.cache_desc.shape, torch.float16, tensor_addrs)
    # 对cache进行赋值
    tensors[0].fill_(2)
    logging.info(f"prompt tensor={tensors[0].cpu()}")

    logging.info('wait for 30 seconds')
    time.sleep(30)
    logging.info('wait ended')
    # 如果pull_cache失败，或者decoder没有调用pull_cache，此处需要调用remove_cache_key，确保cache能够得到释放
    # 如果pull_cache成功，这里只是个空操作
    cache_manager.remove_cache_key(cache_key)
    logging.info('[remove_cache_key] success')
    cache_manager.deallocate_cache(cache)
    logging.info('[deallocate_cache] success')
    datadist.unlink(comm_id)
    datadist.finalize()
    logging.info('[finalize] success')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument("--device_id", type=int, default=0, help='device id')
    parser.add_argument("--cluster_id", type=int, default=1, help='cluster id')
    args = parser.parse_args()
    if args.cluster_id not in [1, 2]:
        raise RuntimeError("Not supported cluster id")
    if args.device_id not in [0, 1, 2, 3, 4, 5, 6, 7]:
        raise RuntimeError("Not supported device id")
    logging.info(f'Sample start, device_id = {args.device_id}, cluster_id = {args.cluster_id}')
    torch.npu.set_device(args.device_id)
    role = LLMRole.PROMPT if args.cluster_id == 1 else LLMRole.DECODER
    datadist = init_llm_datadist(role, args.cluster_id, args.device_id)
    if role == LLMRole.PROMPT:
        run_prompt_sample(datadist, args.device_id)
    else:
        run_decoder_sample(datadist, args.device_id)
    logging.info('Sample end')
