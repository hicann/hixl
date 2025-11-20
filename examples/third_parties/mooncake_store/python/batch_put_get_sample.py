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

import time
import logging

from mooncake_sample_common import create_parser, setup_environment, validate_schema
from mooncake_sample_base import MooncakeSampleBase
from config import Config

logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)


class BatchPutGet(MooncakeSampleBase):
    def run(self):
        logging.info(f"Running batch put/get sample, schema: {self.args.schema}")
        
        self.store = self.init_mooncake_store()
        self.create_tensors()
        addr, remote_addr = self.register_buffers()
        
        for block_i in range(32):
            local_addrs = []
            remote_addrs = []
            sizes = []
            keys = []
            get_keys = []
            
            for layer in range(61):
                keys.append("hello_" + str(self.config.rank) + "_" + str(block_i) + "_" + str(layer))
                get_keys.append("hello_" + str((self.config.rank + 1) % self.config.world_size) + 
                                "_" + str(block_i) + "_" + str(layer))
                local_addrs.append(addr)
                remote_addrs.append(remote_addr)
                sizes.append(144 * 1024)
                addr += 144 * 1024
                remote_addr += 144 * 1024
            
            self.store.batch_put_from(keys, local_addrs, sizes)
            time.sleep(0.5)
            
            results = self.store.batch_get_into(get_keys, remote_addrs, sizes)
            self._show_results(get_keys, results)
        
        self.cleanup()
    
    def _show_results(self, keys, results):
        for k_, result_ in zip(keys, results):
            if result_ > 0:
                logging.info(f"Retrieved {k_} : {result_} bytes")
            else:
                logging.info(f"Failed to retrieve {k_}: error {result_}")
    

if __name__ == "__main__":
    parser = create_parser("Batch Put/Get Sample")
    
    # 创建配置对象并解析参数
    config = Config()
    args = config.parse_args(parser)
    args.schema = args.schema.lower()
    
    runner = BatchPutGet(args, config)
    validate_schema(args.schema)
    setup_environment(args)
    runner.init_process_group()
    runner.run()
