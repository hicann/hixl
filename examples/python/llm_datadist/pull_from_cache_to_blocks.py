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

import argparse
import datetime
import json
import logging
import os
import re
import subprocess
import time

import torch
import torch.distributed as dist

from llm_datadist import (
    CacheDesc,
    CacheKey,
    DataType,
    LLMConfig,
    LLMDataDist,
    LLMRole,
    Placement,
    RegisterMemStatus,
)

logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)


PROMPT_HOST_IP = "10.10.10.0"
PROMPT_IP_LIST = [
    "192.168.1.1",
    "192.168.1.2",
    "192.168.1.3",
    "192.168.1.4",
    "192.168.1.5",
    "192.168.1.6",
    "192.168.1.7",
    "192.168.1.8",
]
DECODER_HOST_IP = "10.10.10.1"
DECODER_IP_LIST = [
    "192.168.2.1",
    "192.168.2.2",
    "192.168.2.3",
    "192.168.2.4",
    "192.168.2.5",
    "192.168.2.6",
    "192.168.2.7",
    "192.168.2.8",
]
CACHE_NUM_TENSORS = 4
CACHE_SHAPE = [2, 16 * 1024]
MEM_POOL_SIZE = 64 * 1024 * 1024


def init_process_group(cluster_id, is_single: bool, host_ip: str, backend="gloo"):
    master_ip = host_ip if is_single else PROMPT_HOST_IP
    if not master_ip:
        raise RuntimeError("host_ip is not set")
    os.environ["MASTER_ADDR"] = master_ip
    os.environ["MASTER_PORT"] = "29500"
    rank = cluster_id - 1
    logging.info(f"init group begin, rank={rank}, master_ip={master_ip}")
    dist.init_process_group(
        backend=backend,
        rank=rank,
        world_size=2,
        timeout=datetime.timedelta(seconds=30),
    )
    logging.info("init group success")


def init_llm_datadist(
    role, cluster_id, device_id: int, enable_mem_pool: bool = True
) -> LLMDataDist:
    datadist = LLMDataDist(role, cluster_id)
    llm_config = LLMConfig()
    llm_config.device_id = device_id
    llm_config.enable_cache_manager = True
    if enable_mem_pool:
        llm_config.mem_pool_cfg = f'{{"memory_size": {MEM_POOL_SIZE}}}'
    llm_options = llm_config.generate_options()
    datadist.init(llm_options)
    return datadist


def get_device_ip_from_hccn_tool(device_id: int) -> str:
    result = subprocess.run(
        ["hccn_tool", "-i", str(device_id), "-ip", "-g"],
        stdout=subprocess.PIPE,
        text=True,
    )
    ip = ""
    for line in result.stdout.splitlines():
        if line.startswith("ipaddr:"):
            ip = line.split(":")[1]
            break
    return ip


def get_physical_device_id() -> list[str]:
    numbers = []
    for name in os.listdir("/dev"):
        if re.match(r"^davinci\d+$", name):
            numbers.append(name.replace("davinci", ""))
    numbers.sort(key=int)
    return numbers


def _build_distributed_rank_table(device_id):
    return {
        "server_count": "2",
        "status": "completed",
        "version": "1.2",
        "server_list": [
            {
                "device": [
                    {
                        "device_id": str(device_id),
                        "device_ip": PROMPT_IP_LIST[device_id],
                        "rank_id": "0",
                    }
                ],
                "host_ip": PROMPT_HOST_IP,
                "server_id": "1",
            },
            {
                "device": [
                    {
                        "device_id": str(device_id),
                        "device_ip": DECODER_IP_LIST[device_id],
                        "rank_id": "1",
                    }
                ],
                "host_ip": DECODER_HOST_IP,
                "server_id": "2",
            },
        ],
    }


def _build_single_rank_table(host_ip: str):
    numbers = get_physical_device_id()
    return {
        "server_count": "1",
        "status": "completed",
        "version": "1.2",
        "server_list": [
            {
                "device": [
                    {
                        "device_id": numbers[0],
                        "device_ip": get_device_ip_from_hccn_tool(int(numbers[0])),
                        "rank_id": "0",
                    },
                    {
                        "device_id": numbers[1],
                        "device_ip": get_device_ip_from_hccn_tool(int(numbers[1])),
                        "rank_id": "1",
                    },
                ],
                "host_ip": host_ip,
                "server_id": "1",
            }
        ],
    }


def _build_rank_table(device_id, is_single: bool, host_ip: str):
    if is_single:
        return _build_single_rank_table(host_ip)
    return _build_distributed_rank_table(device_id)


def link(datadist, device_id, is_single: bool, host_ip: str):
    cluster_rank_info = {1: 0, 2: 1}
    rank_table = json.dumps(_build_rank_table(device_id, is_single, host_ip))
    comm_id = datadist.link("link", cluster_rank_info, rank_table)
    while True:
        ret = datadist.query_register_mem_status(comm_id)
        if ret == RegisterMemStatus.OK:
            logging.info("query_register_mem_status ok")
            return comm_id
        if ret == RegisterMemStatus.FAILED:
            logging.info("query_register_mem_status failed")
            raise RuntimeError("link failed")
        logging.info("need check again")
        time.sleep(1)


def run_decoder_sample(datadist, device_id: int, is_single: bool, host_ip: str):
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(
        num_tensors=CACHE_NUM_TENSORS,
        shape=CACHE_SHAPE,
        data_type=DataType.DT_FLOAT16,
        placement=Placement.DEVICE,
    )
    tensors = []
    addrs = []
    for _ in range(CACHE_NUM_TENSORS):
        tensor = torch.ones(*CACHE_SHAPE, dtype=torch.float16).npu()
        addrs.append(tensor.data_ptr())
        tensors.append(tensor)
    cache = cache_manager.register_blocks_cache(cache_desc, addrs)
    logging.info("[register_blocks_cache] success")

    comm_id = link(datadist, device_id, is_single, host_ip)

    dist.barrier()  # cache ready
    cache_key = CacheKey(prompt_cluster_id=1, req_id=0, model_id=0)
    cache_manager.pull_blocks(cache_key, cache, src_blocks=[], dst_blocks=[0])
    logging.info("[pull_blocks] success")

    dist.barrier()  # pull_blocks end
    datadist.unlink(comm_id)
    dist.barrier()  # wait peer unlink end
    datadist.finalize()


def run_prompt_sample(datadist, device_id: int, is_single: bool, host_ip: str):
    comm_id = link(datadist, device_id, is_single, host_ip)
    # 通过cache_manager分配kv cache
    cache_manager = datadist.cache_manager
    cache_desc = CacheDesc(
        num_tensors=CACHE_NUM_TENSORS,
        shape=CACHE_SHAPE,
        data_type=DataType.DT_FLOAT16,
        placement=Placement.DEVICE,
    )
    cache_key = CacheKey(prompt_cluster_id=1, req_id=0, model_id=0)
    cache = cache_manager.allocate_cache(cache_desc, [cache_key])
    logging.info("[allocate_cache] success")

    dist.barrier()  # cache ready
    dist.barrier()  # decoder pull_blocks end
    datadist.unlink(comm_id)
    dist.barrier()  # wait peer unlink end

    # 如果pull_cache失败，或者decoder没有调用pull_cache，此处需要调用remove_cache_key，确保cache能够得到释放
    # 如果pull_cache成功，这里只是个空操作
    cache_manager.remove_cache_key(cache_key)
    logging.info("[remove_cache_key] success")
    cache_manager.deallocate_cache(cache)
    logging.info("[deallocate_cache] success")
    datadist.finalize()
    logging.info("[finalize] success")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--device_id", type=int, default=0, help="device id")
    parser.add_argument("--cluster_id", type=int, default=1, help="cluster id")
    parser.add_argument("--is_single", type=str, help="whether run on a single machine")
    parser.add_argument("--host_ip", type=str, help="host ip")
    args = parser.parse_args()
    if args.cluster_id not in [1, 2]:
        raise RuntimeError("Not supported cluster id")
    if args.device_id not in [0, 1, 2, 3, 4, 5, 6, 7]:
        raise RuntimeError("Not supported device id")
    is_single = False
    if args.is_single:
        is_single = True
    logging.info(
        f"Sample start, device_id = {args.device_id}, cluster_id = {args.cluster_id}"
    )
    torch.npu.set_device(args.device_id)
    role = LLMRole.PROMPT if args.cluster_id == 1 else LLMRole.DECODER
    init_process_group(args.cluster_id, is_single, args.host_ip)
    datadist = init_llm_datadist(role, args.cluster_id, args.device_id)
    if role == LLMRole.PROMPT:
        run_prompt_sample(datadist, args.device_id, is_single, args.host_ip)
    else:
        run_decoder_sample(datadist, args.device_id, is_single, args.host_ip)
    logging.info("Sample end")
