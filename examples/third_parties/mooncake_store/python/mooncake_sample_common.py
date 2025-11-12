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

import argparse
import logging
import torch
import torch_npu

SEGMENT_SIZE = 1024 * 1024 * 1024
LOCAL_BUFFER = 20 * 1024 * 1024
ALIGNMENT = 2 * 1024 * 1024
SUPPORTED_SCHEMA = ["h2h", "h2d", "d2h", "d2d"]


def create_parser(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--device_id", type=int, default=0, help="device id")
    parser.add_argument("--schema", type=str, default="d2d",
                        help="transport schema, should in ['h2h', 'h2d', 'd2h', 'd2d']")
    return parser


def setup_environment(args):
    torch.npu.set_device(args.device_id)
    logging.info(f"Running on device: {args.device_id}")


def validate_schema(schema):
    if schema not in SUPPORTED_SCHEMA:
        raise RuntimeError(f"Unsupported Schema: {schema}")
