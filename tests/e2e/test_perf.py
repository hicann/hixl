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

import hixl

from .utils import (
    CONNECT_TIMEOUT_MS,
    TRANSFER_TIMEOUT_MS,
    RegisteredMem,
    alloc_device_mem,
    build_op_descs,
    create_engine,
    get_device_lists,
    get_endpoint,
    suppress_noisy_loggers,
)

logger = logging.getLogger(__name__)

SCENARIO_IDX = 2
WARMUP_ITERS = 1

BLOCK_SIZE = 16 * 1024
MAX_MEM_SIZE = 50000 * BLOCK_SIZE

SCENARIOS = [
    {"name": "1byte_latency", "block_size": 1, "total_size": 1, "iters": 1000},
    {
        "name": "batch_16k_8m",
        "block_size": BLOCK_SIZE,
        "total_size": 8 * 1024 * 1024,
        "iters": 100,
    },
    {
        "name": "batch_16k_128m",
        "block_size": BLOCK_SIZE,
        "total_size": 128 * 1024 * 1024,
        "iters": 100,
    },
    {
        "name": "batch_16k_50kblk",
        "block_size": BLOCK_SIZE,
        "total_size": 50000 * BLOCK_SIZE,
        "iters": 100,
    },
]

DECIMAL_BYTES_PER_GB = 1e9
PERF_TOLERANCE = 0.10

BASELINES = {
    "1byte_latency": {"avg_latency_us": 88.64},
    "batch_16k_8m": {"avg_latency_us": 589.94, "avg_throughput_gbps": 14.219},
    "batch_16k_128m": {"avg_latency_us": 7484.30, "avg_throughput_gbps": 17.933},
    "batch_16k_50kblk": {"avg_latency_us": 45799.04, "avg_throughput_gbps": 17.887},
}


def _server_worker(ready_event, addr_queue, stop_event):
    logging.basicConfig(
        format="%(asctime)s [PERF-SERVER] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    _, server_devs = get_device_lists()
    dev_id = server_devs[0]
    engine = None
    mem = None
    try:
        ep = get_endpoint(SCENARIO_IDX, 0, "server")
        logger.info("Initializing engine on device %d, endpoint %s", dev_id, ep)
        engine = create_engine(dev_id, ep)
        logger.info("Allocating device memory: %d bytes", MAX_MEM_SIZE)
        addr, size, _buf = alloc_device_mem(MAX_MEM_SIZE)
        mem = RegisteredMem(engine, addr, size, hixl.MEM_DEVICE)
        logger.info("Server ready, mem_addr=0x%x", addr)
        addr_queue.put(addr)
        ready_event.set()
        stop_event.wait(timeout=600)
    except Exception as e:  # noqa: BLE001
        logger.error("Server error: %s", e)
        ready_event.set()
        addr_queue.put(None)
    finally:
        logger.info("Server cleanup: deregister + finalize")
        if mem is not None:
            mem.deregister()
        if engine is not None:
            engine.finalize()


def _client_worker(remote_addr, result_queue):
    logging.basicConfig(
        format="%(asctime)s [PERF-CLIENT] %(message)s", level=logging.INFO
    )
    suppress_noisy_loggers()
    client_devs, _ = get_device_lists()
    dev_id = client_devs[0]
    engine = None
    mem = None
    try:
        ep = get_endpoint(SCENARIO_IDX, 0, "client")
        logger.info("Initializing engine on device %d, endpoint %s", dev_id, ep)
        engine = create_engine(dev_id, ep)
        logger.info("Allocating device memory: %d bytes", MAX_MEM_SIZE)
        addr, size, _buf = alloc_device_mem(MAX_MEM_SIZE)
        mem = RegisteredMem(engine, addr, size, hixl.MEM_DEVICE)

        remote_ep = get_endpoint(SCENARIO_IDX, 0, "server")
        logger.info("Connecting to server %s", remote_ep)
        ret = engine.connect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
        assert ret == hixl.SUCCESS, f"Connect failed: ret={ret}"
        logger.info("Connected successfully")

        results = {}
        for scenario in SCENARIOS:
            name = scenario["name"]
            block_size = scenario["block_size"]
            total_size = scenario["total_size"]
            total_iters = scenario["iters"]

            op_descs = build_op_descs(addr, remote_addr, total_size, block_size)
            logger.info(
                "Starting scenario [%s]: block_size=%d, total_size=%d, blocks=%d, iters=%d (warmup=%d)",
                name,
                block_size,
                total_size,
                len(op_descs),
                total_iters,
                WARMUP_ITERS,
            )

            latencies_us = []
            for i in range(total_iters):
                start = time.perf_counter()
                ret = engine.transfer_sync(
                    remote_ep,
                    hixl.TransferOp.WRITE,
                    op_descs,
                    timeout_in_millis=TRANSFER_TIMEOUT_MS,
                )
                elapsed_us = (time.perf_counter() - start) * 1e6
                assert ret == hixl.SUCCESS, (
                    f"TransferSync failed in {name} iter {i}: ret={ret}"
                )
                if i < WARMUP_ITERS:
                    logger.info("[%s] iter %d (warmup): %.2f us", name, i, elapsed_us)
                else:
                    latencies_us.append(elapsed_us)

            avg_latency = sum(latencies_us) / len(latencies_us)
            min_latency = min(latencies_us)
            max_latency = max(latencies_us)
            result = {"avg_latency_us": avg_latency}

            if total_size > 1:
                avg_time_s = avg_latency / 1e6
                avg_throughput = total_size / DECIMAL_BYTES_PER_GB / avg_time_s
                result["avg_throughput_gbps"] = avg_throughput
                logger.info(
                    "[PERF] %s: avg_latency=%.2f us, min=%.2f us, max=%.2f us, avg_throughput=%.3f GB/s",
                    name,
                    avg_latency,
                    min_latency,
                    max_latency,
                    avg_throughput,
                )
            else:
                logger.info(
                    "[PERF] %s: avg_latency=%.2f us, min=%.2f us, max=%.2f us",
                    name,
                    avg_latency,
                    min_latency,
                    max_latency,
                )

            results[name] = result

        logger.info("Disconnecting from server")
        engine.disconnect(remote_ep, timeout_in_millis=CONNECT_TIMEOUT_MS)
        mem.deregister()
        engine.finalize()
        logger.info("Client cleanup done")
        result_queue.put(results)
    except Exception as e:  # noqa: BLE001
        logger.error("Client error: %s", e)
        if mem is not None:
            mem.deregister()
        if engine is not None:
            engine.finalize()
        result_queue.put(None)


class TestPerfDeviceRoce:
    """Device RoCE 传输时延与吞吐性能看护

    使用 device RoCE 在单对设备 (dev0 -> dev1) 上执行 4 个场景的传输测试：
    1. 1 字节传输时延 (1000 次迭代)
    2. 批量 16K block 共 8M 传输 (100 次迭代)
    3. 批量 16K block 共 128M 传输 (100 次迭代)
    4. 批量 16K block 共 50000 个 block 传输 (100 次迭代)

    每个场景第 1 次预热，后续迭代取均值。
    """

    @staticmethod
    def test_device_roce_latency_and_throughput():
        original_log_level = os.environ.get("ASCEND_GLOBAL_LOG_LEVEL")
        if original_log_level != "3":
            logger.warning(
                "ASCEND_GLOBAL_LOG_LEVEL is %s, expected 3 (error). "
                "Setting to 3 for performance test.",
                original_log_level if original_log_level else "not set",
            )
        os.environ["ASCEND_GLOBAL_LOG_LEVEL"] = "3"

        try:
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
            assert ready_event.wait(timeout=120), "Server startup timeout"
            remote_addr = addr_queue.get(timeout=10)
            assert remote_addr is not None, "Server failed to provide address"

            client = ctx.Process(
                target=_client_worker,
                args=(remote_addr, result_queue),
            )
            client.start()
            client.join(timeout=600)
            stop_event.set()
            server.join(timeout=30)

            assert client.exitcode == 0, (
                f"Client failed with exit code {client.exitcode}"
            )
            results = result_queue.get(timeout=10)
            assert results is not None, "Client worker failed"
            assert len(results) == len(SCENARIOS), (
                f"Expected {len(SCENARIOS)} results, got {len(results)}"
            )

            failures = []
            for name, baseline in BASELINES.items():
                actual = results[name]
                for metric, base_val in baseline.items():
                    actual_val = actual[metric]
                    if metric == "avg_latency_us":
                        limit = base_val * (1 + PERF_TOLERANCE)
                        if actual_val > limit:
                            failures.append(
                                f"{name}.{metric}: {actual_val:.2f} > {limit:.2f} "
                                f"(baseline={base_val:.2f}, +{PERF_TOLERANCE:.0%})"
                            )
                    elif metric == "avg_throughput_gbps":
                        limit = base_val * (1 - PERF_TOLERANCE)
                        if actual_val < limit:
                            failures.append(
                                f"{name}.{metric}: {actual_val:.3f} < {limit:.3f} "
                                f"(baseline={base_val:.3f}, -{PERF_TOLERANCE:.0%})"
                            )
            assert not failures, "Performance regression detected:\n" + "\n".join(
                failures
            )
        finally:
            if original_log_level is not None:
                os.environ["ASCEND_GLOBAL_LOG_LEVEL"] = original_log_level
            else:
                os.environ.pop("ASCEND_GLOBAL_LOG_LEVEL", None)
