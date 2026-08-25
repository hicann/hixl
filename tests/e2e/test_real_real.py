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

SCENARIO_IDX = 0
MEM_SIZE = TRANSFER_SIZE
SERVER_BYTE = 0xCC
CLIENT_BYTE = 0xDD


def _server_worker(ready_event, addr_queue, stop_event):
    """Server 进程工作函数

    创建 N 个 HIXL 引擎（使用均分后的后半设备），为每个引擎分配设备内存
    并填充 0xCC 模式数据，注册内存后通过 Queue 传递内存地址给 Client，
    等待 Client 完成测试后清理资源。
    """
    logging.basicConfig(
        format="%(asctime)s [S1-SERVER] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _, server_devs = get_device_lists()
    engines = []
    mems = []
    try:
        for i, dev_id in enumerate(server_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "server")
            eng = create_engine(dev_id, ep)
            engines.append(eng)
            addr, size, buf = alloc_device_mem(MEM_SIZE)
            buf.fill_(SERVER_BYTE)
            rm = RegisteredMem(eng, addr, size, hixl.MEM_DEVICE)
            mems.append((rm, buf))

        addrs = {i: mems[i][0].addr for i in range(len(server_devs))}
        addr_queue.put(addrs)
        ready_event.set()
        stop_event.wait(timeout=300)
    except Exception as e:
        logger.error("Server error: %s", e)
        ready_event.set()
        addr_queue.put({})
    finally:
        logger.info(f"[SERVER-D2RD] deregister start, count={len(mems)}")
        for rm, _ in mems:
            rm.deregister()
        logger.info("[SERVER-D2RD] deregister done")
        logger.info(f"[SERVER-D2RD] finalize start, count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[SERVER-D2RD] finalize done")


SCENARIO_IDX_D2RH = 5


def _server_worker_host_mem(ready_event, addr_queue, stop_event):
    """Server 进程工作函数（D2RH 场景）

    创建 N 个 HIXL 引擎（使用均分后的后半设备），为每个引擎分配 Host 内存
    并填充 0xCC 模式数据，注册为 MEM_HOST 后通过 Queue 传递内存地址给 Client，
    等待 Client 完成测试后清理资源。
    """
    logging.basicConfig(
        format="%(asctime)s [S1-SERVER-D2RH] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _, server_devs = get_device_lists()
    engines = []
    mems = []
    try:
        for i, dev_id in enumerate(server_devs):
            ep = get_endpoint(SCENARIO_IDX_D2RH, i, "server")
            eng = create_engine(dev_id, ep)
            engines.append(eng)
            addr, size, buf = alloc_host_mem_pinned(MEM_SIZE)
            buf.fill_(SERVER_BYTE)
            rm = RegisteredMem(eng, addr, size, hixl.MEM_HOST)
            mems.append((rm, buf))

        addrs = {i: mems[i][0].addr for i in range(len(server_devs))}
        addr_queue.put(addrs)
        ready_event.set()
        stop_event.wait(timeout=300)
    except Exception as e:
        logger.error("Server error: %s", e)
        ready_event.set()
        addr_queue.put({})
    finally:
        logger.info(f"[SERVER-D2RH] deregister start, count={len(mems)}")
        for rm, _ in mems:
            rm.deregister()
        logger.info("[SERVER-D2RH] deregister done")
        logger.info(f"[SERVER-D2RH] finalize start, count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[SERVER-D2RH] finalize done")


def _client_worker(remote_addrs, scenario_idx, result_queue):
    """Client 进程工作函数

    创建 N 个 HIXL 引擎（使用均分后的前半设备），为每个引擎分配设备内存
    并填充 0xDD 模式数据，注册内存。连接到对应的 Server 引擎（identity 映射），
    执行 WRITE 操作将自己的数据写入 Server，执行 READ 操作从 Server 读回数据
    并验证数据一致性，最后断开连接并清理资源。

    参数:
        remote_addrs: Server 各引擎的内存地址字典 {逻辑索引: 物理地址}
        scenario_idx: 场景索引，用于端口分配
    """
    logging.basicConfig(
        format=f"%(asctime)s [S{scenario_idx}-CLIENT] %(message)s",
        level=logging.INFO,
    )
    suppress_noisy_loggers()
    client_devs, _ = get_device_lists()
    _, server_devs = get_device_lists()
    engines = []
    local_mems = []
    try:
        for i, dev_id in enumerate(client_devs):
            ep = get_endpoint(scenario_idx, i, "client")
            eng = create_engine(dev_id, ep)
            engines.append(eng)
            addr, size, buf = alloc_device_mem(MEM_SIZE)
            buf.fill_(CLIENT_BYTE)
            rm = RegisteredMem(eng, addr, size, hixl.MEM_DEVICE)
            local_mems.append((rm, buf))

        for i, (eng, dev_id, server_dev) in enumerate(
            zip(engines, client_devs, server_devs)
        ):
            remote_ep = get_endpoint(scenario_idx, i, "server")
            ret = eng.connect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[CLIENT] connect[{i}] dev{dev_id}->server[{i}](dev{server_dev}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"Connect client[{i}] dev{dev_id}->server[{i}](dev{server_dev}) failed: {ret}"
            )

        for i, (eng, (rm, _), dev_id, server_dev) in enumerate(
            zip(engines, local_mems, client_devs, server_devs)
        ):
            remote_ep = get_endpoint(scenario_idx, i, "server")
            remote_addr = remote_addrs[i]
            local_addr = rm.addr
            op_descs = build_op_descs(local_addr, remote_addr, MEM_SIZE)
            ret = eng.transfer_sync(
                remote_ep,
                hixl.TransferOp.WRITE,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[CLIENT] WRITE[{i}] dev{dev_id}->server[{i}](dev{server_dev}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"WRITE client[{i}] dev{dev_id}->server[{i}](dev{server_dev}) failed: {ret}"
            )

        for i, (eng, (rm, local_tensor), dev_id, server_dev) in enumerate(
            zip(engines, local_mems, client_devs, server_devs)
        ):
            remote_ep = get_endpoint(scenario_idx, i, "server")
            remote_addr = remote_addrs[i]
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
                f"[CLIENT] READ[{i}] dev{dev_id}<-server[{i}](dev{server_dev}): ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"READ client[{i}] dev{dev_id}<-server[{i}](dev{server_dev}) failed: {ret}"
            )
            assert verify_data(local_tensor, CLIENT_BYTE), (
                f"Data mismatch after READ client[{i}] dev{dev_id}<-server[{i}](dev{server_dev})"
            )

        for i, (eng, dev_id, server_dev) in enumerate(
            zip(engines, client_devs, server_devs)
        ):
            remote_ep = get_endpoint(scenario_idx, i, "server")
            ret = eng.disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[CLIENT] disconnect[{i}] dev{dev_id}->server[{i}](dev{server_dev}): ret={ret}"
            )

        for rm, _ in local_mems:
            rm.deregister()
        logger.info(f"[CLIENT] deregister done, count={len(local_mems)}")
        for eng in engines:
            eng.finalize()
        logger.info(f"[CLIENT] finalize done, count={len(engines)}")
        result_queue.put(True)
    except Exception as e:
        logger.error("Client error: %s", e)
        for i, (eng, (rm, _)) in enumerate(zip(engines, local_mems)):
            try:
                remote_ep = get_endpoint(scenario_idx, i, "server")
                eng.disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            except Exception as disconnect_err:
                logger.warning(
                    "Failed to disconnect engine[%d] in error handler: %s",
                    i,
                    disconnect_err,
                )
            rm.deregister()
        for eng in engines:
            eng.finalize()
        result_queue.put(False)


class TestMooncakeRealReal:
    """Mooncake real-real 部署模式的端到端测试

    模拟两个 embedded RealClient（类似 vLLM 的 Prefill 和 Decode 节点），
    使用错位设备分配策略，两个进程不共享设备。

    设备分配（以 ASCEND_RT_VISIBLE_DEVICES="0,1,2,3" 为例）：
    - Client 逻辑 ID: [0, 1, 2, 3] -> 物理设备 [0, 1, 2, 3]
    - Server 逻辑 ID: [0, 1, 2, 3] -> 物理设备 [1, 2, 3, 0]（错位 +1）

    连接关系（identity 映射）：
    - Client[0] (物理 0) -> Server[0] (物理 1)
    - Client[1] (物理 1) -> Server[1] (物理 2)
    - Client[2] (物理 2) -> Server[2] (物理 3)
    - Client[3] (物理 3) -> Server[3] (物理 0)

    所有连接都是跨设备的，确保测试真实的 D2D 传输路径。
    """

    @staticmethod
    def test_normal_full_flow_d2rd_batch_read_write():
        """测试 D2D（Device to Device）跨设备批量读写的完整流程

        测试步骤：
        1. 启动 Server 进程：创建 N 个引擎，分配设备内存，填充 0xCC，注册内存
        2. 启动 Client 进程：创建 N 个引擎，分配设备内存，填充 0xDD，注册内存
        3. Client 连接 Server：每个 Client[i] 连接到 Server[i]（跨设备）
        4. WRITE 操作：Client 将自己的内存数据写入 Server 内存
        5. READ 操作：Client 从 Server 读回数据，验证是 Server 原始的 0xCC
        6. 清理资源：断开连接，解注册内存，终止引擎

        验证点：
        - 所有连接成功建立
        - 所有 WRITE 操作成功
        - 所有 READ 操作成功
        - 读回的数据与 Server 原始数据一致（数据完整性）
        - Client 进程正常退出
        """
        ctx = multiprocessing.get_context("spawn")
        ready_event = ctx.Event()
        addr_queue = ctx.Queue()
        stop_event = ctx.Event()
        result_queue = ctx.Queue()

        server = ctx.Process(
            target=_server_worker,
            args=(ready_event, addr_queue, stop_event),
        )
        server.start()
        assert ready_event.wait(timeout=60), "Server startup timeout"
        remote_addrs = addr_queue.get(timeout=10)
        assert remote_addrs, "Server failed to provide addresses"

        client = ctx.Process(
            target=_client_worker, args=(remote_addrs, SCENARIO_IDX, result_queue)
        )
        client.start()
        client.join(timeout=120)
        stop_event.set()
        server.join(timeout=30)

        assert client.exitcode == 0, f"Client failed with exit code {client.exitcode}"
        assert result_queue.get(timeout=10), "Client worker failed"

    @staticmethod
    def test_normal_full_flow_d2rh_batch_read_write():
        """测试 D2RH（Device to Remote Host）场景的批量读写完整流程

        与 D2D 场景的区别：Server 侧使用 Host 内存（MEM_HOST）而非设备内存（MEM_DEVICE）。
        模拟 Prefill 节点将 KV Cache 推送到远端 Host 内存的场景。

        测试步骤：
        1. 启动 Server 进程：创建 N 个引擎，分配 Host 内存，填充 0xCC，注册为 MEM_HOST
        2. 启动 Client 进程：创建 N 个引擎，分配设备内存，填充 0xDD，注册为 MEM_DEVICE
        3. Client 连接 Server：每个 Client[i] 连接到 Server[i]（跨设备）
        4. WRITE 操作：Client 将设备内存数据写入 Server 的 Host 内存
        5. READ 操作：Client 从 Server 的 Host 内存读回数据，验证是 Server 原始的 0xCC
        6. 清理资源：断开连接，解注册内存，终止引擎

        验证点：
        - 所有连接成功建立
        - 所有 WRITE 操作成功（Device -> Host）
        - 所有 READ 操作成功（Host -> Device）
        - 读回的数据与 Server 原始数据一致（数据完整性）
        - Client 进程正常退出
        """
        ctx = multiprocessing.get_context("spawn")
        ready_event = ctx.Event()
        addr_queue = ctx.Queue()
        stop_event = ctx.Event()
        result_queue = ctx.Queue()

        server = ctx.Process(
            target=_server_worker_host_mem,
            args=(ready_event, addr_queue, stop_event),
        )
        server.start()
        assert ready_event.wait(timeout=60), "Server startup timeout"
        remote_addrs = addr_queue.get(timeout=10)
        assert remote_addrs, "Server failed to provide addresses"

        client = ctx.Process(
            target=_client_worker, args=(remote_addrs, SCENARIO_IDX_D2RH, result_queue)
        )
        client.start()
        client.join(timeout=120)
        stop_event.set()
        server.join(timeout=30)

        assert client.exitcode == 0, f"Client failed with exit code {client.exitcode}"
        assert result_queue.get(timeout=10), "Client worker failed"
