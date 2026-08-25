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

import ctypes
import logging
import os

import hixl

logger = logging.getLogger(__name__)


def get_device_lists():
    """从 ASCEND_RT_VISIBLE_DEVICES 解析设备数量，返回错位的设备列表。

    ASCEND_RT_VISIBLE_DEVICES 指定可见的物理卡，进程内 set_device 使用逻辑 ID（从 0 开始）。
    Client 使用所有设备 [0, 1, 2, 3]，Server 使用错位后的设备 [1, 2, 3, 0]，
    确保 client[i] 和 server[i] 在不同的物理设备上，避免资源冲突。

    例如：环境变量 "0,1,2,3" -> client=[0,1,2,3], server=[1,2,3,0]
    """
    visible = os.environ.get("ASCEND_RT_VISIBLE_DEVICES", "0,1,2,3")
    devs = [int(x.strip()) for x in visible.split(",") if x.strip()]
    n = len(devs)
    client_devs = list(range(n))
    server_devs = [(i + 1) % n for i in range(n)]
    return client_devs, server_devs


def suppress_noisy_loggers():
    """抑制 torch._inductor 等 noisy logger，在子进程 worker 中调用"""
    logging.getLogger("torch._inductor").setLevel(logging.WARNING)
    logging.getLogger("torch").setLevel(logging.WARNING)


TRANSFER_SIZE = 64 * 1024
CONNECT_TIMEOUT_MS = 10000
TRANSFER_TIMEOUT_MS = 30000

BASE_PORT = 39000


def get_port(scenario_idx, dev_id, role="server"):
    """生成端口号

    支持复合 role 名称，用于区分不同引擎类型：
    - "server" / "client": 基础端口
    - "server_fabric" / "client_fabric": FabricMem 引擎端口 (+200)
    - "server_hccs" / "client_hccs": HCCS 引擎端口 (+200)

    端口分配示例（SCENARIO_IDX=3, dev_id=0）：
    - "server" → 39300
    - "client" → 39400
    - "server_fabric" → 39500
    - "client_fabric" → 39600
    """
    is_server = "server" in role or "remote" in role
    role_offset = 0 if is_server else 100
    engine_offset = 200 if any(x in role for x in ["fabric", "hccs", "alt"]) else 0
    return BASE_PORT + scenario_idx * 100 + role_offset + dev_id + engine_offset


def get_endpoint(scenario_idx, dev_id, role="server"):
    return f"127.0.0.1:{get_port(scenario_idx, dev_id, role)}"


def get_npu_count():
    try:
        import torch

        if hasattr(torch, "npu") and torch.npu.is_available():
            return torch.npu.device_count()
    except ImportError:
        pass
    import subprocess

    try:
        result = subprocess.run(
            ["npu-smi", "info", "-l"], capture_output=True, text=True, timeout=5
        )
        if result.returncode == 0:
            return sum(
                1
                for line in result.stdout.splitlines()
                if "NPU" in line and "Name" in line
            )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass
    return 0


def set_device(device_id):
    try:
        import torch

        torch.npu.set_device(device_id)
        return
    except ImportError:
        pass
    for lib_name in ["libascendcl.so", "libascendcl_stub.so"]:
        try:
            lib = ctypes.CDLL(lib_name)
            ret = lib.aclrtSetDevice(ctypes.c_int32(device_id))
            if ret == 0:
                return
        except OSError:
            continue
    raise RuntimeError(
        f"Cannot set device {device_id}: install torch_npu or CANN toolkit"
    )


def alloc_device_mem(size):
    import torch

    tensor = torch.zeros(size, dtype=torch.uint8, device="npu")
    addr = tensor.data_ptr()
    return addr, size, tensor


def verify_data(tensor, expected_byte):
    """验证 tensor 中所有字节是否等于 expected_byte"""
    cpu_tensor = tensor.cpu()
    return (cpu_tensor == expected_byte).all().item()


def create_engine(device_id, endpoint, options=None):
    set_device(device_id)
    engine = hixl.Hixl()
    if options is None:
        options = {
            hixl.OPTION_AUTO_CONNECT: "1",
            hixl.OPTION_GLOBAL_RESOURCE_CONFIG: '{"comm_resource_config.protocol_desc": ["roce:device", "roce:host"]}',
        }
    ret = engine.initialize(endpoint, options)
    if ret != hixl.SUCCESS:
        raise RuntimeError(
            f"Initialize failed: device={device_id}, endpoint={endpoint}, ret={ret}"
        )
    return engine


class RegisteredMem:
    def __init__(self, engine, addr, size, mem_type, buf=None):
        self.engine = engine
        self.addr = addr
        self.size = size
        self.mem_type = mem_type
        self.buf = buf  # 保存 buffer 引用，便于后续操作
        self.handle = 0
        ret, handle = engine.register_mem(hixl.MemDesc(addr, size), mem_type)
        if ret != hixl.SUCCESS:
            raise RuntimeError(f"RegisterMem failed: ret={ret}")
        self.handle = handle

    def __del__(self):
        if getattr(self, "handle", 0) != 0:
            try:
                self.deregister()
            except Exception as e:
                logger.warning("Failed to deregister memory in __del__: %s", e)

    def deregister(self):
        if self.handle != 0 and self.engine is not None:
            try:
                self.engine.deregister_mem(self.handle)
            except Exception as e:
                logger.warning("deregister_mem failed in deregister: %s", e)
            finally:
                self.handle = 0


def alloc_host_mem_pinned(size):
    """分配 pinned host memory（页对齐，可被 ACL 注册）

    Args:
        size: 内存大小（字节）

    Returns:
        tuple: (addr, size, tensor) - 内存地址、大小、torch tensor 对象
    """
    import torch

    tensor = torch.empty(size, dtype=torch.uint8).pin_memory()
    return tensor.data_ptr(), size, tensor


def build_op_descs(local_addr, remote_addr, size, block_size=16 * 1024):
    descs = []
    offset = 0
    while offset < size:
        chunk = min(block_size, size - offset)
        descs.append(
            hixl.TransferOpDesc(
                local_addr=local_addr + offset,
                remote_addr=remote_addr + offset,
                len=chunk,
            )
        )
        offset += chunk
    return descs
