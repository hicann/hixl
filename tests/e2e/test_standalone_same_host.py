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
    suppress_noisy_loggers,
    verify_data,
)

logger = logging.getLogger(__name__)

SCENARIO_IDX = 7
MEM_SIZE = TRANSFER_SIZE
APP_BYTE = 0xFF


def _store_service_worker(ready_event, addr_queue, stop_event):
    """Store Service 进程工作函数

    使用均分后的后半设备，分配一大块 pinned host 内存（MEM_SIZE * N），
    创建 N 个 HIXL 引擎，每个引擎注册整块内存（同一 base_addr，同一 total_size）。
    通过 Queue 传递 {"base_addr": ..., "total_size": ...} 给 App。
    """
    logging.basicConfig(format="%(asctime)s [S3-STORE] %(message)s", level=logging.INFO)
    suppress_noisy_loggers()
    _, store_devs = get_device_lists()
    n = len(store_devs)
    engines = []
    mems = []
    host_buf = None
    try:
        total_size = MEM_SIZE * n
        base_addr, _, host_buf = alloc_host_mem_pinned(total_size)

        for i, dev_id in enumerate(store_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "server")
            eng = create_engine(dev_id, ep)
            engines.append(eng)
            rm = RegisteredMem(eng, base_addr, total_size, hixl.MEM_HOST)
            mems.append(rm)

        addr_queue.put({"base_addr": base_addr, "total_size": total_size})
        ready_event.set()
        stop_event.wait(timeout=300)
    except Exception as e:
        logger.error("Store error: %s", e)
        ready_event.set()
        addr_queue.put({})
    finally:
        logger.info(f"[STORE] deregister start, count={len(mems)}")
        for rm in mems:
            rm.deregister()
        logger.info("[STORE] deregister done")
        logger.info(f"[STORE] finalize start, count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[STORE] finalize done")


def _app_worker(store_info, result_queue):
    """App 进程工作函数

    使用均分后的前半设备，创建 N 个 HIXL 引擎，每个分配 device 内存并填充 APP_BYTE。
    identity 映射连接 app[i] -> store[i]，执行 WRITE + READ 验证。
    App[i] 写入 store 大内存的 i * MEM_SIZE 偏移处，避免多 engine 写同一段冲突。
    """
    logging.basicConfig(format="%(asctime)s [S3-APP] %(message)s", level=logging.INFO)
    suppress_noisy_loggers()
    app_devs, _ = get_device_lists()
    _, store_devs = get_device_lists()
    engines = []
    local_mems = []
    base_addr = store_info["base_addr"]
    try:
        for i, dev_id in enumerate(app_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "client")
            eng = create_engine(dev_id, ep)
            engines.append(eng)
            addr, size, buf = alloc_device_mem(MEM_SIZE)
            buf.fill_(APP_BYTE)
            rm = RegisteredMem(eng, addr, size, hixl.MEM_DEVICE)
            local_mems.append((rm, buf))

        for i, (eng, dev_id, store_dev) in enumerate(
            zip(engines, app_devs, store_devs)
        ):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = eng.connect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[APP] connect[{i}] dev{dev_id}->store[{i}](dev{store_dev}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"Connect app[{i}] dev{dev_id}->store[{i}](dev{store_dev}) failed: {ret}"
            )

        for i, (eng, (rm, _), dev_id, store_dev) in enumerate(
            zip(engines, local_mems, app_devs, store_devs)
        ):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = base_addr + i * MEM_SIZE
            local_addr = rm.addr
            op_descs = build_op_descs(local_addr, remote_addr, MEM_SIZE)
            ret = eng.transfer_sync(
                remote_ep,
                hixl.TransferOp.WRITE,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[APP] WRITE[{i}] dev{dev_id}->store[{i}](dev{store_dev}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"WRITE app[{i}] dev{dev_id}->store[{i}](dev{store_dev}) failed: {ret}"
            )

        for i, (eng, (rm, local_tensor), dev_id, store_dev) in enumerate(
            zip(engines, local_mems, app_devs, store_devs)
        ):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = base_addr + i * MEM_SIZE
            local_addr = rm.addr
            local_tensor.fill_(0x00)
            op_descs = build_op_descs(local_addr, remote_addr, MEM_SIZE)
            ret = eng.transfer_sync(
                remote_ep,
                hixl.TransferOp.READ,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[APP] READ[{i}] dev{dev_id}<-store[{i}](dev{store_dev}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"READ app[{i}] dev{dev_id}<-store[{i}](dev{store_dev}) failed: {ret}"
            )
            assert verify_data(local_tensor, APP_BYTE), (
                f"Data mismatch after READ app[{i}] dev{dev_id}<-store[{i}](dev{store_dev})"
            )

        for i, (eng, dev_id, store_dev) in enumerate(
            zip(engines, app_devs, store_devs)
        ):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = eng.disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[APP] disconnect[{i}] dev{dev_id}->store[{i}](dev{store_dev}): ret={ret}"
            )

        for rm, _ in local_mems:
            rm.deregister()
        logger.info(f"[APP] deregister done, count={len(local_mems)}")
        for eng in engines:
            eng.finalize()
        logger.info(f"[APP] finalize done, count={len(engines)}")
        result_queue.put(True)
    except Exception as e:
        logger.error("App error: %s", e)
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
        for rm, _ in local_mems:
            rm.deregister()
        for eng in engines:
            eng.finalize()
        result_queue.put(False)


class TestMooncakeStandalone:
    """Mooncake standalone 部署模式的端到端测试

    模拟 Store Service + App 同主机部署，设备均分：
    - App 使用前半设备，分配 device 内存
    - Store Service 使用后半设备，共享一大块 pinned host 内存，每个 engine 注册整块

    App[i] WRITE 到 store 大内存的 i * MEM_SIZE 偏移处，READ 回验证数据一致性。
    """

    @staticmethod
    def test_normal_full_flow_same_host_d2rh_write():
        """测试 standalone 同主机 D2RH WRITE + READ 完整流程

        测试步骤：
        1. 启动 Store Service：分配大块 pinned host 内存，N 个引擎各注册整块
        2. 启动 App：N 个引擎各分配 device 内存，填充 APP_BYTE
        3. App[i] 连接 Store[i]（identity 映射）
        4. App[i] WRITE 到 store 内存的 i * MEM_SIZE 偏移处
        5. App[i] READ 回同一偏移，验证是 APP_BYTE
        6. 清理资源
        """
        ctx = multiprocessing.get_context("spawn")
        ready_event = ctx.Event()
        addr_queue = ctx.Queue()
        stop_event = ctx.Event()
        result_queue = ctx.Queue()

        store = ctx.Process(
            target=_store_service_worker,
            args=(ready_event, addr_queue, stop_event),
        )
        store.start()
        assert ready_event.wait(timeout=60), "Store startup timeout"
        store_info = addr_queue.get(timeout=10)
        assert store_info, "Store failed"

        app = ctx.Process(target=_app_worker, args=(store_info, result_queue))
        app.start()
        app.join(timeout=120)
        stop_event.set()
        store.join(timeout=30)

        assert app.exitcode == 0, f"App failed with exit code {app.exitcode}"
        assert result_queue.get(timeout=10), "App worker failed"
