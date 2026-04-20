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

import os
import time
import logging
import torch
import torch_npu
import torch.distributed as dist
from mooncake.store import MooncakeDistributedStore

from mooncake_sample_common import SEGMENT_SIZE, LOCAL_BUFFER, HCCS_ALIGNMENT, FABRIC_ALIGNMENT
from config import Config


class MooncakeSampleBase:
    def __init__(self, args, config):
        self.args = args
        self.config = config
        self.store = None
        self.tensor = None
        self.target_tensor = None
        self.register_buffer_size = 32 * 61 * 144 * 1024
        
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
        logging.info(
            f"Initialized distributed process group: "
            f"rank={self.config.rank}, world_size={self.config.world_size}")
    
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
    
    def init_mooncake_dummy_store(self) -> MooncakeDistributedStore:
        store = MooncakeDistributedStore()
        real_client_address = getattr(self.args, 'real_client_address', '127.0.0.1:50052')
        mem_pool_size = getattr(self.args, 'mem_pool_size', SEGMENT_SIZE)
        local_buffer_size = getattr(self.args, 'local_buffer_size', LOCAL_BUFFER)

        logging.info(f"Initializing dummy client, connecting to {real_client_address}")
        store.setup_dummy(
            mem_pool_size,
            local_buffer_size,
            real_client_address
        )
        logging.info(f"Initialized mooncake dummy client, real client at: {real_client_address}")
        return store
    
    def create_tensors(self):
        use_dummy_mode = hasattr(self.args, 'use_dummy') and self.args.use_dummy
        if use_dummy_mode:
            if self.args.schema.startswith("h"):
                import ctypes
                tensor_size = 33 * 61 * 144 * 1024
                # Allocate memory from store's memory pool
                alloc_size = (tensor_size + FABRIC_ALIGNMENT - 1) // FABRIC_ALIGNMENT * FABRIC_ALIGNMENT
                tensor_ptr = self.store.alloc_from_mem_pool(alloc_size)
                self.register_buffer_size = alloc_size
                # Create torch tensors from the allocated pointers
                self.tensor = torch.frombuffer(
                    (ctypes.c_uint8 * tensor_size).from_address(tensor_ptr),
                    dtype=torch.int8
                ).reshape(33, 61, 144 * 1024)
                self.tensor.fill_(1)
            else:
                self.tensor = torch.ones(33, 61, 144 * 1024, dtype=torch.int8, pin_memory=True).npu()

            if self.args.schema.endswith("h"):
                import ctypes
                target_tensor_size = 33 * 61 * 144 * 1024
                # Allocate memory from store's memory pool
                alloc_size = (target_tensor_size + FABRIC_ALIGNMENT - 1) // FABRIC_ALIGNMENT * FABRIC_ALIGNMENT
                tensor_ptr = self.store.alloc_from_mem_pool(alloc_size)
                # Create torch tensors from the allocated pointers
                self.target_tensor = torch.frombuffer(
                    (ctypes.c_uint8 * tensor_size).from_address(tensor_ptr),
                    dtype=torch.int8
                ).reshape(33, 61, 144 * 1024)
                self.target_tensor.fill_(0)
            else:
                self.target_tensor = torch.zeros(33, 61, 144 * 1024, dtype=torch.int8, pin_memory=True).npu()

        else:
            # Embedded mode: use torch tensors
            if self.args.schema.startswith("h"):
                self.tensor = torch.ones(33, 61, 144 * 1024, dtype=torch.int8, pin_memory=True).cpu()
            else:
                self.tensor = torch.ones(33, 61, 144 * 1024, dtype=torch.int8).npu()

            if self.args.schema.endswith("h"):
                self.target_tensor = torch.zeros(33, 61, 144 * 1024, dtype=torch.int8, pin_memory=True).cpu()
            else:
                self.target_tensor = torch.zeros(33, 61, 144 * 1024, dtype=torch.int8).npu()
            logging.info("Embedded mode: created torch tensors")

    def register_buffers(self):
        data_ptr = self.tensor.data_ptr()
        use_dummy_mode = hasattr(self.args, 'use_dummy') and self.args.use_dummy
        if use_dummy_mode:
            addr = data_ptr
        else:
            addr = (data_ptr + HCCS_ALIGNMENT - 1) // HCCS_ALIGNMENT * HCCS_ALIGNMENT
        logging.info(f"dataptr:{data_ptr}, addr:{addr}")
        self.store.register_buffer(addr, self.register_buffer_size)

        target_data_ptr = self.target_tensor.data_ptr()
        if use_dummy_mode:
            remote_addr = target_data_ptr
        else:
            remote_addr = (target_data_ptr + HCCS_ALIGNMENT - 1) // HCCS_ALIGNMENT * HCCS_ALIGNMENT
        logging.info(f"dataptr:{target_data_ptr}, addr:{remote_addr}")
        self.store.register_buffer(remote_addr, self.register_buffer_size)
        
        return addr, remote_addr
    
    def close_store(self):
        if self.store:
            self.store.close()
    
    def barrier(self):
        if self.config.distributed:
            dist.barrier(group=dist.group.WORLD)
