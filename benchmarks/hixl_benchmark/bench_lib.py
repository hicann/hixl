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

"""Support library for the HIXL Python Benchmark.

Hosts everything that does not touch ACL or the hixl engine:

- ``Options`` / ``RunConfig``: CLI and environment option parsing;
- ``CoordServer`` / ``CoordClient``: out-of-band coordination channel;
- ``Report`` / ``StepResult``: result table rendering.
"""

import argparse
import json
import logging
import os
import socket
import struct
import time

LOG = logging.getLogger("hixl_benchmark")

DEFAULT_BLOCK_SIZES = "16K:2M"
DEFAULT_TRANSFER_SIZE = "128M"
DEFAULT_LOOPS = 1000
WARMUP_ROUNDS = 5
DEFAULT_ENGINE_PORT = 16000
COORD_PORT_OFFSET = 10000  # internal derivation: coordination port = engine port + offset; not user-facing
MAX_ENGINE_PORT = 65535 - COORD_PORT_OFFSET
COORD_CONNECT_TIMEOUT_S = 60.0
COORD_ACCEPT_TIMEOUT_S = 300.0
COORD_SESSION_TIMEOUT_S = 1800.0
BYTES_PER_KIB = 1024
BYTES_PER_MIB = 1024 * BYTES_PER_KIB
BYTES_PER_GIB = 1024 * BYTES_PER_MIB

DIRECTION_TABLE = {
    "D2rD": ("device", "device", "write"),
    "rD2D": ("device", "device", "read"),
    "D2rH": ("device", "host", "write"),
    "rH2D": ("device", "host", "read"),
    "H2rH": ("host", "host", "write"),
    "rH2H": ("host", "host", "read"),
    "H2rD": ("host", "device", "write"),
    "rD2H": ("host", "device", "read"),
}

TRANSPORT_SHORTHAND = {
    "hccs": "hccs:device",
    "uboe": "uboe:device",
    "ub_rtp": "ub_rtp:device",
}
TRANSPORT_PASSTHROUGH = frozenset(
    [
        "hccs:device",
        "roce:device",
        "roce:host",
        "uboe:device",
        "ub_rtp:device",
        "ub_ctp",
        "ub_ctp:device",
        "ub_ctp:host",
    ]
)
TRANSPORT_UB_MEM = "ub_mem"

OPTION_ENABLE_USE_FABRIC_MEM = "EnableUseFabricMem"
OPTION_GLOBAL_RESOURCE_CONFIG = "GlobalResourceConfig"
OPTION_LOCAL_COMM_RES = "LocalCommRes"
OPTION_BUFFER_POOL = "BufferPool"

COORD_MSG_VERIFY_REQUEST = 1
COORD_MSG_DONE = 2
COORD_INFO_FORMAT = "!QH"  # buffer address + engine string length
COORD_VERIFY_FORMAT = "!BQBB"

SIZE_SUFFIX_FACTORS = {
    "": 1,
    "k": BYTES_PER_KIB,
    "kb": BYTES_PER_KIB,
    "kib": BYTES_PER_KIB,
    "m": BYTES_PER_MIB,
    "mb": BYTES_PER_MIB,
    "mib": BYTES_PER_MIB,
    "g": BYTES_PER_GIB,
    "gb": BYTES_PER_GIB,
    "gib": BYTES_PER_GIB,
    "t": BYTES_PER_GIB * BYTES_PER_KIB,
    "tb": BYTES_PER_GIB * BYTES_PER_KIB,
    "tib": BYTES_PER_GIB * BYTES_PER_KIB,
}


def fail(message):
    LOG.error("%s", message)
    raise SystemExit(1)


class RunConfig:
    def __init__(self):
        self.mode = "local"
        self.options = {}
        self.direction = "D2rD"
        self.op_name = "write"
        self.initiator_is_host = False
        self.target_is_host = False
        self.block_steps = []
        self.transfer_size = 0
        self.transfer_size_display = ""
        self.loops = DEFAULT_LOOPS
        self.transport_display = "default"
        self.use_fabric = False
        self.target_device = 0
        self.target_engine = ""
        self.engine_port = DEFAULT_ENGINE_PORT
        self.spawn_device = 0
        self.target_host = "127.0.0.1"
        self.target_coord_port = DEFAULT_ENGINE_PORT + COORD_PORT_OFFSET
        self.initiator_device = 0
        self.initiator_engine = "127.0.0.1:0"
        self.device_display = "0 -> 1"
        self.visible_devices = None


class Options:
    """CLI and environment option parsing for the benchmark."""

    @staticmethod
    def parse_args(argv):
        parser = argparse.ArgumentParser(
            prog="hixl_benchmark.py",
            description="HIXL Python Benchmark",
        )
        parser.add_argument(
            "--device",
            metavar="LIST",
            default=None,
            help="comma-separated device ids: 0,1 for local mode, 0 for server/client; "
            "overrides ASCEND_RT_VISIBLE_DEVICES",
        )
        parser.add_argument(
            "--direction",
            default="D2rD",
            choices=sorted(DIRECTION_TABLE),
            help="transfer direction (default: D2rD)",
        )
        parser.add_argument(
            "--transport",
            metavar="TYPE",
            default=None,
            help="shorthand hccs|uboe|ub_rtp; passthrough hccs:device|roce:device|roce:host|"
            "uboe:device|ub_rtp:device|ub_ctp|ub_ctp:device|ub_ctp:host; ub_mem enables fabric mem; "
            "omit to let hixl pick the default route",
        )
        parser.add_argument(
            "--block_sizes",
            metavar="LIST|RANGE",
            default=DEFAULT_BLOCK_SIZES,
            help="block size list (4K,64K,1M) or power-of-two range (16K:2M), units K/M/G/T "
            f"(default: {DEFAULT_BLOCK_SIZES})",
        )
        parser.add_argument(
            "--transfer_size",
            metavar="SIZE",
            default=DEFAULT_TRANSFER_SIZE,
            help=f"bytes transferred per block step, also the buffer size (default: {DEFAULT_TRANSFER_SIZE})",
        )
        parser.add_argument(
            "--loops",
            metavar="N",
            type=int,
            default=DEFAULT_LOOPS,
            help=f"timed loop count per block step, after {WARMUP_ROUNDS} warmup rounds "
            f"(default: {DEFAULT_LOOPS})",
        )
        parser.add_argument(
            "--listen",
            metavar="[PORT]",
            nargs="?",
            const=DEFAULT_ENGINE_PORT,
            type=int,
            help=f"run as server (target); port defaults to {DEFAULT_ENGINE_PORT}",
        )
        parser.add_argument(
            "--remote",
            metavar="[IP][:PORT]",
            nargs="?",
            const="",
            default=None,
            help="run as client (initiator); defaults to 127.0.0.1:16000",
        )
        return parser.parse_args(argv)

    @classmethod
    def resolve(cls, args):
        if args.listen is not None and args.remote is not None:
            fail("Options --listen and --remote are mutually exclusive")
        if args.listen is not None and not 1 <= args.listen <= MAX_ENGINE_PORT:
            fail(
                f"Invalid --listen port:{args.listen}, valid range is [1, {MAX_ENGINE_PORT}] "
                f"(coordination port is engine port + {COORD_PORT_OFFSET})"
            )
        if args.loops <= 0:
            fail(f"Invalid --loops:{args.loops}, it must be a positive integer")
        remote_endpoint = cls._parse_remote(args.remote)
        if remote_endpoint is not None:
            host, port = remote_endpoint
            if not 1 <= port <= MAX_ENGINE_PORT:
                fail(
                    f"Invalid --remote port:{port}, valid range is [1, {MAX_ENGINE_PORT}] "
                    f"(coordination port is engine port + {COORD_PORT_OFFSET})"
                )
        try:
            transfer_size = cls._parse_size(args.transfer_size)
        except ValueError as exc:
            fail(f"Invalid --transfer_size:{args.transfer_size}, {exc}")
        block_steps = cls._parse_block_steps(args.block_sizes, transfer_size)
        transport_value, use_fabric = cls._resolve_transport(args.transport)
        options = cls._build_options(transport_value)
        initiator_mem, target_mem, op_name = DIRECTION_TABLE[args.direction]

        config = RunConfig()
        config.options = options
        config.direction = args.direction
        config.op_name = op_name
        config.initiator_is_host = initiator_mem == "host"
        config.target_is_host = target_mem == "host"
        config.block_steps = block_steps
        config.transfer_size = transfer_size
        config.transfer_size_display = (
            args.transfer_size
            if cls._has_size_unit(args.transfer_size)
            else str(transfer_size)
        )
        config.loops = args.loops
        config.transport_display = (
            transport_value if transport_value is not None else "default"
        )
        config.use_fabric = use_fabric

        if args.listen is not None:
            mode = "server"
        elif remote_endpoint is not None:
            mode = "client"
        else:
            mode = "local"
        spawn_device = None
        if args.device is not None:
            expected_count = 2 if mode == "local" else 1
            mode_name = "local" if expected_count == 2 else "server/client"
            devices = cls._parse_device_list(args.device, expected_count, mode_name)
            config.visible_devices = devices
            config.initiator_device = 0
            config.target_device = 1 if expected_count == 2 else 0
            config.device_display = " -> ".join(str(item) for item in devices)
            if expected_count == 2:
                spawn_device = devices[1]
        else:
            config.initiator_device = 0
            config.target_device = 1 if mode == "local" else 0
            config.device_display = "0 -> 1" if mode == "local" else "0"
            if mode == "local":
                spawn_device = cls._local_spawn_device()

        if mode == "server":
            config.mode = mode
            config.engine_port = args.listen
            config.target_coord_port = args.listen + COORD_PORT_OFFSET
        elif mode == "client":
            config.mode = mode
            host, port = remote_endpoint
            config.target_host = host
            config.target_coord_port = port + COORD_PORT_OFFSET
        else:
            config.mode = mode
            config.target_host = "127.0.0.1"
            config.target_coord_port = DEFAULT_ENGINE_PORT + COORD_PORT_OFFSET
            config.spawn_device = spawn_device
        return config

    @staticmethod
    def _parse_size(text):
        """Parse a byte size such as 16K, 128M, 1G or a plain byte count."""
        value = text.strip()
        if not value:
            raise ValueError(f"empty size: {text}")
        digits = 0
        while digits < len(value) and value[digits].isdigit():
            digits += 1
        if digits == 0:
            raise ValueError(f"invalid size: {text}")
        suffix = value[digits:].lower()
        factor = SIZE_SUFFIX_FACTORS.get(suffix)
        if factor is None:
            raise ValueError(f"invalid size: {text}")
        number = int(value[:digits])
        if number <= 0:
            raise ValueError(f"invalid size: {text}")
        return number * factor

    @staticmethod
    def _has_size_unit(text):
        return not text.strip().isdigit()

    @staticmethod
    def _human_size_bytes(size):
        if size % BYTES_PER_GIB == 0:
            return f"{size // BYTES_PER_GIB}G"
        if size % BYTES_PER_MIB == 0:
            return f"{size // BYTES_PER_MIB}M"
        if size % BYTES_PER_KIB == 0:
            return f"{size // BYTES_PER_KIB}K"
        return str(size)

    @classmethod
    def _parse_block_steps(cls, spec, transfer_size):
        """Expand a block size list or power-of-two range into (size, display) pairs."""
        steps = []
        unit_style = any(cls._has_size_unit(entry) for entry in spec.split(","))
        for entry in spec.split(","):
            entry = entry.strip()
            if not entry:
                continue
            if ":" in entry:
                if entry.count(":") != 1:
                    fail(
                        f"Invalid --block_sizes entry:{entry}, at most one ':' is allowed"
                    )
                start_text, end_text = entry.split(":")
                try:
                    start = cls._parse_size(start_text)
                    end = cls._parse_size(end_text)
                except ValueError as exc:
                    fail(f"Invalid --block_sizes entry:{entry}, {exc}")
                if start <= 0 or end < start:
                    fail(
                        f"Invalid --block_sizes range:{entry}, require 0 < start <= end"
                    )
                current = start
                while current <= end:
                    display = (
                        cls._human_size_bytes(current) if unit_style else str(current)
                    )
                    steps.append((current, display))
                    if current == end:
                        break
                    current *= 2
                    if current > end:
                        fail(
                            f"Invalid --block_sizes range:{entry}, end is not reachable by doubling start"
                        )
            else:
                try:
                    size = cls._parse_size(entry)
                except ValueError as exc:
                    fail(f"Invalid --block_sizes entry:{entry}, {exc}")
                steps.append((size, entry))
        if not steps:
            fail(f"Invalid --block_sizes:{spec}, no block size parsed")
        for size, _display in steps:
            if size > transfer_size:
                fail(
                    f"Invalid block size:{size} bytes, it must not exceed transfer_size:{transfer_size} bytes"
                )
            if transfer_size % size != 0:
                fail(
                    f"Invalid block size:{size} bytes, transfer_size:{transfer_size} bytes must be divisible by it"
                )
        return steps

    @staticmethod
    def _build_options(transport_value):
        """Build hixl initialize options and apply environment variable rules."""
        options = {}
        if transport_value == TRANSPORT_UB_MEM:
            options[OPTION_ENABLE_USE_FABRIC_MEM] = "1"
        elif transport_value is not None:
            resource_config = {"comm_resource_config.protocol_desc": [transport_value]}
            options[OPTION_GLOBAL_RESOURCE_CONFIG] = json.dumps(resource_config)
        global_resource_config = os.environ.get("ASCEND_GLOBAL_RESOURCE_CONFIG")
        if global_resource_config:
            options[OPTION_GLOBAL_RESOURCE_CONFIG] = global_resource_config
        local_comm_res = os.environ.get("ASCEND_LOCAL_COMM_RES")
        if local_comm_res:
            options[OPTION_LOCAL_COMM_RES] = local_comm_res
            try:
                parsed = json.loads(local_comm_res)
            except json.JSONDecodeError as exc:
                fail(f"Invalid ASCEND_LOCAL_COMM_RES, not valid JSON: {exc}")
            if not isinstance(parsed, dict):
                fail("Invalid ASCEND_LOCAL_COMM_RES, expected a JSON object")
            if parsed.get("version") != "1.3":
                options[OPTION_BUFFER_POOL] = "0:0"
        return options

    @staticmethod
    def _parse_remote(value):
        """Parse --remote value into (host, port); omitted IP means localhost."""
        if value is None:
            return None
        if value == "":
            return "127.0.0.1", DEFAULT_ENGINE_PORT
        if value.startswith(":"):
            try:
                return "127.0.0.1", int(value[1:])
            except ValueError:
                fail(f"Invalid --remote:{value}, port must be an integer")
        if value.startswith("["):
            end = value.find("]")
            if end < 0 or end + 1 >= len(value) or value[end + 1] != ":":
                fail(f"Invalid --remote:{value}, expected [ipv6]:port")
            host = value[1:end]
            if not host:
                fail(f"Invalid --remote:{value}, IPv6 host must not be empty")
            try:
                port = int(value[end + 2 :])
            except ValueError:
                fail(f"Invalid --remote:{value}, port must be an integer")
            return host, port
        if ":" in value:
            host, port_text = value.rsplit(":", 1)
            try:
                port = int(port_text)
            except ValueError:
                fail(f"Invalid --remote:{value}, port must be an integer")
        else:
            host = value
            port = DEFAULT_ENGINE_PORT
        if not host:
            fail(f"Invalid --remote:{value}, host is empty")
        return host, port

    @staticmethod
    def _parse_device_list(value, expected_count, mode_name):
        devices = []
        for item in value.split(","):
            item = item.strip()
            if not item:
                continue
            try:
                device = int(item)
            except ValueError:
                fail(f"Invalid --device:{value}, segment:{item} is not an integer")
            if device < 0:
                fail(f"Invalid --device:{value}, segment:{item} must be non-negative")
            devices.append(device)
        if len(devices) != expected_count:
            fail(
                f"Invalid --device:{value}, {mode_name} mode requires exactly {expected_count} device(s)"
            )
        return devices

    @staticmethod
    def _local_spawn_device():
        """Resolve the physical device id the local-mode server child should bind."""
        env_value = os.environ.get("ASCEND_RT_VISIBLE_DEVICES", "")
        devices = []
        for item in env_value.split(","):
            item = item.strip()
            if item.isdigit():
                devices.append(int(item))
        if len(devices) >= 2:
            return devices[1]
        return 1

    @staticmethod
    def _resolve_transport(value):
        """Resolve --transport into (effective value for display, use_fabric flag)."""
        if value is None:
            return None, False
        if value == TRANSPORT_UB_MEM:
            return value, True
        if value == "fabric_mem":
            fail("Invalid --transport fabric_mem, use ub_mem instead")
        if value == "roce":
            fail(
                "Invalid --transport roce, specify placement explicitly, e.g. roce:device or roce:host"
            )
        if value in TRANSPORT_SHORTHAND:
            return TRANSPORT_SHORTHAND[value], False
        if value in TRANSPORT_PASSTHROUGH:
            return value, False
        fail(
            f"Invalid --transport:{value}, supported: "
            f"{','.join(sorted(TRANSPORT_SHORTHAND))}, {TRANSPORT_UB_MEM} or one of "
            f"{','.join(sorted(TRANSPORT_PASSTHROUGH))}"
        )


def _recv_exact(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("coordination peer closed the connection")
        data += chunk
    return data


class CoordServer:
    """Target-side coordination channel: exchange address, serve verify and done."""

    def __init__(self, port):
        self._server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self._server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._server.bind(("0.0.0.0", port))
            self._server.listen(1)
        except OSError as exc:
            self._server.close()
            fail(f"Call api:socket.bind failed, ret:{exc}, param:0.0.0.0:{port}")
        self._server.settimeout(COORD_ACCEPT_TIMEOUT_S)

    def accept(self):
        LOG.info(
            "Waiting for initiator on coordination port:%d",
            self._server.getsockname()[1],
        )
        try:
            conn, _peer = self._server.accept()
        except OSError as exc:
            fail(
                f"Call api:socket.accept failed, ret:{exc}, "
                f"param:timeout:{COORD_ACCEPT_TIMEOUT_S:.0f}s"
            )
        conn.settimeout(COORD_SESSION_TIMEOUT_S)
        return conn

    @staticmethod
    def send_info(conn, addr, engine_str):
        payload = engine_str.encode("utf-8")
        try:
            conn.sendall(struct.pack(COORD_INFO_FORMAT, addr, len(payload)) + payload)
        except OSError as exc:
            conn.close()
            fail(
                f"Call api:socket.sendall failed, ret:{exc}, "
                f"param:buffer address:0x{addr:X}, engine:{engine_str}"
            )

    def serve(self, conn, verify_handler):
        while True:
            try:
                message = _recv_exact(conn, 1)
            except (OSError, ConnectionError) as exc:
                fail(
                    f"Call api:socket.recv failed, ret:{exc}, param:coordination message"
                )
            code = message[0]
            if code == COORD_MSG_DONE:
                return
            if code == COORD_MSG_VERIFY_REQUEST:
                ok, offset, expected, actual = verify_handler()
                try:
                    conn.sendall(
                        struct.pack(COORD_VERIFY_FORMAT, ok, offset, expected, actual)
                    )
                except OSError as exc:
                    fail(
                        f"Call api:socket.sendall failed, ret:{exc}, param:verify result"
                    )
                continue
            fail(f"Coordination channel received unexpected message:{code}")

    def close(self):
        self._server.close()


class CoordClient:
    """Initiator-side coordination channel."""

    def __init__(self, host, port):
        deadline = time.monotonic() + COORD_CONNECT_TIMEOUT_S
        last_error = None
        sock = None
        while time.monotonic() < deadline:
            try:
                sock = socket.create_connection((host, port), timeout=5.0)
                break
            except OSError as exc:
                last_error = exc
                sock = None
                time.sleep(0.5)
        if sock is None:
            fail(
                f"Call api:socket.create_connection failed, ret:{last_error}, "
                f"param:{host}:{port}, timeout:{COORD_CONNECT_TIMEOUT_S:.0f}s"
            )
        self._sock = sock
        self._sock.settimeout(COORD_SESSION_TIMEOUT_S)

    def get_remote_info(self):
        try:
            header = _recv_exact(self._sock, struct.calcsize(COORD_INFO_FORMAT))
            addr, engine_len = struct.unpack(COORD_INFO_FORMAT, header)
            engine_str = _recv_exact(self._sock, engine_len).decode("utf-8")
        except (OSError, ConnectionError, UnicodeDecodeError) as exc:
            fail(f"Call api:socket.recv failed, ret:{exc}, param:target info")
        return addr, engine_str

    def request_verify(self):
        try:
            self._sock.sendall(bytes([COORD_MSG_VERIFY_REQUEST]))
            data = _recv_exact(self._sock, struct.calcsize(COORD_VERIFY_FORMAT))
        except (OSError, ConnectionError) as exc:
            fail(f"Call api:socket.sendall failed, ret:{exc}, param:verify request")
        return struct.unpack(COORD_VERIFY_FORMAT, data)

    def send_done(self):
        try:
            self._sock.sendall(bytes([COORD_MSG_DONE]))
        except OSError as exc:
            fail(f"Call api:socket.sendall failed, ret:{exc}, param:done message")

    def close(self):
        self._sock.close()


class StepResult:
    def __init__(self, display, loops, transfer_display, peak, average):
        self.display = display
        self.loops = loops
        self.transfer_display = transfer_display
        self.peak = peak
        self.average = average


class Report:
    """Render the benchmark result table."""

    @staticmethod
    def render(config, results, verify_ok):
        title = f"HIXL {'Write' if config.op_name == 'write' else 'Read'} BW Test"
        separator = "-" * 87
        lines = [
            separator,
            title.center(87).rstrip(),
            f" Direction       : {config.direction:<12}Transport type : {config.transport_display}",
            f" Device ID       : {config.device_display}",
            f" Local engine    : {config.initiator_engine:<24}Remote engine : {config.target_engine}",
            f" {separator}",
            f" {'block_size':<12}{'loops':<8}{'transfer_size_per_loop':<24}"
            f"{'BW peak[GB/sec]':<18}{'BW average[GB/sec]'}",
        ]
        for result in results:
            lines.append(
                f" {result.display:<12}{result.loops:<8}{result.transfer_display:<24}"
                f"{result.peak:<18.3f}{result.average:.3f}"
            )
        lines.append(f" {separator}")
        lines.append(f" Data verify    : {'pass' if verify_ok else 'fail'}")
        if config.transport_display == "default":
            lines.append(
                " Default transport type  : A2/A3: hccs within a supernode, roce across supernodes"
            )
            lines.append(
                " " * 26
                + "A5: ub_ctp within a supernode, roce/uboe/ub_rtp across supernodes"
            )
        print("\n".join(lines), flush=True)
