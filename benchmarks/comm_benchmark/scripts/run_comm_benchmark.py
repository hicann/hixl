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

"""Launch HIXL communication benchmark processes.

Examples (single machine — default):
  python3 run_comm_benchmark.py --type=D2rD --transport=hccs
  python3 run_comm_benchmark.py --transport=rdma --device_ids=0,1   # all rdma directions
  python3 run_comm_benchmark.py --type=D2rH --transport=rdma --device_ids=0,1
  python3 run_comm_benchmark.py --pattern=one_to_many --device_ids=0,1,2,3,4 --type=D2rD
  python3 run_comm_benchmark.py --pattern=many_to_one --device_ids=0,1,2,3,4 --type=D2rD
  python3 run_comm_benchmark.py --pattern=pairwise --device_ids=0,1,2,3 --type=D2rD
  python3 run_comm_benchmark.py --type=D2rD -H 'LocalCommRes={"version":"1.3"}'
  python3 run_comm_benchmark.py --type=D2rD --transport=hccs --skip_plot

Examples (dual machine):
  # 1:1 (default): runs all transports × directions
  python3 run_comm_benchmark.py --role=target
  python3 run_comm_benchmark.py --role=initiator --target-host=10.0.0.1

  # one_to_many: N targets on target machine (device_ids=0,1,2), 1 initiator on peer
  python3 run_comm_benchmark.py --role=target --pattern=one_to_many --device_ids=0,1,2

  # many_to_one: 1 target on target machine, N initiators on peer (see printed command)
  python3 run_comm_benchmark.py --role=target --pattern=many_to_one --num_initiators=3
"""
from __future__ import annotations

import argparse
import csv
import logging
import socket
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

BENCHMARKS_DIR = Path(__file__).resolve().parents[2]
if str(BENCHMARKS_DIR) not in sys.path:
    sys.path.insert(0, str(BENCHMARKS_DIR))
from benchmark_log import configure_logging  # noqa: E402

configure_logging()
log = logging.getLogger(__name__)

TYPE_MAP = {
    'D2rD': ('device', 'device', 'write'),
    'rD2D': ('device', 'device', 'read'),
    'D2rH': ('device', 'host', 'write'),
    'rH2D': ('device', 'host', 'read'),
    'H2rH': ('host', 'host', 'write'),
    'rH2H': ('host', 'host', 'read'),
    'H2rD': ('host', 'device', 'write'),
    'rD2H': ('host', 'device', 'read'),
}
ALL_TYPES = list(TYPE_MAP.keys())
TRANSPORTS_A2 = ['hccs', 'rdma']
TRANSPORTS_A3 = ['hccs', 'rdma', 'fabric_mem']
TRANSPORTS_A5 = ['rdma', 'fabric_mem', 'uboe']
DEFAULT_START_BLOCK = 16384
DEFAULT_MAX_BLOCK = 2097152
BLOCK_SORT_ORDER = ['16K', '32K', '64K', '128K', '256K', '512K', '1M', '2M', '4M', '8M']
DEFAULT_TCP_ACCEPT_WAIT_SINGLE = 30
DEFAULT_TCP_ACCEPT_WAIT_ALL = 300
DEFAULT_INTER_RUN_DELAY_SEC = 3
DEFAULT_CONNECT_TIMEOUT_MS = 60000
DEFAULT_DUAL_DEVICE_IDS = '0'
DEFAULT_SINGLE_DEVICE_IDS = '0,1'
RENDER_PERF_SCRIPT = BENCHMARKS_DIR / 'performance' / 'render_perf_md.py'


def detect_platform_noninteractive():
    """Detect A2/A3/A5 from npu-smi info without prompting (aligned with run_all_benchmarks)."""
    if str(BENCHMARKS_DIR) not in sys.path:
        sys.path.insert(0, str(BENCHMARKS_DIR))
    try:
        from platform_detect import detect_platform_from_npu_smi
        return detect_platform_from_npu_smi()
    except Exception:
        return None


def bench_soc_variant_for_bin(args) -> str:
    """Value forwarded as --soc_variant to hixl_comm_bench."""
    if args.soc_variant is not None:
        return args.soc_variant
    detected = detect_platform_noninteractive()
    if detected is not None:
        return detected
    return 'auto'


def effective_soc_for_hccs_gate(args) -> str | None:
    """When return value is a2/a3/a5, Python may reject illegal combos early; None defers to binary ACL probe."""
    if args.soc_variant is not None:
        if args.soc_variant == 'auto':
            return detect_platform_noninteractive()
        return args.soc_variant
    return detect_platform_noninteractive()


def _apply_csv_transport_row(row: dict[str, str], transports: set[str], directions: set[str]) -> None:
    transport = row.get('transport', '').strip()
    direction = row.get('direction', '').strip()
    if transport:
        transports.add(transport)
    if direction:
        directions.add(direction)


def _read_csv_transport_info(csv_file) -> tuple[set[str], set[str]]:
    transports: set[str] = set()
    directions: set[str] = set()
    for row in csv.DictReader(csv_file):
        _apply_csv_transport_row(row, transports, directions)
    return transports, directions


def _collect_csv_transport_info(csv_path: Path) -> tuple[set[str], set[str]]:
    try:
        with csv_path.open(newline='', encoding='utf-8') as csv_file:
            return _read_csv_transport_info(csv_file)
    except OSError:
        return set(), set()


def infer_platform_from_csv_dir(csv_dir: Path) -> str | None:
    """Infer a2/a3/a5 from collected comm CSV rows when SOC probe is unavailable."""
    all_transports: set[str] = set()
    all_directions: set[str] = set()
    for csv_path in csv_dir.glob('comm_result_*.csv'):
        transports, directions = _collect_csv_transport_info(csv_path)
        all_transports.update(transports)
        all_directions.update(directions)
    if not all_transports:
        return None
    if 'hccs' in all_transports and ('H2rD' in all_directions or 'rD2H' in all_directions):
        return 'a3'
    if 'fabric_mem' in all_transports:
        return 'a5' if 'hccs' not in all_transports else 'a3'
    if 'hccs' in all_transports:
        return 'a2'
    return None


def resolve_report_platform(args, gate_soc: str | None = None, csv_dir: Path | None = None) -> str:
    """Platform label for perf.md / terminal table (prefer runtime SOC gate, then probe, then CSV)."""
    if args.soc_variant is not None and args.soc_variant != 'auto':
        return args.soc_variant
    if gate_soc in ('a2', 'a3', 'a5'):
        return gate_soc
    detected = detect_platform_noninteractive()
    if detected is not None:
        return detected
    if csv_dir is not None:
        inferred = infer_platform_from_csv_dir(csv_dir)
        if inferred is not None:
            log.info(f'[INFO] Infer report platform {inferred} from CSV transports/directions')
            return inferred
    log.warning('[WARN] SOC detection failed; report platform defaults to a2')
    return 'a2'


def platform_for_report(args, gate_soc: str | None = None, csv_dir: Path | None = None) -> str:
    return resolve_report_platform(args, gate_soc, csv_dir)


def hccs_combo_allowed(transport: str, bench_type: str, gate_soc: str | None) -> bool:
    if transport != 'hccs':
        return True
    if gate_soc == 'a5':
        return False
    (im, tm, _op) = TYPE_MAP[bench_type]
    if im == 'device' and tm == 'device':
        return True
    if im == 'host' and tm == 'device':
        if gate_soc == 'a2':
            return False
        if gate_soc == 'a3':
            return True
        return True
    return False


def supported_types_for_transport(transport: str, gate_soc: str | None) -> list[str]:
    """Return ordered direction names supported for transport on this SOC."""
    if transport == 'hccs' and gate_soc == 'a5':
        return []
    return [bench_type for bench_type in ALL_TYPES if hccs_combo_allowed(transport, bench_type, gate_soc)]


def is_all_directions_mode(args) -> bool:
    return args.type in (None, 'all')


def supported_transports_for_soc(gate_soc: str | None, dual: bool = False) -> list[str]:
    if gate_soc == 'a3':
        return list(TRANSPORTS_A3)
    if gate_soc == 'a5':
        return list(TRANSPORTS_A5)
    if dual:
        return ['rdma']
    return list(TRANSPORTS_A2)


def is_all_transports_mode(args) -> bool:
    return args.transport == 'all'


def resolve_bench_types(args, gate_soc: str | None, transport: str | None = None) -> list[str]:
    """Resolve --type into an ordered list of direction names to run."""
    active_transport = transport if transport is not None else args.transport
    if is_all_directions_mode(args):
        return supported_types_for_transport(active_transport, gate_soc)
    return [args.type]


def build_dual_run_plan(args, gate_soc: str | None) -> list[tuple[str, list[str]]]:
    """Return [(transport, directions), ...] in execution order."""
    transports = supported_transports_for_soc(gate_soc, dual=True) if is_all_transports_mode(args) else [args.transport]
    plan: list[tuple[str, list[str]]] = []
    for transport in transports:
        bench_types = resolve_bench_types(args, gate_soc, transport)
        if bench_types:
            plan.append((transport, bench_types))
    return plan


@dataclass
class DualTopology:
    pattern: str
    target_device_ids: list[int]
    initiator_device_ids: list[int]

    @property
    def target_count(self) -> int:
        return len(self.target_device_ids)

    @property
    def initiator_count(self) -> int:
        return len(self.initiator_device_ids)


@dataclass
class PeerLauncherSpec:
    args: argparse.Namespace
    local_ip: str
    base_hixl_port: int
    base_tcp_port: int
    device_ids: str
    pattern: str
    transport_flag: str | None
    num_targets: int | None = None
    num_initiators: int | None = None


@dataclass
class TargetCommandSpec:
    bench_bin: str
    args: argparse.Namespace
    bench_type: str
    lane: dict
    tcp_accept_wait_s: int


@dataclass
class InitiatorCommandSpec:
    bench_bin: str
    args: argparse.Namespace
    bench_type: str
    lane: dict
    target_host: str
    base_hixl_port: int


@dataclass
class DualTargetContext:
    args: argparse.Namespace
    bench_bin: str
    topology: DualTopology
    local_ip: str
    base_hixl_port: int
    base_tcp_port: int
    tcp_accept_wait_s: int


@dataclass
class DualInitiatorContext:
    args: argparse.Namespace
    bench_bin: str
    topology: DualTopology
    base_hixl_port: int
    base_tcp_port: int


@dataclass
class SingleTargetCommandSpec:
    args: argparse.Namespace
    bench_bin: str
    bench_type: str
    device_id: int
    target: str
    tcp_port: int


@dataclass
class SingleInitiatorCommandSpec:
    args: argparse.Namespace
    bench_bin: str
    bench_type: str
    device_id: int
    local_port: int
    remotes: str
    ports: str


@dataclass
class LaunchDispatchSpec:
    args: argparse.Namespace
    bench_bin: str
    devices: list[int]
    run_plan: list[tuple[str, list[str]]]
    single_bench_types: list[str]
    gate_soc: str | None


@dataclass
class DualDirectionsRunSpec:
    transport: str
    bench_types: list[str]
    run_counter: list[int]
    step_fn: object
    role_label: str


def parse_device_ids(value: str) -> list[int]:
    return [int(x.strip()) for x in value.split(',') if x.strip()]


def format_device_ids(devices: list[int]) -> str:
    return ','.join((str(d) for d in devices))


def resolve_default_device_ids(role: str | None) -> str:
    if role in ('target', 'initiator'):
        return DEFAULT_DUAL_DEVICE_IDS
    return DEFAULT_SINGLE_DEVICE_IDS


def _resolve_one_to_many_target(args, devices: list[int]) -> DualTopology | None:
    if not devices:
        log.error('[ERROR] one_to_many target requires at least one --device_id')
        return None
    target_ids = devices
    if args.num_targets is not None:
        if args.num_targets <= 0 or args.num_targets > len(devices):
            log.info(f'[ERROR] --num_targets must be in 1..{len(devices)} for one_to_many target')
            return None
        target_ids = devices[:args.num_targets]
    return DualTopology(args.pattern, target_ids, [0])


def _resolve_one_to_many_initiator(args, devices: list[int]) -> DualTopology | None:
    if not devices:
        log.error('[ERROR] one_to_many initiator requires at least one --device_id')
        return None
    if len(devices) != 1:
        log.warning('[WARN] one_to_many initiator uses the first --device_ids entry only')
    target_count = args.num_targets if args.num_targets is not None else len(devices)
    if target_count <= 1 and args.num_targets is None:
        log.error('[ERROR] one_to_many initiator requires --num_targets or multiple --device_ids')
        return None
    if target_count <= 0:
        log.error('[ERROR] one_to_many initiator --num_targets must be positive')
        return None
    return DualTopology(args.pattern, list(range(target_count)), [devices[0]])


def _resolve_many_to_one_target(args, devices: list[int]) -> DualTopology | None:
    if not devices:
        log.error('[ERROR] many_to_one target requires one --device_id')
        return None
    if len(devices) != 1:
        log.warning('[WARN] many_to_one target uses the first --device_id only')
    initiator_count = args.num_initiators if args.num_initiators is not None else 1
    if initiator_count <= 0:
        log.error('[ERROR] many_to_one target --num_initiators must be positive')
        return None
    return DualTopology(args.pattern, [devices[0]], list(range(initiator_count)))


def _many_to_one_initiator_ids(args, devices: list[int]) -> list[int] | None:
    if args.num_initiators is None:
        return devices
    if args.num_initiators <= 0:
        log.error('[ERROR] many_to_one initiator --num_initiators must be positive')
        return None
    if len(devices) == 1 and args.num_initiators > 1:
        return list(range(args.num_initiators))
    if args.num_initiators <= len(devices):
        return devices[:args.num_initiators]
    log.info(f'[ERROR] --num_initiators={args.num_initiators} exceeds --device_ids count ({len(devices)})')
    return None


def _resolve_many_to_one_initiator(args, devices: list[int]) -> DualTopology | None:
    if not devices:
        log.error('[ERROR] many_to_one initiator requires at least one --device_id')
        return None
    initiator_ids = _many_to_one_initiator_ids(args, devices)
    if initiator_ids is None:
        return None
    return DualTopology(args.pattern, [0], initiator_ids)


def _resolve_pairwise_topology(devices: list[int]) -> DualTopology | None:
    if not devices:
        log.error('[ERROR] dual-machine mode requires at least one --device_id')
        return None
    return DualTopology('pairwise', [devices[0]], [devices[0]])


def resolve_dual_topology(args, devices: list[int], role: str) -> DualTopology | None:
    if args.pattern == 'one_to_many':
        resolver = _resolve_one_to_many_target if role == 'target' else _resolve_one_to_many_initiator
        return resolver(args, devices)
    if args.pattern == 'many_to_one':
        resolver = _resolve_many_to_one_target if role == 'target' else _resolve_many_to_one_initiator
        return resolver(args, devices)
    return _resolve_pairwise_topology(devices)


def get_local_ip() -> str:
    """Auto-detect local non-loopback IP by connecting a UDP socket."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.settimeout(2)
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        try:
            return socket.gethostbyname(socket.gethostname())
        except Exception:
            return '127.0.0.1'


def find_bench_bin() -> str:
    """Auto-detect hixl_comm_bench binary from build directory."""
    candidates = [
        Path(__file__).resolve().parents[3] / 'build' / 'benchmarks' / 'comm_benchmark' / 'hixl_comm_bench',
        Path('build/benchmarks/comm_benchmark/hixl_comm_bench'),
        Path('./hixl_comm_bench'),
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    return './hixl_comm_bench'


def _launcher_script_for_peer_hint() -> str:
    """Short path for dual-machine copy-paste (avoid absolute __file__ in printed commands)."""
    resolved = Path(__file__).resolve()
    cwd = Path.cwd().resolve()
    try:
        return str(resolved.relative_to(cwd))
    except ValueError:
        pass
    repo_root = resolved.parents[3]
    try:
        return str(resolved.relative_to(repo_root))
    except ValueError:
        return resolved.name


def endpoint(host, port):
    return f'{host}:{port}'


def _add_topology_args(parser) -> None:
    parser.add_argument('--pattern', choices=['pairwise', 'one_to_many', 'many_to_one'], default='pairwise')
    parser.add_argument(
        '--num_targets',
        type=int,
        default=None,
        help='Target lane count. Dual one_to_many: required on initiator; optional cap on target.',
    )
    parser.add_argument(
        '--num_initiators',
        type=int,
        default=None,
        help='Initiator lane count for dual many_to_one target. Single-machine: topology override.',
    )
    parser.add_argument('--bench_bin', default=None, help='Path to hixl_comm_bench (auto-detected)')
    parser.add_argument(
        '--host',
        default='127.0.0.1',
        help='Local host IP. In dual-machine target mode, used as the advertised IP.',
    )
    parser.add_argument(
        '--role',
        choices=['target', 'initiator'],
        default=None,
        help='Dual-machine mode: only run this role locally. Omit for single-machine mode.',
    )
    parser.add_argument(
        '--target-host',
        default=None,
        help='Target host IP. Required when --role=initiator in dual-machine mode.',
    )
    parser.add_argument('--base_hixl_port', type=int, default=16000)
    parser.add_argument('--base_tcp_port', type=int, default=20000)
    parser.add_argument(
        '--device_ids',
        default=None,
        help='Device IDs. Dual-machine default: 0. Single-machine default: 0,1.',
    )


def _add_transfer_args(parser) -> None:
    parser.add_argument(
        '--type',
        choices=[*ALL_TYPES, 'all'],
        default=None,
        help='Transfer direction. Dual-machine default: all supported directions for --transport.',
    )
    parser.add_argument(
        '--transport',
        choices=['hccs', 'rdma', 'fabric_mem', 'uboe', 'all'],
        default=None,
        help='Transport path. Dual-machine default: all platform-supported transports.',
    )
    parser.add_argument(
        '--soc_variant',
        choices=['auto', 'a2', 'a3', 'a5'],
        default=None,
        help='SOC class forwarded to hixl_comm_bench. Default: npu-smi hint or ACL probe.',
    )


def _add_size_args(parser) -> None:
    parser.add_argument(
        '--start_block_size',
        type=int,
        default=DEFAULT_START_BLOCK,
        help=f'First block size in bytes (default {DEFAULT_START_BLOCK})',
    )
    parser.add_argument(
        '--max_block_size',
        type=int,
        default=DEFAULT_MAX_BLOCK,
        help=f'Max block size in bytes, doubles each step (default {DEFAULT_MAX_BLOCK})',
    )
    parser.add_argument(
        '--total_size',
        type=int,
        default=None,
        help='Bytes transferred per block-size step (default from hixl_comm_bench)',
    )
    parser.add_argument(
        '--buffer_size',
        type=int,
        default=None,
        help='Allocation/register size in bytes (default from hixl_comm_bench)',
    )
    parser.add_argument('--batch_size', type=int, default=1)
    parser.add_argument('--threads', type=int, default=1)
    parser.add_argument(
        '--loops',
        type=int,
        default=5,
        help='Repeat full block-step ladder (hixl_comm_bench --loops/-n).',
    )
    parser.add_argument('--output_dir', default='comm_benchmark/output')


def _add_report_args(parser) -> None:
    parser.add_argument(
        '--plot',
        dest='plot',
        action='store_true',
        default=True,
        help='Generate PNG plots for CSVs produced by this run.',
    )
    parser.add_argument(
        '--skip_plot',
        '--skip-plot',
        dest='plot',
        action='store_false',
        help='Skip PNG plot generation',
    )
    parser.add_argument(
        '--report',
        dest='report',
        action='store_true',
        default=True,
        help='Generate perf.md report on dual-machine initiator.',
    )
    parser.add_argument('--skip_report', dest='report', action='store_false', help='Skip perf.md report generation')
    parser.add_argument('--report_path', default=None, help='perf.md output path (default: {output_dir}/perf.md)')


def _add_runtime_args(parser) -> None:
    parser.add_argument(
        '-H',
        '--hixl_option',
        action='append',
        default=[],
        metavar='KEY=VALUE',
        help='Forwarded to hixl_comm_bench as -H=KEY=VALUE (repeatable).',
    )
    parser.add_argument(
        '--tcp_accept_wait_s',
        type=int,
        default=None,
        help='Target TCP accept timeout per direction.',
    )
    parser.add_argument(
        '--inter_run_delay_s',
        type=int,
        default=DEFAULT_INTER_RUN_DELAY_SEC,
        help=f'Seconds to wait between dual-machine runs (default {DEFAULT_INTER_RUN_DELAY_SEC}).',
    )
    parser.add_argument(
        '--connect_timeout',
        type=int,
        default=DEFAULT_CONNECT_TIMEOUT_MS,
        help='Initiator TCP/HIXL connect timeout in ms.',
    )


def parse_args():
    parser = argparse.ArgumentParser(description='Run HIXL communication benchmark')
    _add_topology_args(parser)
    _add_transfer_args(parser)
    _add_size_args(parser)
    _add_report_args(parser)
    _add_runtime_args(parser)
    return parser.parse_args()


def hixl_init_extra_args(args) -> list[str]:
    """hixl_comm_bench expects -H=KEY=VALUE (one argv per option)."""
    if not args.hixl_option:
        return []
    return [f'-H={payload}' for payload in args.hixl_option]


def peer_launcher_hixl_flags(args) -> list[str]:
    """Same options as CLI, for the printed dual-machine initiator command."""
    if not args.hixl_option:
        return []
    return [f'--hixl_option={payload}' for payload in args.hixl_option]


def benchmark_group_for_run(args) -> str:
    if args.role in ('target', 'initiator'):
        return 'dual'
    return 'single'


def base_options(args, bench_type: str):
    (im, tm, op) = TYPE_MAP[bench_type]
    options = [
        f'--benchmark_group={benchmark_group_for_run(args)}',
        f'--output_dir={args.output_dir}',
        f'--soc_variant={bench_soc_variant_for_bin(args)}',
        f'--initiator_memory={im}',
        f'--target_memory={tm}',
        f'--op_type={op}',
        f'--start_block_size={args.start_block_size}',
        f'--max_block_size={args.max_block_size}',
        f'--start_batch_size={args.batch_size}',
        f'--max_batch_size={args.batch_size}',
        f'--start_threads={args.threads}',
        f'--max_threads={args.threads}',
        f'--loops={args.loops}',
        f'--transport={args.transport}',
        f'--connect_timeout={args.connect_timeout}',
    ]
    if args.total_size is not None:
        options.append(f'--total_size={args.total_size}')
    if args.buffer_size is not None:
        options.append(f'--buffer_size={args.buffer_size}')
    return options


def tcp_accept_wait_for_run(args, run_count: int) -> int:
    if args.tcp_accept_wait_s is not None:
        return args.tcp_accept_wait_s
    if run_count > 1:
        return DEFAULT_TCP_ACCEPT_WAIT_ALL
    return DEFAULT_TCP_ACCEPT_WAIT_SINGLE


def build_target_cmd(spec: TargetCommandSpec) -> list[str]:
    lane = spec.lane
    args = spec.args
    return [
        spec.bench_bin,
        '--role=target',
        f"--device_id={lane['device_id']}",
        f"--local_engine={endpoint(lane['local_ip'], lane['hixl_port'])}",
        f"--tcp_port={lane['tcp_port']}",
        f"--tcp_client_count={lane['tcp_client_count']}",
        f'--tcp_accept_wait_s={spec.tcp_accept_wait_s}',
        *base_options(args, spec.bench_type),
        *hixl_init_extra_args(args),
    ]


def build_initiator_cmd(spec: InitiatorCommandSpec) -> list[str]:
    lane = spec.lane
    args = spec.args
    remote = lane['remote_engines']
    local_port = lane['local_hixl_port']
    return [
        spec.bench_bin,
        '--role=initiator',
        f"--device_id={lane['device_id']}",
        f"--local_engine={endpoint('127.0.0.1', local_port)}",
        f'--remote_engine={remote}',
        f"--tcp_port={lane['tcp_port']}",
        *base_options(args, spec.bench_type),
        *hixl_init_extra_args(args),
    ]


def _peer_launcher_common_args(spec: PeerLauncherSpec) -> list[str]:
    args = spec.args
    initiator_script = _launcher_script_for_peer_hint()
    type_flag = 'all' if is_all_directions_mode(args) else args.type
    transport = spec.transport_flag if spec.transport_flag is not None else args.transport
    cmd = [
        'python3',
        initiator_script,
        f'--type={type_flag}',
        f'--transport={transport}',
        f'--pattern={spec.pattern}',
        '--role=initiator',
        f'--target-host={spec.local_ip}',
        f'--base_hixl_port={spec.base_hixl_port}',
        f'--base_tcp_port={spec.base_tcp_port}',
        f'--device_ids={spec.device_ids}',
        f'--start_block_size={args.start_block_size}',
        f'--max_block_size={args.max_block_size}',
        f'--batch_size={args.batch_size}',
        f'--threads={args.threads}',
        f'--loops={args.loops}',
        f'--output_dir={args.output_dir}',
        f'--soc_variant={bench_soc_variant_for_bin(args)}',
    ]
    if spec.num_targets is not None:
        cmd.append(f'--num_targets={spec.num_targets}')
    if spec.num_initiators is not None:
        cmd.append(f'--num_initiators={spec.num_initiators}')
    if args.total_size is not None:
        cmd.append(f'--total_size={args.total_size}')
    if args.buffer_size is not None:
        cmd.append(f'--buffer_size={args.buffer_size}')
    if args.tcp_accept_wait_s is not None:
        cmd.append(f'--tcp_accept_wait_s={args.tcp_accept_wait_s}')
    if args.connect_timeout != DEFAULT_CONNECT_TIMEOUT_MS:
        cmd.append(f'--connect_timeout={args.connect_timeout}')
    if args.inter_run_delay_s != DEFAULT_INTER_RUN_DELAY_SEC:
        cmd.append(f'--inter_run_delay_s={args.inter_run_delay_s}')
    if not args.plot:
        cmd.append('--skip_plot')
    if not args.report:
        cmd.append('--skip_report')
    if args.report_path is not None:
        cmd.append(f'--report_path={args.report_path}')
    cmd.extend(peer_launcher_hixl_flags(args))
    return cmd


def build_peer_initiator_launcher_cmd(
    args,
    local_ip: str,
    base_hixl_port: int,
    base_tcp_port: int,
    transport_flag: str | None = None,
) -> list[str]:
    """Printed command for dual-machine initiator (mirror target-side args)."""
    device_ids = args.device_ids if args.device_ids is not None else resolve_default_device_ids('initiator')
    return _peer_launcher_common_args(
        PeerLauncherSpec(
            args=args,
            local_ip=local_ip,
            base_hixl_port=base_hixl_port,
            base_tcp_port=base_tcp_port,
            device_ids=device_ids,
            pattern=args.pattern,
            transport_flag=transport_flag,
            num_targets=args.num_targets,
            num_initiators=args.num_initiators,
        )
    )


def start_process(cmd):
    log.info('[INFO] %s', ' '.join(cmd))
    return subprocess.Popen(cmd)


def list_result_csvs(output_dir):
    return set(Path(output_dir).glob('comm_result_*.csv'))


def snapshot_result_csvs(output_dir):
    snapshot = {}
    for csv_path in list_result_csvs(output_dir):
        try:
            stat = csv_path.stat()
        except OSError:
            continue
        snapshot[csv_path] = (stat.st_mtime_ns, stat.st_size)
    return snapshot


def changed_result_csvs(output_dir, before_snapshot):
    changed = set()
    after_snapshot = snapshot_result_csvs(output_dir)
    for (csv_path, stat_info) in after_snapshot.items():
        if before_snapshot.get(csv_path) != stat_info:
            changed.add(csv_path)
    return changed


def block_label_from_bytes(block_size: int) -> str:
    if block_size % (1024 * 1024) == 0:
        return f'{block_size // (1024 * 1024)}M'
    if block_size % 1024 == 0:
        return f'{block_size // 1024}K'
    return str(block_size)


def block_sort_key(block_label: str) -> int:
    try:
        return BLOCK_SORT_ORDER.index(block_label)
    except ValueError:
        return len(BLOCK_SORT_ORDER)


def _append_csv_summary_groups(csv_path: Path, groups) -> None:
    with csv_path.open(newline='', encoding='utf-8') as csv_file:
        for row in csv.DictReader(csv_file):
            try:
                direction = row['direction'].strip()
                transport = row['transport'].strip()
                block_label = block_label_from_bytes(int(row['block_size']))
                bandwidth = float(row['bandwidth_gbps'])
            except (KeyError, ValueError):
                continue
            if direction and transport:
                groups[direction, transport, block_label].append(bandwidth)


def _load_average_summary_groups(csvs):
    groups = defaultdict(list)
    for csv_path in sorted(csvs):
        try:
            _append_csv_summary_groups(csv_path, groups)
        except OSError as exc:
            log.warning(f'[WARN] failed to read {csv_path}: {exc}')
    return groups


def print_average_summary(csvs):
    if not csvs:
        return
    groups = _load_average_summary_groups(csvs)
    if not groups:
        return
    log.info('\n[AVG] Bandwidth summary (same mean as perf.md)')

    def sort_key(item):
        return (item[0][0], item[0][1], block_sort_key(item[0][2]))

    for (direction, transport, block_label) in sorted(groups, key=sort_key):
        values = groups[direction, transport, block_label]
        avg_bw = statistics.mean(values)
        log.info(
            f'[AVG] direction={direction} transport={transport} block={block_label} '
            f'samples={len(values)} bandwidth={avg_bw:.3f} GB/s'
        )


def generate_plots(args, csvs):
    if not args.plot:
        return
    if not csvs:
        log.warning(f'[WARN] no comm CSV result found in {args.output_dir}')
        return
    plot_script = Path(__file__).resolve().parent / 'plot_comm_benchmark.py'
    for csv_path in sorted(csvs):
        out_dir = Path(args.output_dir)
        if len(csvs) > 1:
            out_dir = out_dir / csv_path.stem
        cmd = [sys.executable, str(plot_script), '--csv', str(csv_path), f'--output_dir={out_dir}']
        log.info('[INFO] %s', ' '.join(map(str, cmd)))
        subprocess.run(cmd, check=False)


def generate_perf_report(args, gate_soc: str | None) -> None:
    if not args.report:
        return
    csv_dir = Path(args.output_dir)
    if not list(csv_dir.glob('comm_result_*.csv')):
        log.warning(f'[WARN] no comm CSV result found in {csv_dir}, skip perf.md')
        return
    if not RENDER_PERF_SCRIPT.exists():
        log.warning(f'[WARN] render script not found: {RENDER_PERF_SCRIPT}')
        return
    report_path = Path(args.report_path) if args.report_path else csv_dir / 'perf.md'
    platform = platform_for_report(args, gate_soc, csv_dir)
    cmd = [
        sys.executable,
        str(RENDER_PERF_SCRIPT),
        f'--csv-dir={csv_dir}',
        f'--platform={platform}',
        f'--output={report_path}',
    ]
    if not args.plot:
        cmd.append('--no-plots')
    log.info('[INFO] %s', ' '.join(map(str, cmd)))
    ret = subprocess.run(cmd, check=False)
    if ret.returncode != 0:
        log.warning(f'[WARN] perf.md generation failed with code {ret.returncode}')
    else:
        log.info(f'[INFO] perf.md written to {report_path}')


def _format_run_plan_summary(run_plan: list[tuple[str, list[str]]]) -> str:
    parts = []
    for (transport, bench_types) in run_plan:
        if len(bench_types) == 1:
            parts.append(f'{transport}/{bench_types[0]}')
        else:
            parts.append(f'{transport}({len(bench_types)} dirs)')
    return ', '.join(parts)


def _dual_target_peer_count(topology: DualTopology) -> int:
    return topology.initiator_count if topology.pattern == 'many_to_one' else 1


def _dual_target_lanes(topology: DualTopology, local_ip: str, base_hixl_port: int, base_tcp_port: int):
    if topology.pattern == 'one_to_many':
        lanes = []
        for (idx, device_id) in enumerate(topology.target_device_ids):
            lanes.append(
                {
                    'device_id': device_id,
                    'local_ip': local_ip,
                    'hixl_port': base_hixl_port + idx,
                    'tcp_port': base_tcp_port + idx,
                    'tcp_client_count': 1,
                }
            )
        return lanes
    return [
        {
            'device_id': topology.target_device_ids[0],
            'local_ip': local_ip,
            'hixl_port': base_hixl_port,
            'tcp_port': base_tcp_port,
            'tcp_client_count': _dual_target_peer_count(topology),
        }
    ]


def _dual_initiator_lanes(topology: DualTopology, target_host: str, base_hixl_port: int, base_tcp_port: int):
    if topology.pattern == 'one_to_many':
        remotes = ','.join((endpoint(target_host, base_hixl_port + i) for i in range(topology.target_count)))
        ports = ','.join((str(base_tcp_port + i) for i in range(topology.target_count)))
        return [
            {
                'device_id': topology.initiator_device_ids[0],
                'remote_engines': remotes,
                'tcp_port': ports,
                'local_hixl_port': base_hixl_port + topology.target_count,
            }
        ]
    if topology.pattern == 'many_to_one':
        remote = endpoint(target_host, base_hixl_port)
        lanes = []
        for (idx, device_id) in enumerate(topology.initiator_device_ids):
            lanes.append(
                {
                    'device_id': device_id,
                    'remote_engines': remote,
                    'tcp_port': str(base_tcp_port),
                    'local_hixl_port': base_hixl_port + 1 + idx,
                }
            )
        return lanes
    return [
        {
            'device_id': topology.initiator_device_ids[0],
            'remote_engines': endpoint(target_host, base_hixl_port),
            'tcp_port': str(base_tcp_port),
            'local_hixl_port': base_hixl_port + 1,
        }
    ]


def _print_peer_initiator_hint(ctx: DualTargetContext, run_plan: list[tuple[str, list[str]]]):
    args = ctx.args
    topology = ctx.topology
    transport_flag = 'all' if is_all_transports_mode(args) else args.transport
    initiator_cmd = build_peer_initiator_launcher_cmd(
        args,
        ctx.local_ip,
        ctx.base_hixl_port,
        ctx.base_tcp_port,
        transport_flag=transport_flag,
    )
    total_runs = sum((len(bench_types) for (_, bench_types) in run_plan))
    sep = '=' * 60
    log.info(f'\n{sep}')
    log.info(f'  Target ready.  Local IP: {ctx.local_ip}')
    log.info(f'  Pattern: {topology.pattern}')
    log.info(f'  Target lanes: {topology.target_count} (devices={format_device_ids(topology.target_device_ids)})')
    log.info(
        f'  Initiator lanes: {topology.initiator_count} '
        f'(devices={format_device_ids(topology.initiator_device_ids)})'
    )
    log.info(f'  HIXL base port: {ctx.base_hixl_port},  TCP base port: {ctx.base_tcp_port}')
    if is_all_transports_mode(args):
        log.info(f"  Transports: {', '.join((t for (t, _) in run_plan))}")
    else:
        log.info(f'  Transport: {args.transport}')
    if is_all_directions_mode(args):
        log.info(f'  Total runs: {total_runs} ({_format_run_plan_summary(run_plan)})')
    elif total_runs == 1:
        log.info(f'  Direction: {run_plan[0][1][0]}')
    log.info('  Run this on the INITIATOR machine:')
    log.info(sep)
    log.info(f"  {' '.join(initiator_cmd)}")
    log.info(f'{sep}\n')


def _dual_inter_run_delay(args, run_index: int) -> None:
    """Pause between sequential dual-machine runs so target can finish cleanup and re-listen."""
    if run_index <= 1 or args.inter_run_delay_s <= 0:
        return
    log.info(f'[DUAL] Waiting {args.inter_run_delay_s}s before next run ...')
    time.sleep(args.inter_run_delay_s)


def _run_dual_target_step(ctx: DualTargetContext, transport: str, bench_type: str) -> int:
    args = ctx.args
    saved_transport = args.transport
    args.transport = transport
    procs = []
    try:
        for lane in _dual_target_lanes(ctx.topology, ctx.local_ip, ctx.base_hixl_port, ctx.base_tcp_port):
            target_cmd = build_target_cmd(
                TargetCommandSpec(
                    bench_bin=ctx.bench_bin,
                    args=args,
                    bench_type=bench_type,
                    lane=lane,
                    tcp_accept_wait_s=ctx.tcp_accept_wait_s,
                )
            )
            procs.append(start_process(target_cmd))
        time.sleep(2)
        for proc in procs:
            if proc.poll() is not None:
                log.error(f'[ERROR] Target exited early with code {proc.returncode}')
                return proc.returncode
        log.info('[DUAL] Waiting for remote initiator to connect and finish...')
        return max((proc.wait() for proc in procs))
    finally:
        args.transport = saved_transport


def _run_dual_directions(ctx, run_spec: DualDirectionsRunSpec) -> int:
    args = ctx.args
    last_ret = 0
    for (idx, bench_type) in enumerate(run_spec.bench_types, start=1):
        run_spec.run_counter[0] += 1
        _dual_inter_run_delay(args, run_spec.run_counter[0])
        if len(run_spec.bench_types) > 1:
            log.info(
                f'[DUAL] transport={run_spec.transport} [{idx}/{len(run_spec.bench_types)}] '
                f'Direction {bench_type} — starting {run_spec.role_label} ...'
            )
        ret = run_spec.step_fn(ctx, run_spec.transport, bench_type)
        if ret != 0:
            log.info(
                f'[ERROR] {run_spec.role_label.capitalize()} failed for '
                f'transport={run_spec.transport} direction={bench_type} with code {ret}'
            )
            return ret
        if len(run_spec.bench_types) > 1:
            log.info(
                f'[DUAL] transport={run_spec.transport} [{idx}/{len(run_spec.bench_types)}] '
                f'Direction {bench_type} — done'
            )
        last_ret = ret
    return last_ret


def _run_target_directions(
    ctx: DualTargetContext,
    transport: str,
    bench_types: list[str],
    run_counter: list[int],
) -> int:
    run_spec = DualDirectionsRunSpec(transport, bench_types, run_counter, _run_dual_target_step, 'target')
    return _run_dual_directions(ctx, run_spec)


def _launch_target(args, bench_bin: str, topology: DualTopology, run_plan: list[tuple[str, list[str]]]):
    """Start target locally for each transport/direction; print initiator command(s)."""
    if not run_plan:
        log.error('[ERROR] No transport/direction combinations to run on target')
        return -1
    local_ip = get_local_ip()
    if args.host != '127.0.0.1':
        local_ip = args.host
    base_hixl_port = args.base_hixl_port
    base_tcp_port = args.base_tcp_port
    total_runs = sum((len(bench_types) for (_, bench_types) in run_plan))
    tcp_accept_wait_s = tcp_accept_wait_for_run(args, total_runs)
    ctx = DualTargetContext(args, bench_bin, topology, local_ip, base_hixl_port, base_tcp_port, tcp_accept_wait_s)
    log.info(f'\n[DUAL] Starting target on {local_ip}, pattern={topology.pattern}, lanes={topology.target_count}')
    if total_runs > 1:
        log.info(f'[DUAL] Plan: {total_runs} run(s) — {_format_run_plan_summary(run_plan)}')
    _print_peer_initiator_hint(ctx, run_plan)
    last_ret = 0
    run_counter = [0]
    for (t_idx, (transport, bench_types)) in enumerate(run_plan, start=1):
        if len(run_plan) > 1:
            log.info(f'\n[DUAL] Transport [{t_idx}/{len(run_plan)}] {transport} — {len(bench_types)} direction(s)')
        ret = _run_target_directions(ctx, transport, bench_types, run_counter)
        if ret != 0:
            return ret
        last_ret = ret
    if total_runs > 1:
        log.info(f'[DUAL] All {total_runs} run(s) completed on target')
    return last_ret


def _run_dual_initiator_step(ctx: DualInitiatorContext, transport: str, bench_type: str) -> int:
    args = ctx.args
    saved_transport = args.transport
    args.transport = transport
    procs = []
    try:
        for lane in _dual_initiator_lanes(ctx.topology, args.target_host, ctx.base_hixl_port, ctx.base_tcp_port):
            initiator_cmd = build_initiator_cmd(
                InitiatorCommandSpec(
                    bench_bin=ctx.bench_bin,
                    args=args,
                    bench_type=bench_type,
                    lane=lane,
                    target_host=args.target_host,
                    base_hixl_port=ctx.base_hixl_port,
                )
            )
            procs.append(start_process(initiator_cmd))
        return max((proc.wait() for proc in procs))
    finally:
        args.transport = saved_transport


def _run_initiator_directions(
    ctx: DualInitiatorContext,
    transport: str,
    bench_types: list[str],
    run_counter: list[int],
) -> int:
    run_spec = DualDirectionsRunSpec(transport, bench_types, run_counter, _run_dual_initiator_step, 'initiator')
    return _run_dual_directions(ctx, run_spec)


def _launch_initiator(args, bench_bin: str, topology: DualTopology, run_plan: list[tuple[str, list[str]]], gate_soc):
    """Start initiator locally for each transport/direction, then generate report."""
    if not args.target_host:
        log.error('[ERROR] --target-host is required when --role=initiator')
        return -1
    if not run_plan:
        log.error('[ERROR] No transport/direction combinations to run on initiator')
        return -1
    base_hixl_port = args.base_hixl_port
    base_tcp_port = args.base_tcp_port
    ctx = DualInitiatorContext(args, bench_bin, topology, base_hixl_port, base_tcp_port)
    total_runs = sum((len(bench_types) for (_, bench_types) in run_plan))
    if total_runs > 1:
        log.info(
            f'\n[DUAL] Connecting to target at {args.target_host}, pattern={topology.pattern}, '
            f'initiator lanes={topology.initiator_count}; {total_runs} run(s) — '
            f'{_format_run_plan_summary(run_plan)}'
        )
    else:
        (transport, bench_types) = run_plan[0]
        log.info(
            f'\n[DUAL] Connecting to target at {args.target_host}  '
            f'(pattern={topology.pattern}, transport={transport}, direction={bench_types[0]}, '
            f'initiator lanes={topology.initiator_count})'
        )
    before_csvs = snapshot_result_csvs(args.output_dir)
    last_ret = 0
    run_counter = [0]
    for (t_idx, (transport, bench_types)) in enumerate(run_plan, start=1):
        if len(run_plan) > 1:
            log.info(f'\n[DUAL] Transport [{t_idx}/{len(run_plan)}] {transport} — {len(bench_types)} direction(s)')
        ret = _run_initiator_directions(ctx, transport, bench_types, run_counter)
        if ret != 0:
            return ret
        last_ret = ret
    new_csvs = changed_result_csvs(args.output_dir, before_csvs)
    print_average_summary(new_csvs)
    generate_plots(args, new_csvs)
    generate_perf_report(args, gate_soc)
    if total_runs > 1:
        log.info(f'[DUAL] All {total_runs} run(s) completed on initiator')
    return last_ret


def infer_single_mode_counts(pattern: str, devices: list[int], num_targets, num_initiators):
    if len(devices) < 2:
        log.error('[ERROR] single-machine mode requires at least two device IDs')
        return None
    if num_targets is not None and num_targets <= 0:
        log.error('[ERROR] --num_targets must be positive')
        return None
    if num_initiators is not None and num_initiators <= 0:
        log.error('[ERROR] --num_initiators must be positive')
        return None
    if pattern == 'one_to_many':
        if num_initiators not in (None, 1):
            log.error('[ERROR] one_to_many supports exactly one initiator')
            return None
        return validate_single_mode_counts(num_targets or len(devices) - 1, 1, len(devices))
    if pattern == 'many_to_one':
        if num_targets not in (None, 1):
            log.error('[ERROR] many_to_one supports exactly one target')
            return None
        return validate_single_mode_counts(1, num_initiators or len(devices) - 1, len(devices))
    if num_targets is not None and num_initiators is not None and (num_targets != num_initiators):
        log.error('[ERROR] pairwise requires --num_targets == --num_initiators')
        return None
    pair_count = num_targets or num_initiators
    if pair_count is None:
        if len(devices) % 2 != 0:
            log.error('[ERROR] pairwise mode requires an even number of device IDs')
            return None
        pair_count = len(devices) // 2
    return validate_single_mode_counts(pair_count, pair_count, len(devices))


def validate_single_mode_counts(target_count: int, initiator_count: int, device_count: int):
    if target_count + initiator_count > device_count:
        log.info(
            f'[ERROR] device_ids has {device_count} device(s), '
            f'but topology needs {target_count + initiator_count}'
        )
        return None
    return (target_count, initiator_count)


def target_peer_count(pattern: str, initiator_count: int) -> int:
    return initiator_count if pattern == 'many_to_one' else 1


def initiator_peer_args(pattern: str, idx: int, target_endpoints: list[str], tcp_ports: list[int]):
    if pattern == 'one_to_many':
        return (','.join(target_endpoints), ','.join((str(p) for p in tcp_ports)))
    if pattern == 'many_to_one':
        return (target_endpoints[0], str(tcp_ports[0]))
    return (target_endpoints[idx], str(tcp_ports[idx]))


def _single_target_cmd(spec: SingleTargetCommandSpec) -> list[str]:
    args = spec.args
    return [
        spec.bench_bin,
        '--role=target',
        f'--device_id={spec.device_id}',
        f'--local_engine={spec.target}',
        f'--tcp_port={spec.tcp_port}',
        f'--tcp_client_count={target_peer_count(args.pattern, args.initiator_count)}',
        f'--tcp_accept_wait_s={args.tcp_accept_wait_s}',
        *base_options(args, spec.bench_type),
        *hixl_init_extra_args(args),
    ]


def _single_initiator_cmd(spec: SingleInitiatorCommandSpec) -> list[str]:
    args = spec.args
    return [
        spec.bench_bin,
        '--role=initiator',
        f'--device_id={spec.device_id}',
        f'--local_engine={endpoint(args.host, spec.local_port)}',
        f'--remote_engine={spec.remotes}',
        f'--tcp_port={spec.ports}',
        *base_options(args, spec.bench_type),
        *hixl_init_extra_args(args),
    ]


def _run_single_direction(args, bench_bin: str, devices: list[int], bench_type: str, tcp_accept_wait_s: int) -> int:
    counts = infer_single_mode_counts(args.pattern, devices, args.num_targets, args.num_initiators)
    if counts is None:
        return -1
    (target_count, initiator_count) = counts
    args.initiator_count = initiator_count
    args.tcp_accept_wait_s = tcp_accept_wait_s
    target_endpoints = [endpoint(args.host, args.base_hixl_port + i) for i in range(target_count)]
    tcp_ports = [args.base_tcp_port + i for i in range(target_count)]
    procs = []
    for (idx, target) in enumerate(target_endpoints):
        procs.append(
            start_process(
                _single_target_cmd(
                    SingleTargetCommandSpec(args, bench_bin, bench_type, devices[idx], target, tcp_ports[idx])
                )
            )
        )
    time.sleep(2)
    for idx in range(initiator_count):
        (remotes, ports) = initiator_peer_args(args.pattern, idx, target_endpoints, tcp_ports)
        local_port = args.base_hixl_port + target_count + idx
        procs.append(
            start_process(
                _single_initiator_cmd(
                    SingleInitiatorCommandSpec(
                        args,
                        bench_bin,
                        bench_type,
                        devices[target_count + idx],
                        local_port,
                        remotes,
                        ports,
                    )
                )
            )
        )
    return max((proc.wait() for proc in procs))


def _launch_single(args, bench_bin: str, devices: list[int], bench_types: list[str]):
    """Single-machine mode: start both target and initiator locally."""
    if not bench_types:
        log.error('[ERROR] No supported directions to run in single-machine mode')
        return -1
    before_csvs = snapshot_result_csvs(args.output_dir)
    tcp_accept_wait_s = tcp_accept_wait_for_run(args, len(bench_types))
    run_counter = [0]
    last_ret = 0
    if len(bench_types) > 1:
        log.info(
            f"\n[SINGLE] transport={args.transport}, "
            f"{len(bench_types)} direction(s): {', '.join(bench_types)}"
        )
    for (idx, bench_type) in enumerate(bench_types, start=1):
        run_counter[0] += 1
        _dual_inter_run_delay(args, run_counter[0])
        if len(bench_types) > 1:
            log.info(f'[SINGLE] [{idx}/{len(bench_types)}] direction={bench_type} — starting ...')
        last_ret = _run_single_direction(args, bench_bin, devices, bench_type, tcp_accept_wait_s)
        if last_ret != 0:
            log.info(f'[ERROR] Single-machine run failed for direction={bench_type} with code {last_ret}')
            return last_ret
        if len(bench_types) > 1:
            log.info(f'[SINGLE] [{idx}/{len(bench_types)}] direction={bench_type} — done')
    new_csvs = changed_result_csvs(args.output_dir, before_csvs)
    print_average_summary(new_csvs)
    generate_plots(args, new_csvs)
    return last_ret


def _normalize_launch_args(args) -> bool:
    transport_explicit = args.transport is not None
    if args.transport is None:
        args.transport = 'all' if args.role in ('target', 'initiator') else 'hccs'
    elif args.transport == 'all' and args.role not in ('target', 'initiator'):
        log.error('[ERROR] --transport=all is only supported in dual-machine mode (--role=target|initiator)')
        return False
    if args.role in ('target', 'initiator'):
        if args.type is None:
            args.type = 'all'
    elif args.type is None:
        args.type = 'all' if transport_explicit else 'D2rD'
    return True


def _validate_launch_plan(args, gate_soc, run_plan, single_bench_types) -> int | None:
    if args.role in ('target', 'initiator'):
        if not run_plan:
            log.info(
                f'[ERROR] No supported transport/direction combinations on this platform '
                f'(transport={args.transport}, type={args.type}).'
            )
            return -1
        return None
    if is_all_directions_mode(args):
        if not single_bench_types:
            log.info(f'[ERROR] No supported directions for transport={args.transport} on this platform.')
            return -1
        return None
    if not hccs_combo_allowed(args.transport, args.type, gate_soc):
        log.error(
            '[ERROR] transport=hccs is unsupported for this direction. '
            'A2 supports HCCS D2D only; A3 also supports H2rD|rD2H; A5 has no HCCS.'
        )
        return -1
    return None


def _launch_by_role(spec: LaunchDispatchSpec):
    args = spec.args
    if args.role == 'target':
        topology = resolve_dual_topology(args, spec.devices, 'target')
        if topology is None:
            return -1
        return _launch_target(args, spec.bench_bin, topology, spec.run_plan)
    if args.role == 'initiator':
        topology = resolve_dual_topology(args, spec.devices, 'initiator')
        if topology is None:
            return -1
        return _launch_initiator(args, spec.bench_bin, topology, spec.run_plan, spec.gate_soc)
    return _launch_single(args, spec.bench_bin, spec.devices, spec.single_bench_types)


def launch(args):
    gate_soc = effective_soc_for_hccs_gate(args)
    if not _normalize_launch_args(args):
        return -1
    run_plan = build_dual_run_plan(args, gate_soc) if args.role in ('target', 'initiator') else []
    single_bench_types = resolve_bench_types(args, gate_soc) if args.role not in ('target', 'initiator') else []
    plan_error = _validate_launch_plan(args, gate_soc, run_plan, single_bench_types)
    if plan_error is not None:
        return plan_error
    bench_bin = args.bench_bin or find_bench_bin()
    if not Path(bench_bin).exists():
        log.error(f'[ERROR] bench binary not found: {bench_bin}')
        log.error('[ERROR] Build with: bash build.sh --examples')
        return -1
    if args.device_ids is None:
        args.device_ids = resolve_default_device_ids(args.role)
    devices = parse_device_ids(args.device_ids)
    return _launch_by_role(LaunchDispatchSpec(args, bench_bin, devices, run_plan, single_bench_types, gate_soc))


if __name__ == '__main__':
    sys.exit(launch(parse_args()))
