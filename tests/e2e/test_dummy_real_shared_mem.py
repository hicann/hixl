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

import logging
import multiprocessing
import time
from dataclasses import dataclass

import hixl

from .utils import (
    CONNECT_TIMEOUT_MS,
    TRANSFER_SIZE,
    TRANSFER_TIMEOUT_MS,
    RegisteredMem,
    alloc_device_mem,
    alloc_host_mem_pinned,
    build_op_descs,
    create_engine,
    get_device_lists,
    get_endpoint,
    set_device,
    suppress_noisy_loggers,
)

logger = logging.getLogger(__name__)

# 场景索引，用于端口分配
SCENARIO_IDX = 1
# 每次传输的内存大小
MEM_SIZE = TRANSFER_SIZE
# Remote Dummy 填充的数据模式
REMOTE_BYTE = 0xBB
# Local Dummy 填充的数据模式
LOCAL_BYTE = 0xAA
# Remote Real host memory 填充的数据模式
REMOTE_HOST_BYTE = 0xCC

# ACL IPC memory key 长度（固定值）
IPC_KEY_LEN = 65
# ACL_RT_IPC_MEM_EXPORT_FLAG_DISABLE_PID_VALIDATION: 跳过 PID 校验
# 需要 driver >= 26.1.1
IPC_EXPORT_DISABLE_PID = 0x1
# ACL_RT_IPC_MEM_IMPORT_FLAG_DEFAULT: 默认导入模式
IPC_IMPORT_DEFAULT = 0x0

VERIFY_SETTLE_DELAY_S = 2
PHASE_TRANSITION_DELAY_S = 5
STARTUP_SETTLE_DELAY_S = 10


def _init_acl():
    """初始化 ACL 运行时环境"""
    import acl

    acl.init()


def _export_ipc_key(addr, size):
    """导出 device memory 的 IPC key

    使用 ACL_RT_IPC_MEM_EXPORT_FLAG_DISABLE_PID_VALIDATION 标志，
    跳过 PID 校验，允许任意进程导入。

    Args:
        addr: device memory 地址
        size: 内存大小（字节）

    Returns:
        str: IPC key 字符串

    Raises:
        RuntimeError: 导出失败时抛出
    """
    from acl import rt as acl_rt

    key, ret = acl_rt.ipc_mem_get_export_key(
        addr, size, IPC_KEY_LEN, IPC_EXPORT_DISABLE_PID
    )
    if ret != 0:
        raise RuntimeError(f"ipc_mem_get_export_key failed: ret={ret}")
    return key


def _import_ipc_key(key):
    """导入 IPC key，获取 device memory 的映射地址

    Args:
        key: IPC key 字符串

    Returns:
        int: 映射后的 device memory 地址

    Raises:
        RuntimeError: 导入失败时抛出
    """
    from acl import rt as acl_rt

    addr, ret = acl_rt.ipc_mem_import_by_key(key, IPC_IMPORT_DEFAULT)
    if ret != 0:
        raise RuntimeError(f"ipc_mem_import_by_key failed: ret={ret}")
    return addr


def _close_ipc_key(key):
    """关闭 IPC key，释放资源

    Args:
        key: IPC key 字符串
    """
    from acl import rt as acl_rt

    acl_rt.ipc_mem_close(key)


def _remote_dummy_worker(ready_event, key_queue, d2rd_write_done_event, stop_event):
    """Remote Dummy 进程工作函数

    职责：
    1. 为每个 remote 设备分配 device memory
    2. 填充 REMOTE_BYTE 模式数据
    3. 导出 IPC key（使用 DISABLE_PID_VALIDATION 标志）
    4. 通过 Queue 传递 IPC keys 给 Remote Real
    5. 等待 D2RD WRITE 完成后验证 Remote device memory 包含 LOCAL_BYTE

    参数:
        ready_event: 就绪信号
        key_queue: 传递 IPC keys 的队列
        d2rd_write_done_event: D2RD WRITE 完成信号
        stop_event: 停止信号
    """
    logging.basicConfig(
        format="%(asctime)s [S2-R-DUMMY] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _init_acl()
    _, remote_devs = get_device_lists()
    keys = []
    bufs = []
    try:
        # 为每个 remote 设备分配并导出 IPC key
        for dev_id in remote_devs:
            set_device(dev_id)
            addr, size, buf = alloc_device_mem(MEM_SIZE)
            buf.fill_(REMOTE_BYTE)
            key = _export_ipc_key(addr, MEM_SIZE)
            keys.append(key)
            bufs.append(buf)

        key_queue.put(keys)
        ready_event.set()

        # 等待 D2RD WRITE 完成并验证 Remote device memory 被写入 LOCAL_BYTE
        assert d2rd_write_done_event.wait(timeout=120), "D2RD WRITE verify timeout"
        time.sleep(VERIFY_SETTLE_DELAY_S)
        for i, buf in enumerate(bufs):
            cpu = buf.cpu()
            assert (cpu == LOCAL_BYTE).all().item(), (
                f"D2RD WRITE verify failed: remote[{i}] expected 0x{LOCAL_BYTE:02X}, got 0x{cpu[0].item():02X}"
            )

        # 等待停止信号
        stop_event.wait(timeout=300)
    except Exception as e:
        logger.error("Remote dummy error: %s", e)
        ready_event.set()
        key_queue.put([])
    finally:
        logger.info(f"[R-DUMMY] close IPC keys, count={len(keys)}")
        # 清理 IPC keys
        for key in keys:
            try:
                _close_ipc_key(key)
            except Exception as e:
                logger.warning("Failed to close IPC key: %s", e)
        logger.info("[R-DUMMY] cleanup done")


def _remote_real_worker(
    dummy_keys, ready_event, addr_queue, d2rh_write_done_event, stop_event
):
    """Remote Real 进程工作函数

    职责：
    1. 使能 P2P 访问（local_devs <-> remote_devs）
    2. 分配一大块 pinned host memory（MEM_SIZE * N），填充 REMOTE_HOST_BYTE
    3. 导入 Remote Dummy 的 device memory（通过 IPC key）
    4. 创建 N 个 HIXL engines，每个 engine 注册：
       - 导入的 device memory（对应的一段）
       - 整块 host memory
    5. 通过 Queue 传递 device_addrs 和 host_base_addr 给 Local Real
    6. 等待 D2RH WRITE 完成后验证 host memory 包含 LOCAL_BYTE

    参数:
        dummy_keys: Remote Dummy 导出的 IPC keys
        ready_event: 就绪信号
        addr_queue: 传递地址信息的队列
        d2rh_write_done_event: D2RH WRITE 完成信号
        stop_event: 停止信号
    """
    logging.basicConfig(
        format="%(asctime)s [S2-R-REAL] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _init_acl()
    local_devs, _ = get_device_lists()
    _, remote_devs = get_device_lists()
    n = len(remote_devs)
    engines = []
    dev_mems = []
    host_mems = []
    host_buf = None
    try:
        # 分配一大块 pinned host memory
        total_size = MEM_SIZE * n
        host_base_addr, _, host_buf = alloc_host_mem_pinned(total_size)
        host_buf.fill_(REMOTE_HOST_BYTE)

        # 为每个 remote 设备创建 engine 并注册内存
        imported_addrs = []
        for i, dev_id in enumerate(remote_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "server")
            eng = create_engine(dev_id, ep)
            engines.append(eng)

            # 导入 Remote Dummy 的 device memory
            mapped_addr = _import_ipc_key(dummy_keys[i])
            imported_addrs.append(mapped_addr)

            # 注册导入的 device memory
            dm = RegisteredMem(eng, mapped_addr, MEM_SIZE, hixl.MEM_DEVICE)
            dev_mems.append(dm)

            # 注册整块 host memory
            hm = RegisteredMem(eng, host_base_addr, total_size, hixl.MEM_HOST)
            host_mems.append(hm)

        # 传递地址信息给 Local Real
        addr_queue.put(
            {
                "device_addrs": {i: imported_addrs[i] for i in range(n)},
                "host_base_addr": host_base_addr,
            }
        )
        ready_event.set()

        # 等待 D2RH WRITE 完成并验证 host memory 被写入 LOCAL_BYTE
        assert d2rh_write_done_event.wait(timeout=120), "D2RH WRITE verify timeout"
        time.sleep(VERIFY_SETTLE_DELAY_S)
        for i in range(n):
            region = host_buf[i * MEM_SIZE : (i + 1) * MEM_SIZE]  # noqa: E203
            assert (region == LOCAL_BYTE).all().item(), (
                f"D2RH WRITE verify failed: host[{i}] expected 0x{LOCAL_BYTE:02X}, got 0x{region[0].item():02X}"
            )

        stop_event.wait(timeout=300)
    except Exception as e:
        logger.error("Remote real error: %s", e)
        ready_event.set()
        addr_queue.put({})
    finally:
        logger.info(f"[R-REAL] deregister dev_mems, count={len(dev_mems)}")
        # 清理资源
        for dm in dev_mems:
            dm.deregister()
        logger.info(f"[R-REAL] deregister host_mems, count={len(host_mems)}")
        for hm in host_mems:
            hm.deregister()
        logger.info(f"[R-REAL] finalize engines, count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[R-REAL] cleanup done")


@dataclass
class LocalDummyArgs:
    """Local Dummy 进程参数"""

    ready_event: multiprocessing.Event
    key_queue: multiprocessing.Queue
    d2rd_done_event: multiprocessing.Event
    d2rh_done_event: multiprocessing.Event
    result_queue: multiprocessing.Queue
    stop_event: multiprocessing.Event


@dataclass
class LocalRealArgs:
    """Local Real 进程参数"""

    dummy_keys: list
    remote_info: dict
    d2rd_write_done_event: multiprocessing.Event
    d2rd_done_event: multiprocessing.Event
    d2rh_write_done_event: multiprocessing.Event
    d2rh_done_event: multiprocessing.Event
    result_queue: multiprocessing.Queue


def _local_dummy_worker(args: LocalDummyArgs):
    """Local Dummy 进程工作函数

    职责：
    1. 为每个 local 设备分配 device memory
    2. 填充 LOCAL_BYTE 模式数据
    3. 导出 IPC key（使用 DISABLE_PID_VALIDATION 标志）
    4. 通过 Queue 传递 IPC keys 给 Local Real
    5. 等待 D2RD/D2RH 完成后验证数据

    验证逻辑：
    - D2RD: WRITE 后 Local 清零，READ 回 LOCAL_BYTE
    - D2RH: WRITE 后 Local 清零，READ 回 LOCAL_BYTE
    """
    logging.basicConfig(
        format="%(asctime)s [S2-L-DUMMY] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _init_acl()
    local_devs, _ = get_device_lists()
    keys = []
    bufs = []
    try:
        for dev_id in local_devs:
            set_device(dev_id)
            addr, size, buf = alloc_device_mem(MEM_SIZE)
            buf.fill_(LOCAL_BYTE)
            key = _export_ipc_key(addr, MEM_SIZE)
            keys.append(key)
            bufs.append(buf)

        args.key_queue.put(keys)
        args.ready_event.set()

        assert args.d2rd_done_event.wait(timeout=120), "D2RD verify timeout"
        time.sleep(VERIFY_SETTLE_DELAY_S)
        for i, buf in enumerate(bufs):
            cpu = buf.cpu()
            assert (cpu == LOCAL_BYTE).all().item(), (
                f"D2RD verify failed: local[{i}] expected 0x{LOCAL_BYTE:02X}, got 0x{cpu[0].item():02X}"
            )
        args.result_queue.put("d2rd_ok")

        assert args.d2rh_done_event.wait(timeout=120), "D2RH verify timeout"
        time.sleep(VERIFY_SETTLE_DELAY_S)
        for i, buf in enumerate(bufs):
            cpu = buf.cpu()
            assert (cpu == LOCAL_BYTE).all().item(), (
                f"D2RH verify failed: local[{i}] expected 0x{LOCAL_BYTE:02X}, got 0x{cpu[0].item():02X}"
            )
        args.result_queue.put("d2rh_ok")

        args.stop_event.wait(timeout=30)
    except Exception as e:
        logger.error("Local dummy error: %s", e)
        args.ready_event.set()
        args.key_queue.put([])
        args.result_queue.put(f"error: {e}")
    finally:
        logger.info(f"[L-DUMMY] close IPC keys, count={len(keys)}")
        for key in keys:
            try:
                _close_ipc_key(key)
            except Exception as e:
                logger.warning("Failed to close IPC key: %s", e)
        logger.info("[L-DUMMY] cleanup done")


def _local_real_worker(args: LocalRealArgs):
    """Local Real 进程工作函数

    职责：
    1. 使能 P2P 访问（local_devs <-> remote_devs）
    2. 分配一大块 pinned host memory（MEM_SIZE * N）
    3. 导入 Local Dummy 的 device memory（通过 IPC key）
    4. 创建 N 个 HIXL engines，每个 engine 注册：
       - 导入的 device memory（对应的一段）
       - 整块 host memory
    5. 连接 Remote Real
    6. D2RD WRITE: Local device → Remote device，然后清零 Local device
    7. D2RD READ: Remote device → Local device
    8. D2RH WRITE: Local device → Remote host，然后清零 Local device
    9. D2RH READ: Remote host → Local device
    """
    logging.basicConfig(
        format="%(asctime)s [S2-L-REAL] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _init_acl()
    local_devs, _ = get_device_lists()
    _, remote_devs = get_device_lists()
    n = len(local_devs)
    engines = []
    dev_mems = []
    host_mems = []
    imported_addrs = []
    host_buf = None
    try:
        total_size = MEM_SIZE * n
        host_base_addr, _, host_buf = alloc_host_mem_pinned(total_size)

        for i, dev_id in enumerate(local_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "client")
            eng = create_engine(dev_id, ep)
            engines.append(eng)

            mapped_addr = _import_ipc_key(args.dummy_keys[i])
            imported_addrs.append(mapped_addr)

            dm = RegisteredMem(eng, mapped_addr, MEM_SIZE, hixl.MEM_DEVICE)
            dev_mems.append(dm)

            hm = RegisteredMem(eng, host_base_addr, total_size, hixl.MEM_HOST)
            host_mems.append(hm)

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = engines[i].connect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[L-REAL] connect[{i}] dev{local_devs[i]}->remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"Connect local[{i}]->remote[{i}] failed: {ret}"

        remote_device_addrs = args.remote_info["device_addrs"]

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            op_descs = build_op_descs(
                imported_addrs[i], remote_device_addrs[i], MEM_SIZE
            )
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.WRITE,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[L-REAL] D2RD WRITE[{i}] dev{local_devs[i]}->remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"D2RD WRITE local[{i}] failed: {ret}"

        args.d2rd_write_done_event.set()
        time.sleep(PHASE_TRANSITION_DELAY_S)

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            op_descs = build_op_descs(
                imported_addrs[i], remote_device_addrs[i], MEM_SIZE
            )
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.READ,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[L-REAL] D2RD READ[{i}] dev{local_devs[i]}<-remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"D2RD READ local[{i}] failed: {ret}"

        args.d2rd_done_event.set()
        time.sleep(PHASE_TRANSITION_DELAY_S)

        remote_host_base = args.remote_info["host_base_addr"]

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = remote_host_base + i * MEM_SIZE
            op_descs = build_op_descs(imported_addrs[i], remote_addr, MEM_SIZE)
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.WRITE,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[L-REAL] D2RH WRITE[{i}] dev{local_devs[i]}->remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"D2RH WRITE local[{i}] failed: {ret}"

        args.d2rh_write_done_event.set()
        time.sleep(PHASE_TRANSITION_DELAY_S)

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = remote_host_base + i * MEM_SIZE
            op_descs = build_op_descs(imported_addrs[i], remote_addr, MEM_SIZE)
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.READ,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[L-REAL] D2RH READ[{i}] dev{local_devs[i]}<-remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, f"D2RH READ local[{i}] failed: {ret}"

        args.d2rh_done_event.set()
        time.sleep(PHASE_TRANSITION_DELAY_S)

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = engines[i].disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[L-REAL] disconnect[{i}] dev{local_devs[i]}->remote[{i}](dev{remote_devs[i]}): ret={ret}"
            )

        logger.info(f"[L-REAL] deregister dev_mems, count={len(dev_mems)}")
        for dm in dev_mems:
            dm.deregister()
        logger.info(f"[L-REAL] deregister host_mems, count={len(host_mems)}")
        for hm in host_mems:
            hm.deregister()
        logger.info(f"[L-REAL] finalize engines, count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[L-REAL] cleanup done")
        args.result_queue.put(True)
    except Exception as e:
        logger.error("Local real error: %s", e)
        logger.info(f"[L-REAL] disconnect engines, count={len(engines)}")
        for i, eng in enumerate(engines):
            try:
                remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
                eng.disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            except Exception as disconnect_err:
                logger.warning(
                    "Failed to disconnect engine[%d] in error handler: %s",
                    i,
                    disconnect_err,
                )
        logger.info(f"[L-REAL] deregister dev_mems, count={len(dev_mems)}")
        for dm in dev_mems:
            dm.deregister()
        logger.info(f"[L-REAL] deregister host_mems, count={len(host_mems)}")
        for hm in host_mems:
            hm.deregister()
        logger.info(f"[L-REAL] finalize engines, count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[L-REAL] cleanup done (error)")
        args.result_queue.put(False)


class TestMooncakeDummyReal:
    """Mooncake dummy-real 部署模式的端到端测试

    模拟 Dummy + Real 跨进程 device memory 共享场景：
    - Dummy 分配 device memory，通过 ACL IPC key 导出
    - Real 通过 IPC key 导入 device memory，注册到 HIXL engine
    - 设备均分：Local 使用前半，Remote 使用后半
    - 每个 Real 分配一大块 host memory，所有 engine 注册整块

    测试 D2RD（Device to Remote Device）和 D2RH（Device to Remote Host）
    两种传输路径的 WRITE + READ 完整流程。
    """

    @staticmethod
    def test_normal_full_flow_d2rd_d2rh_read():
        """测试 dummy-real D2RD + D2RH WRITE + READ 完整流程

        场景说明：
        模拟 Mooncake dummy-real 部署模式，验证跨进程 device memory 共享和 HIXL 传输。
        4 个进程协作完成 D2RD（Device to Remote Device）和 D2RH（Device to Remote Host）
        两种传输路径的端到端测试。

        进程架构：
        - Remote Dummy: 分配 device memory，填充 REMOTE_BYTE，导出 IPC key
        - Remote Real: 导入 IPC key，创建 engines，注册 host + device memory
        - Local Dummy: 分配 device memory，填充 LOCAL_BYTE，导出 IPC key
        - Local Real: 导入 IPC key，创建 engines，执行 D2RD/D2RH WRITE + READ

        测试步骤：
        1. 启动 Remote Dummy，分配 device memory 并导出 IPC key
        2. 启动 Remote Real，导入 IPC key，创建 engines，注册内存
        3. 启动 Local Dummy，分配 device memory 并导出 IPC key
        4. 启动 Local Real，导入 IPC key，创建 engines，连接 Remote Real
        5. D2RD WRITE: Local Real 将 Local device memory WRITE 到 Remote device memory
        6. Remote Dummy 验证 device memory 包含 LOCAL_BYTE
        7. Local Real 清零 Local device memory
        8. D2RD READ: Local Real 从 Remote device memory READ 到 Local device memory
        9. Local Dummy 验证 device memory 包含 LOCAL_BYTE
        10. D2RH WRITE: Local Real 将 Local device memory WRITE 到 Remote host memory
        11. Remote Real 验证 host memory 包含 LOCAL_BYTE
        12. Local Real 清零 Local device memory
        13. D2RH READ: Local Real 从 Remote host memory READ 到 Local device memory
        14. Local Dummy 验证 device memory 包含 LOCAL_BYTE
        15. 清理所有进程资源
        """
        ctx = multiprocessing.get_context("spawn")

        # 启动 Remote Dummy 进程
        r_dummy_ready = ctx.Event()
        r_dummy_keys_q = ctx.Queue()
        r_dummy_stop = ctx.Event()
        d2rd_write_done = ctx.Event()
        r_dummy = ctx.Process(
            target=_remote_dummy_worker,
            args=(
                r_dummy_ready,
                r_dummy_keys_q,
                d2rd_write_done,
                r_dummy_stop,
            ),
        )
        r_dummy.start()
        assert r_dummy_ready.wait(timeout=60), "Remote dummy startup timeout"
        r_dummy_keys = r_dummy_keys_q.get(timeout=10)
        assert r_dummy_keys, "Remote dummy failed to export IPC keys"

        # 启动 Remote Real 进程
        r_real_ready = ctx.Event()
        r_real_addrs_q = ctx.Queue()
        r_real_stop = ctx.Event()
        d2rh_write_done = ctx.Event()
        r_real = ctx.Process(
            target=_remote_real_worker,
            args=(
                r_dummy_keys,
                r_real_ready,
                r_real_addrs_q,
                d2rh_write_done,
                r_real_stop,
            ),
        )
        r_real.start()
        assert r_real_ready.wait(timeout=60), "Remote real startup timeout"
        remote_info = r_real_addrs_q.get(timeout=10)
        assert remote_info, "Remote real failed"

        time.sleep(STARTUP_SETTLE_DELAY_S)

        # 启动 Local Dummy 进程
        l_dummy_ready = ctx.Event()
        l_dummy_keys_q = ctx.Queue()
        l_dummy_stop = ctx.Event()
        d2rd_done = ctx.Event()
        d2rh_done = ctx.Event()
        verify_q = ctx.Queue()
        l_dummy_args = LocalDummyArgs(
            ready_event=l_dummy_ready,
            key_queue=l_dummy_keys_q,
            d2rd_done_event=d2rd_done,
            d2rh_done_event=d2rh_done,
            result_queue=verify_q,
            stop_event=l_dummy_stop,
        )
        l_dummy = ctx.Process(
            target=_local_dummy_worker,
            args=(l_dummy_args,),
        )
        l_dummy.start()
        assert l_dummy_ready.wait(timeout=60), "Local dummy startup timeout"
        l_dummy_keys = l_dummy_keys_q.get(timeout=10)
        assert l_dummy_keys, "Local dummy failed to export IPC keys"

        # 启动 Local Real 进程
        result_queue = ctx.Queue()
        l_real_args = LocalRealArgs(
            dummy_keys=l_dummy_keys,
            remote_info=remote_info,
            d2rd_write_done_event=d2rd_write_done,
            d2rd_done_event=d2rd_done,
            d2rh_write_done_event=d2rh_write_done,
            d2rh_done_event=d2rh_done,
            result_queue=result_queue,
        )
        l_real = ctx.Process(
            target=_local_real_worker,
            args=(l_real_args,),
        )
        l_real.start()
        l_real.join(timeout=180)

        # 验证 D2RD READ 结果
        d2rd_result = verify_q.get(timeout=30)
        assert d2rd_result == "d2rd_ok", f"D2RD verification: {d2rd_result}"

        # 验证 D2RH READ 结果
        d2rh_result = verify_q.get(timeout=30)
        assert d2rh_result == "d2rh_ok", f"D2RH verification: {d2rh_result}"

        # 清理所有进程
        r_real_stop.set()
        r_real.join(timeout=30)
        l_dummy_stop.set()
        l_dummy.join(timeout=30)
        r_dummy_stop.set()
        r_dummy.join(timeout=30)

        assert l_real.exitcode == 0, (
            f"Local real failed with exit code {l_real.exitcode}"
        )
        assert result_queue.get(timeout=10), "Local real worker failed"
