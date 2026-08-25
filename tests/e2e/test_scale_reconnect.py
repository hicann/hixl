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
import os
import time
from dataclasses import dataclass

import hixl

from .utils import (
    CONNECT_TIMEOUT_MS,
    TRANSFER_TIMEOUT_MS,
    RegisteredMem,
    alloc_device_mem,
    create_engine,
    get_device_lists,
    get_endpoint,
    suppress_noisy_loggers,
)

logger = logging.getLogger(__name__)

SCENARIO_IDX = 4
LINKS_PER_DEV = int(os.environ.get("HIXL_E2E_LINKS_PER_DEV", "250"))
TRANSFER_SIZE_PER_LINK = int(os.environ.get("HIXL_E2E_TRANSFER_SIZE", "4096"))
REGISTER_MEM_SIZE = int(
    os.environ.get("HIXL_E2E_REGISTER_SIZE", str(256 * 1024 * 1024))
)
if LINKS_PER_DEV <= 0:
    raise ValueError(f"HIXL_E2E_LINKS_PER_DEV must be > 0, got {LINKS_PER_DEV}")
if TRANSFER_SIZE_PER_LINK <= 0:
    raise ValueError(
        f"HIXL_E2E_TRANSFER_SIZE must be > 0, got {TRANSFER_SIZE_PER_LINK}"
    )
if REGISTER_MEM_SIZE <= 0:
    raise ValueError(f"HIXL_E2E_REGISTER_SIZE must be > 0, got {REGISTER_MEM_SIZE}")
FLAP_DEV = 2
SERVER_BYTE = 0xAA
CLIENT_BYTE = 0xBB


@dataclass
class FlapConfig:
    """设备抖动测试配置"""

    flap_event: multiprocessing.Event = None
    flap_done_event: multiprocessing.Event = None
    flap_addrs_queue: multiprocessing.Queue = None


def _server_worker(ready_event, addr_queue, stop_event, flap_config=None):
    logging.basicConfig(
        format="%(asctime)s [S5-SERVER] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _, server_devs = get_device_lists()
    n = len(server_devs)
    engines = []
    mems = []
    try:
        for i, dev_id in enumerate(server_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "server")
            eng = create_engine(dev_id, ep)
            engines.append(eng)
            addr, size, buf = alloc_device_mem(REGISTER_MEM_SIZE)
            buf.fill_(SERVER_BYTE)
            rm = RegisteredMem(eng, addr, size, hixl.MEM_DEVICE)
            mems.append((rm, buf, i))

        addrs = {i: mems[i][0].addr for i in range(n)}
        addr_queue.put(addrs)
        ready_event.set()

        if flap_config is not None and flap_config.flap_event is not None:
            flap_config.flap_event.wait(timeout=300)
            if flap_config.flap_event.is_set():
                logger.info("Flapping dev%d: deregister + finalize", FLAP_DEV)
                mems[FLAP_DEV][0].deregister()
                engines[FLAP_DEV].finalize()
                mems[FLAP_DEV] = (None, None, FLAP_DEV)
                engines[FLAP_DEV] = None

                if flap_config.flap_done_event is not None:
                    flap_config.flap_done_event.wait(timeout=30)

                logger.info("Flapping dev%d: re-initialize + re-register", FLAP_DEV)
                ep = get_endpoint(SCENARIO_IDX, FLAP_DEV, "server")
                new_eng = create_engine(server_devs[FLAP_DEV], ep)
                new_addr, new_size, new_buf = alloc_device_mem(REGISTER_MEM_SIZE)
                new_buf.fill_(SERVER_BYTE)
                new_rm = RegisteredMem(new_eng, new_addr, new_size, hixl.MEM_DEVICE)
                engines[FLAP_DEV] = new_eng
                mems[FLAP_DEV] = (new_rm, new_buf, FLAP_DEV)

                if flap_config.flap_addrs_queue is not None:
                    flap_config.flap_addrs_queue.put({FLAP_DEV: new_addr})

                ready_event.set()

        stop_event.wait(timeout=300)
    except Exception as e:
        logger.error("Server error: %s", e)
        ready_event.set()
        addr_queue.put({})
    finally:
        logger.info(f"[SERVER] deregister start, count={len(mems)}")
        for rm, _, _ in mems:
            if rm is not None:
                rm.deregister()
        logger.info("[SERVER] deregister done")
        logger.info(f"[SERVER] finalize start, count={len(engines)}")
        for eng in engines:
            if eng is not None:
                eng.finalize()
        logger.info("[SERVER] finalize done")


def _client_worker_normal(remote_addrs, result_queue):
    logging.basicConfig(
        format="%(asctime)s [S5-CLIENT] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    client_devs, _ = get_device_lists()
    n = len(client_devs)
    total_links = LINKS_PER_DEV * n
    engines = []
    local_mems = []
    try:
        for i, dev_id in enumerate(client_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "client")
            eng = create_engine(dev_id, ep)
            engines.append(eng)
            addr, size, buf = alloc_device_mem(TRANSFER_SIZE_PER_LINK * LINKS_PER_DEV)
            buf.fill_(CLIENT_BYTE)
            rm = RegisteredMem(eng, addr, size, hixl.MEM_DEVICE)
            local_mems.append((rm, buf))

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = engines[i].connect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[CLIENT] connect[{i}] dev{client_devs[i]}->server[{i}]: ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"Connect client[{i}] dev{client_devs[i]}->server[{i}] failed: {ret}"
            )

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = remote_addrs[i]
            local_base = local_mems[i][0].addr
            for link_idx in range(LINKS_PER_DEV):
                offset = (link_idx * TRANSFER_SIZE_PER_LINK) % REGISTER_MEM_SIZE
                local_offset = link_idx * TRANSFER_SIZE_PER_LINK
                op_descs = [
                    hixl.TransferOpDesc(
                        local_addr=local_base + local_offset,
                        remote_addr=remote_addr + offset,
                        len=TRANSFER_SIZE_PER_LINK,
                    )
                ]
                ret = engines[i].transfer_sync(
                    remote_ep,
                    hixl.TransferOp.WRITE,
                    op_descs,
                    timeout_in_millis=TRANSFER_TIMEOUT_MS,
                )
                assert ret == hixl.SUCCESS, (
                    f"WRITE client[{i}] dev{client_devs[i]} link{link_idx} failed: {ret}"
                )

        logger.info(f"[CLIENT] All {total_links} logical links WRITE completed")

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = engines[i].disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[CLIENT] disconnect[{i}] dev{client_devs[i]}->server[{i}]: ret={ret}"
            )

        logger.info(f"[CLIENT] deregister start, count={len(local_mems)}")
        for rm, _ in local_mems:
            rm.deregister()
        logger.info("[CLIENT] deregister done")
        logger.info(f"[CLIENT] finalize start, count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[CLIENT] finalize done")
        result_queue.put(True)
    except Exception as e:
        logger.error("Client error: %s", e)
        logger.info(f"[CLIENT] disconnect start (error), count={len(engines)}")
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
        logger.info("[CLIENT] disconnect done")
        logger.info(f"[CLIENT] deregister start (error), count={len(local_mems)}")
        for rm, _ in local_mems:
            rm.deregister()
        logger.info("[CLIENT] deregister done")
        logger.info(f"[CLIENT] finalize start (error), count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[CLIENT] finalize done")
        result_queue.put(False)


def _client_worker_flap(remote_addrs, result_queue, flap_config=None):
    logging.basicConfig(
        format="%(asctime)s [S5-CLIENT] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    client_devs, _ = get_device_lists()
    n = len(client_devs)
    engines = []
    local_mems = []
    try:
        for i, dev_id in enumerate(client_devs):
            ep = get_endpoint(SCENARIO_IDX, i, "client")
            eng = create_engine(dev_id, ep)
            engines.append(eng)
            addr, size, buf = alloc_device_mem(TRANSFER_SIZE_PER_LINK * LINKS_PER_DEV)
            buf.fill_(CLIENT_BYTE)
            rm = RegisteredMem(eng, addr, size, hixl.MEM_DEVICE)
            local_mems.append((rm, buf))

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = engines[i].connect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[CLIENT-FLAP] connect[{i}] dev{client_devs[i]}->server[{i}]: ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"Connect client[{i}] dev{client_devs[i]}->server[{i}] failed: {ret}"
            )

        for i in range(n):
            if i == FLAP_DEV:
                continue
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = remote_addrs[i]
            local_base = local_mems[i][0].addr
            op_descs = [
                hixl.TransferOpDesc(
                    local_addr=local_base,
                    remote_addr=remote_addr,
                    len=TRANSFER_SIZE_PER_LINK,
                )
            ]
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.WRITE,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[CLIENT-FLAP] Pre-flap WRITE[{i}] dev{client_devs[i]}: ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"Pre-flap WRITE client[{i}] dev{client_devs[i]} failed: {ret}"
            )

        if flap_config is not None and flap_config.flap_event is not None:
            flap_config.flap_event.set()

        flap_client_idx = FLAP_DEV
        flap_remote_ep = get_endpoint(SCENARIO_IDX, FLAP_DEV, "server")
        flap_remote_addr = remote_addrs[FLAP_DEV]
        flap_local_base = local_mems[flap_client_idx][0].addr

        logger.info(
            f"[CLIENT-FLAP] Flap test: client[{flap_client_idx}] "
            f"dev{client_devs[flap_client_idx]} -> server[{FLAP_DEV}]"
        )

        op_descs = [
            hixl.TransferOpDesc(
                local_addr=flap_local_base,
                remote_addr=flap_remote_addr,
                len=TRANSFER_SIZE_PER_LINK,
            )
        ]
        logger.info(
            f"[CLIENT-FLAP] Attempting transfer during flap: client[{flap_client_idx}] -> server[{FLAP_DEV}]"
        )
        ret = engines[flap_client_idx].transfer_sync(
            flap_remote_ep,
            hixl.TransferOp.WRITE,
            op_descs,
            timeout_in_millis=5000,
        )
        logger.info(
            f"[CLIENT-FLAP] Transfer to flapping server[{FLAP_DEV}] during flap: ret={ret}"
        )

        for i in range(n):
            if i == FLAP_DEV:
                continue
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            remote_addr = remote_addrs[i]
            local_base = local_mems[i][0].addr
            op_descs = [
                hixl.TransferOpDesc(
                    local_addr=local_base + TRANSFER_SIZE_PER_LINK,
                    remote_addr=remote_addr + TRANSFER_SIZE_PER_LINK,
                    len=TRANSFER_SIZE_PER_LINK,
                )
            ]
            ret = engines[i].transfer_sync(
                remote_ep,
                hixl.TransferOp.WRITE,
                op_descs,
                timeout_in_millis=TRANSFER_TIMEOUT_MS,
            )
            logger.info(
                f"[CLIENT-FLAP] During-flap WRITE[{i}] dev{client_devs[i]}: ret={ret}"
            )
            assert ret == hixl.SUCCESS, (
                f"During-flap WRITE client[{i}] dev{client_devs[i]} failed: {ret}"
            )

        if flap_config is not None and flap_config.flap_done_event is not None:
            flap_config.flap_done_event.set()

        if flap_config is not None and flap_config.flap_addrs_queue is not None:
            logger.info("[CLIENT-FLAP] Waiting for new flap device address...")
            new_addrs = flap_config.flap_addrs_queue.get(timeout=60)
            new_flap_addr = new_addrs.get(FLAP_DEV)
            logger.info(f"[CLIENT-FLAP] Received new flap address: {new_flap_addr}")

            if new_flap_addr is not None:
                logger.info(f"[CLIENT-FLAP] Disconnecting from server[{FLAP_DEV}]...")
                ret = engines[flap_client_idx].disconnect(
                    flap_remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS
                )
                logger.info(f"[CLIENT-FLAP] Disconnect result: {ret}")

                time.sleep(2)

                logger.info(f"[CLIENT-FLAP] Reconnecting to server[{FLAP_DEV}]...")
                ret = engines[flap_client_idx].connect(
                    flap_remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS
                )
                logger.info(f"[CLIENT-FLAP] Reconnect result: {ret}")
                assert ret == hixl.SUCCESS, (
                    f"Reconnect to server[{FLAP_DEV}] after flap failed: {ret}"
                )

                op_descs = [
                    hixl.TransferOpDesc(
                        local_addr=flap_local_base,
                        remote_addr=new_flap_addr,
                        len=TRANSFER_SIZE_PER_LINK,
                    )
                ]
                logger.info(
                    f"[CLIENT-FLAP] Attempting post-reconnect WRITE: client[{flap_client_idx}] -> server[{FLAP_DEV}]"
                )
                ret = engines[flap_client_idx].transfer_sync(
                    flap_remote_ep,
                    hixl.TransferOp.WRITE,
                    op_descs,
                    timeout_in_millis=TRANSFER_TIMEOUT_MS,
                )
                logger.info(f"[CLIENT-FLAP] Post-reconnect WRITE result: {ret}")
                assert ret == hixl.SUCCESS, (
                    f"Post-reconnect WRITE to server[{FLAP_DEV}] failed: {ret}"
                )
                logger.info(
                    f"[CLIENT-FLAP] Post-reconnect WRITE to server[{FLAP_DEV}] success"
                )

        for i in range(n):
            remote_ep = get_endpoint(SCENARIO_IDX, i, "server")
            ret = engines[i].disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
            logger.info(
                f"[CLIENT-FLAP] disconnect[{i}] dev{client_devs[i]}->server[{i}]: ret={ret}"
            )

        logger.info(f"[CLIENT-FLAP] deregister start, count={len(local_mems)}")
        for rm, _ in local_mems:
            rm.deregister()
        logger.info("[CLIENT-FLAP] deregister done")
        logger.info(f"[CLIENT-FLAP] finalize start, count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[CLIENT-FLAP] finalize done")
        result_queue.put(True)
    except Exception as e:
        logger.error("Client error: %s", e)
        logger.info(f"[CLIENT-FLAP] disconnect start (error), count={len(engines)}")
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
        logger.info("[CLIENT-FLAP] disconnect done")
        logger.info(f"[CLIENT-FLAP] deregister start (error), count={len(local_mems)}")
        for rm, _ in local_mems:
            rm.deregister()
        logger.info("[CLIENT-FLAP] deregister done")
        logger.info(f"[CLIENT-FLAP] finalize start (error), count={len(engines)}")
        for eng in engines:
            eng.finalize()
        logger.info("[CLIENT-FLAP] finalize done")
        result_queue.put(False)


class TestMooncakeScaleReconnect:
    """大规模连接和设备抖动重连测试

    设备分配策略：
    - 使用 get_device_lists() 获取错位设备列表
    - Client 使用 [0, 1, 2, 3]，Server 使用 [1, 2, 3, 0]
    - 连接采用 identity 映射: client[i] -> server[i]
    - 由于 server 设备已错位，client[i] 和 server[i] 天然在不同物理设备上
    """

    @staticmethod
    def test_normal_flow_multi_links():
        """测试大规模多链路传输的正常流程"""
        ctx = multiprocessing.get_context("spawn")
        ready_event = ctx.Event()
        addr_queue = ctx.Queue()
        stop_event = ctx.Event()

        server = ctx.Process(
            target=_server_worker,
            args=(ready_event, addr_queue, stop_event),
        )
        server.start()
        assert ready_event.wait(timeout=120), "Server startup timeout"
        remote_addrs = addr_queue.get(timeout=10)
        assert remote_addrs, "Server failed"

        result_q = ctx.Queue()
        client = ctx.Process(
            target=_client_worker_normal,
            args=(remote_addrs, result_q),
        )
        client.start()
        client.join(timeout=300)
        stop_event.set()
        server.join(timeout=30)

        assert client.exitcode == 0, f"Client exit code {client.exitcode}"
        result = result_q.get(timeout=10)
        assert result is True, "Client reported failure"

    @staticmethod
    def test_flapping_dev_reconnect():
        """测试设备抖动场景下的重连能力"""
        ctx = multiprocessing.get_context("spawn")
        ready_event = ctx.Event()
        addr_queue = ctx.Queue()
        stop_event = ctx.Event()
        flap_config = FlapConfig(
            flap_event=ctx.Event(),
            flap_done_event=ctx.Event(),
            flap_addrs_queue=ctx.Queue(),
        )

        server = ctx.Process(
            target=_server_worker,
            args=(ready_event, addr_queue, stop_event, flap_config),
        )
        server.start()
        assert ready_event.wait(timeout=120), "Server startup timeout"
        remote_addrs = addr_queue.get(timeout=10)
        assert remote_addrs, "Server failed"

        result_q = ctx.Queue()
        client = ctx.Process(
            target=_client_worker_flap,
            args=(remote_addrs, result_q, flap_config),
        )
        client.start()
        client.join(timeout=300)
        stop_event.set()
        server.join(timeout=30)

        assert client.exitcode == 0, f"Client exit code {client.exitcode}"
        result = result_q.get(timeout=10)
        assert result is True, "Client reported failure"
