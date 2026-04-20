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
import logging
import torch
import torch_npu

SEGMENT_SIZE = 1024 * 1024 * 1024
LOCAL_BUFFER = 20 * 1024 * 1024
HCCS_ALIGNMENT = 2 * 1024 * 1024
FABRIC_ALIGNMENT = 1024 * 1024 * 1024
SUPPORTED_SCHEMA = ["h2h", "h2d", "d2h", "d2d"]


def create_parser(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--schema", type=str, default="d2d",
                        help="transport schema, should in ['h2h', 'h2d', 'd2h', 'd2d']")
    parser.add_argument('--config', type=str, help='Path to config file')
    parser.add_argument('--device_id', type=int, required=True, help='Device ID (must be provided)')
    parser.add_argument('--rank', type=int, required=True, help='Rank ID (must be provided to sync process group)')
    parser.add_argument('--world_size', type=int, help='World size (optional, default: 1)')
    parser.add_argument('--distributed', action='store_true', help='Enable distributed mode')
    # Dummy client mode options
    parser.add_argument('--use_dummy', action='store_true',
                        help='Use dummy client mode to connect to standalone Real Client')
    parser.add_argument('--real_client_address', type=str, default='127.0.0.1:54000',
                        help='Real Client address for dummy mode (default: 127.0.0.1:54000)')
    parser.add_argument('--mem_pool_size', type=int, default=0,
                        help='Memory pool size in bytes for dummy mode (optional)')
    parser.add_argument('--local_buffer_size', type=int, default=0,
                        help='Local buffer size in bytes for dummy mode (optional)')
    return parser


def setup_environment(args):
    torch.npu.set_device(args.device_id)
    logging.info(f"Running on device: {args.device_id}")


def validate_schema(args):
    if args.schema not in SUPPORTED_SCHEMA:
        raise RuntimeError(f"Unsupported Schema: {args.schema}")
    if args.use_dummy and args.schema not in ["h2h", "d2d"]:
        raise RuntimeError(f"Only h2h and d2d supported for Dummy/Real Clients now.") 
