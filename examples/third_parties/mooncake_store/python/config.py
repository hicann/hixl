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
import argparse
import yaml


class Config:
    def __init__(self, config_file=None):
        self.config = {}
        self.distributed = False
        self.world_size = 1
        self.rank = 0
        self.master_addr = "127.0.0.1"
        self.master_port = "29500"
        self.mooncake_store_ip = "127.0.0.1"
        self.mooncake_store_port_start = 12345
        self.metadata_url = "http://127.0.0.1:8080/metadata"
        self.grpc_url = "127.0.0.1:50051"
        self.device_id = 0
        
        if config_file and os.path.exists(config_file):
            self.load_config(config_file)
    
    def load_config(self, config_file):
        with open(config_file, 'r') as f:
            self.config = yaml.safe_load(f)
        
        dist_config = self.config.get('distributed', {})
        self.distributed = dist_config.get('enabled', False)
        self.world_size = dist_config.get('world_size', 1)
        self.master_addr = dist_config.get('master_addr', "127.0.0.1")
        self.master_port = dist_config.get('master_port', "29500")
        
        mooncake_config = self.config.get('mooncake', {})
        self.mooncake_store_ip = mooncake_config.get('store_ip', "127.0.0.1")
        self.mooncake_store_port_start = mooncake_config.get('port_start', 12345)
        self.metadata_url = mooncake_config.get('metadata_url', "http://127.0.0.1:8080/metadata")
        self.grpc_url = mooncake_config.get('grpc_url', "127.0.0.1:50051")
    
    def parse_args(self, parser):
        args = parser.parse_args()
        
        if args.config:
            self.load_config(args.config)
        
        self.device_id = args.device_id
        
        if args.distributed:
            self.distributed = True
        
        if args.rank is not None:
            self.rank = args.rank
        else:
            self.rank = self.device_id // 2
        
        if args.world_size is not None:
            self.world_size = args.world_size
        
        return args
