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

"""HIXL Python Benchmark main program.

Depends only on the Python standard library, the hixl wheel (pybind11 bindings)
and a local CANN runtime (libascendcl.so). No source compilation is required.

This file owns the engine-facing half: ACL access, buffers, the hixl engine
session lifecycle and the traffic loops. Option parsing, the coordination
channel and report rendering live in bench_lib; the unified ACL facade lives
in acl_compat.

Three launch modes:

- local single-machine dual-card (default, no options)
- server (target):      --listen [port]
- client (initiator):   --remote [ip][:port]

Examples:

    # Single-machine dual-card (default)
    python3 hixl_benchmark.py

    # Dual-machine: start the server first, then connect from the client
    python3 hixl_benchmark.py --listen
    python3 hixl_benchmark.py --remote 192.168.1.10:16000
"""

import ctypes
import logging
import os
import subprocess
import sys
import time

import hixl

from acl_compat import (
    ACL_HBM_MEM_HUGE,
    ACL_MEMCPY_DEVICE_TO_HOST,
    ACL_MEM_HANDLE_TYPE_NONE,
    ACL_MEM_LOCATION_TYPE_DEVICE,
    ACL_MEM_LOCATION_TYPE_HOST,
    ACL_MEM_LOCATION_TYPE_HOST_NUMA,
    ACL_MEM_MALLOC_HUGE_ONLY,
    ACL_MEM_P2P_HUGE,
    ACL_MEM_P2P_HUGE1G,
    ACL_RESERVE_FLAG_HUGE_PAGE,
    ACL_SUCCESS,
)

import acl_compat
from bench_lib import (
    BYTES_PER_MIB,
    DEFAULT_ENGINE_PORT,
    WARMUP_ROUNDS,
    CoordClient,
    CoordServer,
    Options,
    Report,
    StepResult,
    fail,
)

LOG = logging.getLogger("hixl_benchmark")

CONNECT_TIMEOUT_MS = 60000
TRANSFER_TIMEOUT_MS = 60000

SERVER_FILL_PATTERN = ord("S")
CLIENT_FILL_PATTERN = ord("C")

DEVICES_PER_CHIP = 4
NUMA_NODE_STEP = 2

acl = None


def setup_logging():
    logging.basicConfig(
        level=logging.INFO, format="[%(levelname)s] %(message)s", stream=sys.stderr
    )


def load_acl():
    try:
        return acl_compat.load()
    except acl_compat.AclUnavailableError as exc:
        fail(str(exc))


def load_mmpa():
    try:
        lib_path = acl_compat.find_cann_library("libmmpa.so")
        if lib_path is not None:
            mmpa = acl_compat.load_library_with_deps(lib_path, use_errno=True)
        else:
            mmpa = ctypes.CDLL("libmmpa.so", use_errno=True)
        mmpa.mmGetPageSize.argtypes = []
        mmpa.mmGetPageSize.restype = ctypes.c_size_t
        mmpa.mmAlignMalloc.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
        mmpa.mmAlignMalloc.restype = ctypes.c_void_p
        mmpa.mmAlignFree.argtypes = [ctypes.c_void_p]
        mmpa.mmAlignFree.restype = None
    except (OSError, AttributeError) as exc:
        fail(
            f"Load api:libmmpa.so failed, ret:{exc}, param:host roce buffer. "
            "Set ASCEND_HOME_PATH or source the CANN set_env.sh script"
        )
    return mmpa


class BenchBuffer:
    def __init__(
        self, addr, size, is_host, use_fabric, fabric_handle=None, host_mmpa=None
    ):
        self.addr = addr
        self.size = size
        self.is_host = is_host
        self.use_fabric = use_fabric
        self.fabric_handle = fabric_handle
        self.host_mmpa = host_mmpa


def alloc_buffer(size, is_host, use_fabric, host_roce=False):
    if use_fabric:
        return alloc_fabric_buffer(size, is_host)
    if is_host and host_roce:
        mmpa = load_mmpa()
        page_size = mmpa.mmGetPageSize()
        if page_size < ctypes.sizeof(ctypes.c_void_p) or page_size & (page_size - 1):
            fail(
                f"Call api:mmGetPageSize failed, ret:invalid page size:{page_size} bytes"
            )
        ctypes.set_errno(0)
        addr = mmpa.mmAlignMalloc(size, page_size)
        if not addr:
            fail(
                f"Call api:mmAlignMalloc failed, ret:errno:{ctypes.get_errno()}, "
                f"param:size:{size} bytes, alignment:{page_size} bytes"
            )
        return BenchBuffer(addr, size, is_host, False, host_mmpa=mmpa)
    if is_host:
        addr, ret = acl.rt.malloc_host(size)
        if ret != ACL_SUCCESS:
            fail(
                f"Call api:acl.rt.malloc_host failed, "
                f"ret:{ret}, param:size:{size} bytes, is_host:{is_host}"
            )
    else:
        addr, ret = acl.rt.malloc(size, ACL_MEM_MALLOC_HUGE_ONLY)
        if ret != ACL_SUCCESS:
            fail(
                f"Call api:acl.rt.malloc failed, "
                f"ret:{ret}, param:size:{size} bytes, is_host:{is_host}"
            )
    return BenchBuffer(addr, size, is_host, False)


def _physical_mem_prop(mem_attr, location_type, location_id):
    return {
        "handleType": ACL_MEM_HANDLE_TYPE_NONE,
        "allocationType": 0,
        "memAttr": mem_attr,
        "location": {"type": location_type, "id": location_id},
        "reserve": 0,
    }


def alloc_fabric_buffer(size, is_host):
    """Allocate fabric memory: physical memory plus virtual address mapping."""
    device, ret = acl.rt.get_device()
    if ret != ACL_SUCCESS:
        fail(f"Call api:acl.rt.get_device failed, ret:{ret}, param:none")
    if not is_host:
        prop = _physical_mem_prop(
            ACL_HBM_MEM_HUGE, ACL_MEM_LOCATION_TYPE_DEVICE, device
        )
        handle, ret = acl.rt.malloc_physical(size, prop, 0)
        if ret != ACL_SUCCESS:
            fail(
                f"Call api:acl.rt.malloc_physical failed, ret:{ret}, "
                f"param:size:{size} bytes, memAttr:ACL_HBM_MEM_HUGE, location:device/{device}"
            )
    else:
        phy_device, ret = acl.rt.get_phy_dev_id_by_logic_dev_id(device)
        if ret != ACL_SUCCESS:
            fail(
                f"Call api:acl.rt.get_phy_dev_id_by_logic_dev_id failed, "
                f"ret:{ret}, param:device:{device}"
            )
        numa_id = (phy_device // DEVICES_PER_CHIP) * NUMA_NODE_STEP
        attempts = [
            (ACL_MEM_P2P_HUGE1G, ACL_MEM_LOCATION_TYPE_HOST_NUMA, numa_id),
            (ACL_MEM_P2P_HUGE1G, ACL_MEM_LOCATION_TYPE_HOST, 0),
            (ACL_MEM_P2P_HUGE, ACL_MEM_LOCATION_TYPE_HOST, 0),
        ]
        handle = None
        ret = -1
        for mem_attr, location_type, location_id in attempts:
            prop = _physical_mem_prop(mem_attr, location_type, location_id)
            handle, ret = acl.rt.malloc_physical(size, prop, 0)
            if ret == ACL_SUCCESS:
                break
        if ret != ACL_SUCCESS:
            fail(
                f"Call api:acl.rt.malloc_physical failed, ret:{ret}, "
                f"param:size:{size} bytes, memAttr:HOST huge page, numa:{numa_id}"
            )
    virtual_address = None
    soc_name = acl.get_soc_name()
    prefer_no_uc = (
        acl.rt.has_reserve_no_uc_memory()
        and soc_name is not None
        and soc_name.startswith("Ascend910_93")
    )
    if prefer_no_uc:
        virtual_address, ret = acl.rt.reserve_mem_address_no_uc(size)
        if ret != ACL_SUCCESS:
            LOG.warning(
                "Call api:acl.rt.reserve_mem_address_no_uc failed, ret:%d, "
                "param:size:%d bytes, fall back to acl.rt.reserve_mem_address",
                ret,
                size,
            )
            virtual_address = None
    if virtual_address is None or virtual_address == 0:
        virtual_address, ret = acl.rt.reserve_mem_address(
            size, 0, 0, ACL_RESERVE_FLAG_HUGE_PAGE
        )
        if ret != ACL_SUCCESS:
            fail(
                f"Call api:acl.rt.reserve_mem_address failed, "
                f"ret:{ret}, param:size:{size} bytes"
            )
    ret = acl.rt.map_mem(virtual_address, size, 0, handle, 0)
    if ret != ACL_SUCCESS:
        fail(
            f"Call api:acl.rt.map_mem failed, ret:{ret}, "
            f"param:va:0x{virtual_address:X}, size:{size} bytes"
        )
    if is_host:
        access_device = device
        if not acl.rt.runtime_skips_user_to_logic_convert():
            access_device, ret = acl.rt.get_logic_dev_id_by_user_dev_id(device)
            if ret != ACL_SUCCESS:
                fail(
                    f"Call api:acl.rt.get_logic_dev_id_by_user_dev_id failed, "
                    f"ret:{ret}, param:device:{device}"
                )
        ret = acl.rt.mem_set_access(
            virtual_address, size, ACL_MEM_LOCATION_TYPE_DEVICE, access_device
        )
        if ret != ACL_SUCCESS:
            fail(
                f"Call api:acl.rt.mem_set_access failed, ret:{ret}, "
                f"param:va:0x{virtual_address:X}, size:{size} bytes"
            )
    return BenchBuffer(virtual_address, size, is_host, True, fabric_handle=handle)


def free_buffer(buffer):
    if buffer is None or buffer.addr == 0:
        return
    if buffer.use_fabric:
        ret = acl.rt.unmap_mem(buffer.addr)
        if ret != ACL_SUCCESS:
            LOG.warning(
                "Call api:acl.rt.unmap_mem failed, ret:%d, param:va:0x%X",
                ret,
                buffer.addr,
            )
        ret = acl.rt.release_mem_address(buffer.addr)
        if ret != ACL_SUCCESS:
            LOG.warning(
                "Call api:acl.rt.release_mem_address failed, ret:%d, param:va:0x%X",
                ret,
                buffer.addr,
            )
        ret = acl.rt.free_physical(buffer.fabric_handle)
        if ret != ACL_SUCCESS:
            LOG.warning(
                "Call api:acl.rt.free_physical failed, ret:%d, param:handle:0x%X",
                ret,
                buffer.fabric_handle or 0,
            )
        return
    if buffer.host_mmpa is not None:
        buffer.host_mmpa.mmAlignFree(buffer.addr)
        return
    if buffer.is_host:
        ret = acl.rt.free_host(buffer.addr)
        if ret != ACL_SUCCESS:
            LOG.warning(
                "Call api:acl.rt.free_host failed, ret:%d, param:addr:0x%X",
                ret,
                buffer.addr,
            )
    else:
        ret = acl.rt.free(buffer.addr)
        if ret != ACL_SUCCESS:
            LOG.warning(
                "Call api:acl.rt.free failed, ret:%d, param:addr:0x%X", ret, buffer.addr
            )


def fill_pattern(buffer, value):
    if buffer.host_mmpa is not None:
        ctypes.memset(buffer.addr, value, buffer.size)
        return
    ret = acl.rt.memset(buffer.addr, buffer.size, value, buffer.size)
    if ret != ACL_SUCCESS:
        fail(
            f"Call api:acl.rt.memset failed, ret:{ret}, "
            f"param:addr:0x{buffer.addr:X}, size:{buffer.size} bytes"
        )


def read_back_buffer(buffer):
    if buffer.is_host:
        return ctypes.string_at(buffer.addr, buffer.size)
    scratch = ctypes.create_string_buffer(buffer.size)
    ret = acl.rt.memcpy(
        ctypes.addressof(scratch),
        buffer.size,
        buffer.addr,
        buffer.size,
        ACL_MEMCPY_DEVICE_TO_HOST,
    )
    if ret != ACL_SUCCESS:
        fail(
            f"Call api:acl.rt.memcpy failed, ret:{ret}, "
            f"param:src:0x{buffer.addr:X}, count:{buffer.size} bytes, kind:device_to_host"
        )
    return scratch.raw


def verify_buffer(buffer, expected_value):
    """Compare the whole buffer against the expected fill byte value."""
    data = read_back_buffer(buffer)
    pattern = bytes([expected_value])
    chunk_size = BYTES_PER_MIB
    for start in range(0, len(data), chunk_size):
        end = min(start + chunk_size, len(data))
        if data[start:end] != pattern * (end - start):
            for offset in range(start, end):
                if data[offset] != expected_value:
                    return False, offset, expected_value, data[offset]
    return True, 0, expected_value, expected_value


def build_transfer_descs(hixl, local_addr, remote_addr, block_size, count):
    descs = []
    for index in range(count):
        offset = index * block_size
        descs.append(
            hixl.TransferOpDesc(
                local_addr=local_addr + offset,
                remote_addr=remote_addr + offset,
                len=block_size,
            )
        )
    return descs


def acl_setup(device_id):
    ret = acl.rt.set_device(device_id)
    if ret != ACL_SUCCESS:
        fail(
            f"Call api:acl.rt.set_device failed, ret:{ret}, param:device_id:{device_id}"
        )


def acl_teardown(device_id):
    ret = acl.rt.reset_device(device_id)
    if ret != ACL_SUCCESS:
        LOG.warning(
            "Call api:acl.rt.reset_device failed, ret:%d, param:device_id:%d",
            ret,
            device_id,
        )


class EngineSession:
    """hixl engine lifecycle: alloc -> register -> transfer -> disconnect -> deregister.

    Exiting guarantees disconnect / deregister / finalize / buffer free / ACL teardown
    in that order on both success and failure paths (mirrors hixl_comm_bench cleanup).
    """

    def __init__(self, config, target_side):
        self.hixl = hixl
        self.engine = self.hixl.Hixl()
        self.target_side = target_side
        self.engine_str = (
            config.target_engine if target_side else config.initiator_engine
        )
        self.device_id = (
            config.target_device if target_side else config.initiator_device
        )
        self.transfer_size = config.transfer_size
        self.is_host = (
            config.target_is_host if target_side else config.initiator_is_host
        )
        self.use_fabric = config.use_fabric
        self.host_roce = config.transport_display == "roce:host"
        ret = self.engine.initialize(self.engine_str, config.options)
        if ret != self.hixl.SUCCESS:
            fail(
                f"Call api:Hixl.initialize failed, ret:{ret}, "
                f"param:engine:{self.engine_str}, device:{self.device_id}"
            )
        self.buffer = None
        self.mem_handle = None
        self.connected = False

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, exc_tb):
        self.disconnect()
        self.deregister()
        self.engine.finalize()
        free_buffer(self.buffer)
        acl_teardown(self.device_id)

    def alloc(self):
        self.buffer = alloc_buffer(
            self.transfer_size, self.is_host, self.use_fabric, self.host_roce
        )
        return self.buffer

    def register(self):
        # Local fabric device buffers need no explicit RegisterMem; TransferSync takes
        # raw addresses directly (mirrors ShouldRegisterInitiatorMem in hixl_comm_bench).
        if not self.target_side and self.use_fabric and not self.buffer.is_host:
            return
        mem_desc = self.hixl.MemDesc(self.buffer.addr, self.buffer.size)
        mem_type = (
            self.hixl.MemType.MEM_HOST
            if self.buffer.is_host
            else self.hixl.MemType.MEM_DEVICE
        )
        ret, mem_handle = self.engine.register_mem(mem_desc, mem_type)
        if ret != self.hixl.SUCCESS:
            fail(
                f"Call api:Hixl.register_mem failed, ret:{ret}, "
                f"param:addr:0x{self.buffer.addr:X}, size:{self.buffer.size} bytes"
            )
        self.mem_handle = mem_handle

    def connect(self):
        ret = self.engine.connect(self.engine_str, timeout_in_millis=CONNECT_TIMEOUT_MS)
        if ret != self.hixl.SUCCESS:
            fail(
                f"Call api:Hixl.connect failed, ret:{ret}, "
                f"param:engine:{self.engine_str}, timeout:{CONNECT_TIMEOUT_MS}ms"
            )
        self.connected = True

    def disconnect(self):
        if not self.connected:
            return
        ret = self.engine.disconnect(
            self.engine_str, timeout_in_millis=CONNECT_TIMEOUT_MS
        )
        self.connected = False
        if ret != self.hixl.SUCCESS:
            LOG.warning(
                "Call api:Hixl.disconnect failed, ret:%d, param:engine:%s",
                ret,
                self.engine_str,
            )

    def deregister(self):
        if self.mem_handle is None:
            return
        ret = self.engine.deregister_mem(self.mem_handle)
        if ret != self.hixl.SUCCESS:
            LOG.warning("Call api:Hixl.deregister_mem failed, ret:%d", ret)


def run_target(config):
    acl_setup(config.target_device)
    coord = CoordServer(config.target_coord_port)
    conn = None
    try:
        conn = coord.accept()
        config.target_engine = f"{conn.getsockname()[0]}:{config.engine_port}"
        with EngineSession(config, target_side=True) as session:
            LOG.info(
                "Target ready, engine:%s, device:%d, acl source:%s",
                config.target_engine,
                config.target_device,
                acl.source,
            )
            buffer = session.alloc()
            fill_pattern(buffer, SERVER_FILL_PATTERN)
            session.register()
            coord.send_info(conn, buffer.addr, config.target_engine)
            LOG.info(
                "Target sent buffer address:0x%X, size:%d bytes, engine:%s",
                buffer.addr,
                buffer.size,
                config.target_engine,
            )

            def on_verify():
                return verify_buffer(buffer, CLIENT_FILL_PATTERN)

            coord.serve(conn, on_verify)
            LOG.info("Target received done, cleaning up")
    finally:
        if conn is not None:
            conn.close()
        coord.close()
    return 0


def warmup_step(session, transfer_op, descs, display):
    """Warm up one block step; excluded from statistics."""
    for _round in range(WARMUP_ROUNDS):
        ret = session.engine.transfer_sync(
            session.engine_str,
            transfer_op,
            descs,
            timeout_in_millis=TRANSFER_TIMEOUT_MS,
        )
        if ret != session.hixl.SUCCESS:
            fail(
                f"Call api:Hixl.transfer_sync failed, ret:{ret}, "
                f"param:block:{display}, stage:warmup"
            )


def measure_one_transfer_loop(session, config, transfer_op, descs, display):
    """Timed loops for one block step; re-arms C before the loops, verifies after the first read loop."""
    bandwidths = []
    mismatch = None
    if config.op_name == "read":
        # Warmup already pulled S into the buffer; re-arm C outside the timed
        # window so a successful-but-idle first loop fails the verify below.
        fill_pattern(session.buffer, CLIENT_FILL_PATTERN)
    for loop_index in range(config.loops):
        start = time.perf_counter()
        ret = session.engine.transfer_sync(
            session.engine_str,
            transfer_op,
            descs,
            timeout_in_millis=TRANSFER_TIMEOUT_MS,
        )
        elapsed = time.perf_counter() - start
        if ret != session.hixl.SUCCESS:
            fail(
                f"Call api:Hixl.transfer_sync failed, ret:{ret}, "
                f"param:block:{display}, loop:{loop_index + 1}/{config.loops}"
            )
        if elapsed > 0:
            bandwidths.append(config.transfer_size / 1e9 / elapsed)
        else:
            bandwidths.append(0.0)
        if loop_index == 0 and config.op_name == "read":
            ok, offset, expected, actual = verify_buffer(
                session.buffer, SERVER_FILL_PATTERN
            )
            if not ok:
                mismatch = (offset, expected, actual)
    return bandwidths, mismatch


def build_step_result(display, config, bandwidths):
    if bandwidths:
        peak = max(bandwidths)
        average = sum(bandwidths) / len(bandwidths)
    else:
        peak = 0.0
        average = 0.0
    return StepResult(
        display, config.loops, config.transfer_size_display, peak, average
    )


def transfer(session, config, remote_addr):
    """Run every block step: warmup -> timed loops -> read verification."""
    transfer_op = (
        session.hixl.TransferOp.READ
        if config.op_name == "read"
        else session.hixl.TransferOp.WRITE
    )
    results = []
    mismatch = None
    for block_size, display in config.block_steps:
        count = config.transfer_size // block_size
        descs = build_transfer_descs(
            session.hixl, session.buffer.addr, remote_addr, block_size, count
        )
        warmup_step(session, transfer_op, descs, display)
        bandwidths, step_mismatch = measure_one_transfer_loop(
            session, config, transfer_op, descs, display
        )
        if step_mismatch is not None:
            mismatch = step_mismatch
        results.append(build_step_result(display, config, bandwidths))
    LOG.info("Traffic end, completed steps:%d", len(results))
    return results, mismatch


def run_initiator(config):
    acl_setup(config.initiator_device)
    with EngineSession(config, target_side=False) as session:
        buffer = session.alloc()
        fill_pattern(buffer, CLIENT_FILL_PATTERN)
        session.register()
        coord = CoordClient(config.target_host, config.target_coord_port)
        verify_ok = True
        try:
            remote_addr, remote_engine = coord.get_remote_info()
            LOG.info(
                "Initiator received target buffer address:0x%X, engine:%s",
                remote_addr,
                remote_engine,
            )
            config.target_engine = remote_engine
            session.engine_str = remote_engine
            session.connect()
            LOG.info(
                "Traffic start, direction:%s, transport:%s, block steps:%d, loops:%d, "
                "transfer_size:%d bytes per loop, warmup:%d rounds per step",
                config.direction,
                config.transport_display,
                len(config.block_steps),
                config.loops,
                config.transfer_size,
                WARMUP_ROUNDS,
            )
            results, mismatch = transfer(session, config, remote_addr)
            if config.op_name == "write":
                ok, offset, expected, actual = coord.request_verify()
                if not ok:
                    mismatch = (offset, expected, actual)
            session.disconnect()
            coord.send_done()
            verify_ok = mismatch is None
            Report.render(config, results, verify_ok)
            if not verify_ok:
                offset, expected, actual = mismatch
                LOG.error(
                    "Data verify failed, first mismatch offset:%d bytes, "
                    "expected:0x%02X, actual:0x%02X",
                    offset,
                    expected,
                    actual,
                )
        finally:
            coord.close()
    return 0 if verify_ok else 1


def run_local(config):
    command = (
        [sys.executable, os.path.abspath(sys.argv[0])]
        + sys.argv[1:]
        + [
            "--listen",
            str(DEFAULT_ENGINE_PORT),
            "--device",
            str(config.spawn_device),
        ]
    )
    LOG.info("Spawning server subprocess, cmd:%s", " ".join(command))
    proc = subprocess.Popen(command)
    try:
        exit_code = run_initiator(config)
    except BaseException:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        raise
    target_code = proc.wait()
    if target_code != 0:
        LOG.error("Target subprocess exited with code:%d", target_code)
        return 1
    return exit_code


def main():
    global acl
    setup_logging()
    args = Options.parse_args(sys.argv[1:])
    config = Options.resolve(args)
    if config.visible_devices is not None:
        os.environ["ASCEND_RT_VISIBLE_DEVICES"] = ",".join(
            str(item) for item in config.visible_devices
        )
    acl = load_acl()
    if config.mode == "server":
        return run_target(config)
    if config.mode == "client":
        return run_initiator(config)
    return run_local(config)


if __name__ == "__main__":
    sys.exit(main())
