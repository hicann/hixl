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

import argparse
import logging
import socket
import struct
import sys
import time

import torch
import hixl

logging.basicConfig(
    format="%(asctime)s [%(levelname)s] %(message)s", level=logging.INFO
)

BUF_SIZE = 8 * 1024 * 1024
BLOCK_SIZE = 16 * 1024
BLOCK_COUNT = BUF_SIZE // BLOCK_SIZE
DEFAULT_DEVICE_CLIENT = 0
DEFAULT_DEVICE_SERVER = 2
DEFAULT_CLIENT_ENGINE = "127.0.0.1:16000"
DEFAULT_SERVER_ENGINE = "127.0.0.1:16001"
CONNECT_TIMEOUT_MS = 5000
TRANSFER_TIMEOUT_MS = 30000
SOCKET_PORT_OFFSET = 1000
SOCKET_BACKLOG = 1
SOCKET_RETRY_COUNT = 10
SOCKET_RETRY_INTERVAL_S = 0.5
SOCKET_RETRY_TIMEOUT_S = 2.0
FILL_VALUE = 0xAA
DISCONNECT_WAIT_TIMEOUT_S = 5.0


def parse_engine_addr(engine_addr: str) -> tuple[str, int]:
    if engine_addr.startswith("["):
        bracket_end = engine_addr.find("]")
        if (
            bracket_end < 0
            or bracket_end + 1 >= len(engine_addr)
            or engine_addr[bracket_end + 1] != ":"
        ):
            raise ValueError(
                f"Invalid IPv6 engine address format: {engine_addr}, expected [host]:port"
            )
        host = engine_addr[1:bracket_end]
        port = int(engine_addr[bracket_end + 2 :])
    else:
        pos = engine_addr.rfind(":")
        if pos < 0:
            raise ValueError(
                f"Invalid engine address format: {engine_addr}, expected host:port"
            )
        host = engine_addr[:pos]
        port = int(engine_addr[pos + 1 :])
    return host, port


def build_default_options() -> dict:
    options = {
        hixl.OPTION_AUTO_CONNECT: "1",
    }
    return options


def setup_listen_socket(port: int) -> socket.socket:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", port))
        srv.listen(SOCKET_BACKLOG)
    except Exception:
        srv.close()
        raise
    return srv


def _wait_client_disconnect(conn) -> None:
    conn.settimeout(DISCONNECT_WAIT_TIMEOUT_S)
    try:
        while True:
            chunk = conn.recv(64)
            if not chunk:
                break
    except socket.timeout:
        logging.warning(
            "Server timed out waiting for client disconnect after %ss, "
            "proceeding anyway (client may have crashed)",
            DISCONNECT_WAIT_TIMEOUT_S,
        )
    finally:
        conn.settimeout(None)
    logging.info("Server done")


def server_send_addr(listen_port: int, dev_addr: int, timeout_s: float = 120.0):
    srv = setup_listen_socket(listen_port)
    srv.settimeout(timeout_s)
    logging.info(f"Server listening on port {listen_port}, waiting for client...")
    try:
        conn, _ = srv.accept()
    except socket.timeout as e:
        srv.close()
        raise RuntimeError(
            f"Server timed out waiting for client after {timeout_s}s"
        ) from e
    try:
        conn.settimeout(timeout_s)
        data = struct.pack("!Q", dev_addr)
        conn.sendall(data)
        logging.info(f"Server sent buffer addr 0x{dev_addr:X} to client")
        _wait_client_disconnect(conn)
    finally:
        conn.close()
        srv.close()


def _recv_exact(sock, size: int) -> bytes:
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("Server closed connection before sending address")
        data += chunk
    return data


def client_get_remote_addr(remote_engine: str):
    ip, port = parse_engine_addr(remote_engine)
    port += SOCKET_PORT_OFFSET
    sock = None
    for attempt in range(1, SOCKET_RETRY_COUNT + 1):
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(SOCKET_RETRY_TIMEOUT_S)
            sock.connect((ip, port))
            data = _recv_exact(sock, 8)
            remote_addr = struct.unpack("!Q", data)[0]
            logging.info(f"Client received remote addr 0x{remote_addr:X}")
            return remote_addr, sock
        except OSError:
            if sock:
                sock.close()
                sock = None
            logging.info(f"Connect retry {attempt}/{SOCKET_RETRY_COUNT}...")
            time.sleep(SOCKET_RETRY_INTERVAL_S)
    raise RuntimeError(
        f"Failed to connect to server at {ip}:{port} after {SOCKET_RETRY_COUNT} retries"
    )


def run_server(args):
    torch.npu.set_device(args.device)

    options = build_default_options()
    engine = hixl.Hixl()
    handle = None

    try:
        ret = engine.initialize(args.local_engine, options)
        if ret != hixl.SUCCESS:
            raise RuntimeError(f"Server Initialize failed, ret={ret}")
        logging.info("Server engine initialized")

        buf_tensor = torch.full(
            (BUF_SIZE,), FILL_VALUE, dtype=torch.uint8, device="npu"
        )
        dev_addr = int(buf_tensor.data_ptr())
        logging.info(f"Server NPU buffer at 0x{dev_addr:X}, size={BUF_SIZE}")

        mem_desc = hixl.MemDesc(dev_addr, BUF_SIZE)
        ret, handle = engine.register_mem(mem_desc, hixl.MemType.MEM_DEVICE)
        if ret != hixl.SUCCESS:
            raise RuntimeError(f"Server RegisterMem failed, ret={ret}")
        logging.info(f"Server RegisterMem success, handle=0x{handle:X}")

        listen_port = parse_engine_addr(args.local_engine)[1] + SOCKET_PORT_OFFSET
        server_send_addr(listen_port, dev_addr)

    finally:
        if handle is not None:
            ret = engine.deregister_mem(handle)
            if ret != hixl.SUCCESS:
                logging.warning(f"Server DeregisterMem ret={ret}")
        engine.finalize()
        logging.info("Server finalize done")


def _build_transfer_op_descs(dev_addr: int, remote_addr: int):
    op_descs = []
    for i in range(BLOCK_COUNT):
        local_offset = i * BLOCK_SIZE
        remote_offset = i * BLOCK_SIZE
        op_descs.append(
            hixl.TransferOpDesc(
                local_addr=dev_addr + local_offset,
                remote_addr=remote_addr + remote_offset,
                len=BLOCK_SIZE,
            )
        )
    return op_descs


def _verify_transferred_data(buf_tensor) -> None:
    host_data = buf_tensor.cpu().numpy()
    expected = bytearray([FILL_VALUE]) * BUF_SIZE
    if host_data.tobytes() != expected:
        mismatch_count = sum(1 for a, b in zip(host_data.tobytes(), expected) if a != b)
        raise RuntimeError(f"Verify FAILED: {mismatch_count} bytes mismatch")
    logging.info("Client Verify success — all bytes match 0xAA")


def _cleanup_client(engine, handle, sock, connected, remote_engine: str) -> None:
    if connected:
        ret = engine.disconnect(remote_engine, timeout_in_millis=CONNECT_TIMEOUT_MS)
        if ret != hixl.SUCCESS:
            logging.warning(f"Client Disconnect ret={ret}")
        else:
            logging.info("Client Disconnect success")
    if sock is not None:
        try:
            sock.close()
        except OSError as e:
            logging.warning(f"Client socket close failed: {e}")
    if handle is not None:
        ret = engine.deregister_mem(handle)
        if ret != hixl.SUCCESS:
            logging.warning(f"Client DeregisterMem ret={ret}")
    engine.finalize()
    logging.info("Client finalize done")


def run_client(args):
    torch.npu.set_device(args.device)

    options = build_default_options()
    engine = hixl.Hixl()
    handle = None
    sock = None
    connected = False

    try:
        ret = engine.initialize(args.local_engine, options)
        if ret != hixl.SUCCESS:
            raise RuntimeError(f"Client Initialize failed, ret={ret}")
        logging.info("Client engine initialized")

        buf_tensor = torch.zeros(BUF_SIZE, dtype=torch.uint8, device="npu")
        dev_addr = int(buf_tensor.data_ptr())
        logging.info(f"Client NPU buffer at 0x{dev_addr:X}, size={BUF_SIZE}")

        mem_desc = hixl.MemDesc(dev_addr, BUF_SIZE)
        ret, handle = engine.register_mem(mem_desc, hixl.MemType.MEM_DEVICE)
        if ret != hixl.SUCCESS:
            raise RuntimeError(f"Client RegisterMem failed, ret={ret}")
        logging.info(f"Client RegisterMem success, handle=0x{handle:X}")

        remote_addr, sock = client_get_remote_addr(args.remote_engine)

        ret = engine.connect(args.remote_engine, timeout_in_millis=CONNECT_TIMEOUT_MS)
        if ret != hixl.SUCCESS:
            raise RuntimeError(f"Client Connect failed, ret={ret}")
        connected = True
        logging.info("Client Connect success")

        op_descs = _build_transfer_op_descs(dev_addr, remote_addr)

        ret = engine.transfer_sync(
            args.remote_engine,
            hixl.TransferOp.READ,
            op_descs,
            timeout_in_millis=TRANSFER_TIMEOUT_MS,
        )
        if ret != hixl.SUCCESS:
            raise RuntimeError(f"Client TransferSync READ failed, ret={ret}")
        logging.info("Client TransferSync READ completed")

        _verify_transferred_data(buf_tensor)

    finally:
        _cleanup_client(engine, handle, sock, connected, args.remote_engine)


def parse_args():
    parser = argparse.ArgumentParser(description="HIXL D2RD multi-process sample")
    parser.add_argument("--role", required=True, choices=["client", "server"])
    parser.add_argument("--device", type=int, default=None)
    parser.add_argument("--local-engine", type=str, default=None)
    parser.add_argument("--remote-engine", type=str, default=None)
    args = parser.parse_args()

    is_client = args.role == "client"
    if args.device is None:
        args.device = DEFAULT_DEVICE_CLIENT if is_client else DEFAULT_DEVICE_SERVER
    if args.local_engine is None:
        args.local_engine = (
            DEFAULT_CLIENT_ENGINE if is_client else DEFAULT_SERVER_ENGINE
        )
    if args.remote_engine is None:
        args.remote_engine = (
            DEFAULT_SERVER_ENGINE if is_client else DEFAULT_CLIENT_ENGINE
        )

    if is_client:
        parse_engine_addr(args.remote_engine)
    else:
        parse_engine_addr(args.local_engine)

    logging.info(f"role={args.role}, device={args.device}")
    logging.info(f"  local={args.local_engine}, remote={args.remote_engine}")
    return args


def main():
    try:
        args = parse_args()
    except ValueError as e:
        logging.error(str(e))
        sys.exit(1)
    if args.role == "server":
        run_server(args)
    else:
        run_client(args)
    logging.info("Sample finished successfully")


if __name__ == "__main__":
    main()
