#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import os
import time
import logging
import torch
import torch_npu
import torch.distributed as dist
from mooncake.store import MooncakeDistributedStore

from mooncake_sample_common import SEGMENT_SIZE, LOCAL_BUFFER, ALIGNMENT
from config import Config


class MooncakeSampleBase:
    def __init__(self, args, config):
        self.args = args
        self.config = config
        self.store = None
        self.tensor = None
        self.target_tensor = None
        
    def init_process_group(self):
        if not self.config.distributed:
            logging.info("Running in single-machine mode")
            return
        
        os.environ["MASTER_ADDR"] = self.config.master_addr
        os.environ["MASTER_PORT"] = self.config.master_port
        
        dist.init_process_group(
            backend="gloo",
            rank=self.config.rank,
            world_size=self.config.world_size
        )
        dist.barrier(group=dist.group.WORLD)
        logging.info(f"Initialized distributed process group: \
        rank={self.config.rank}, world_size={self.config.world_size}")
    
    def init_mooncake_store(self) -> MooncakeDistributedStore:
        store = MooncakeDistributedStore()
        port = self.config.mooncake_store_port_start + self.config.rank
        store_ip = self.config.mooncake_store_ip + ":" + str(port)
        
        store.setup(
            store_ip,
            self.config.metadata_url,
            SEGMENT_SIZE,
            LOCAL_BUFFER,
            "ascend",
            "",
            self.config.grpc_url
        )
        logging.info(f"Initialized mooncake store: {store_ip}")
        return store
    
    def create_tensors(self):
        if self.args.schema.startswith("h"):
            self.tensor = torch.ones(33, 61, 144 * 1024, dtype=torch.int8, pin_memory=True).cpu()
        else:
            self.tensor = torch.ones(33, 61, 144 * 1024, dtype=torch.int8).npu()
        
        if self.args.schema.endswith("h"):
            self.target_tensor = torch.zeros(33, 61, 144 * 1024, dtype=torch.int8, pin_memory=True).cpu()
        else:
            self.target_tensor = torch.zeros(33, 61, 144 * 1024, dtype=torch.int8).npu()
    
    def register_buffers(self):
        data_ptr = self.tensor.data_ptr()
        addr = (data_ptr + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
        logging.info(f"dataptr:{data_ptr}, addr:{addr}")
        self.store.register_buffer(addr, 61 * 32 * 144 * 1024)

        target_data_ptr = self.target_tensor.data_ptr()
        remote_addr = (target_data_ptr + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
        logging.info(f"dataptr:{target_data_ptr}, addr:{remote_addr}")
        self.store.register_buffer(remote_addr, 61 * 32 * 144 * 1024)
        
        return addr, remote_addr
    
    def close_store(self):
        if self.store:
            self.store.close()
    
    def barrier(self):
        if self.config.distributed:
            dist.barrier(group=dist.group.WORLD)
