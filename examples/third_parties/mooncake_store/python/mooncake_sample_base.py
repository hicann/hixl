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
import logging
import torch
import torch_npu
import torch.distributed as dist
from mooncake.store import MooncakeDistributedStore

from mooncake_sample_common import SEGMENT_SIZE, LOCAL_BUFFER, ALIGNMENT


class MooncakeSampleBase:
    def __init__(self, args):
        self.args = args
        self.store = None
        self.tensor = None
        self.target_tensor = None
        
    def init_process_group(self):
        os.environ["MASTER_ADDR"] = "master addr" # 运行时填入
        os.environ["MASTER_PORT"] = "29500"
        dist.init_process_group("gloo", rank=self.args.device_id % 2, world_size=2)
        dist.barrier(group=dist.group.WORLD)
    
    def init_mooncake_store(self) -> MooncakeDistributedStore:
        store = MooncakeDistributedStore()
        port = 12345 + int(self.args.device_id)
        store.setup(
            "mooncake_store_ip:" + str(port), # 运行时补充mooncake store ip
            "http://localhost:8080/metadata",
            SEGMENT_SIZE,
            LOCAL_BUFFER,
            "ascend",
            "",
            "localhost:50051"
        )
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
    
    def unregister_buffers(self):
        if self.tensor is not None:
            self.store.unregister_buffer(self.tensor.data_ptr())
        if self.target_tensor is not None:
            self.store.unregister_buffer(self.target_tensor.data_ptr())
    
    def close_store(self):
        if self.store:
            self.store.close()
    
    def cleanup(self):
        self.unregister_buffers()
        self.close_store()
