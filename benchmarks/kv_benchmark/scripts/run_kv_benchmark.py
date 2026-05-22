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

"""Launch the HIXL KV benchmark processes."""
from __future__ import annotations

import argparse
import csv
import logging
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

_BENCHMARKS_DIR = Path(__file__).resolve().parents[2]
if str(_BENCHMARKS_DIR) not in sys.path:
    sys.path.insert(0, str(_BENCHMARKS_DIR))
from benchmark_log import configure_logging  # noqa: E402
from kv_defaults import KV_PROCESS_COUNT_BY_PLATFORM, default_kv_transport, resolve_kv_devices  # noqa: E402

configure_logging()
log = logging.getLogger(__name__)


@dataclass
class KvProcessSpec:
    args: argparse.Namespace
    bench_bin: str
    rank: int
    device: str
    model_config: str
    run_id: str
    key_counts: str


def find_bench_bin() -> str:
    """Auto-detect hixl_kv_bench binary from build directory."""
    candidates = [
        Path(__file__).resolve().parents[3] / 'build' / 'benchmarks' / 'kv_benchmark' / 'hixl_kv_bench',
        Path('build/benchmarks/kv_benchmark/hixl_kv_bench'),
        Path('./hixl_kv_bench'),
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    return './hixl_kv_bench'


def find_model_config() -> str:
    """Auto-detect model config JSON relative to repo root."""
    candidates = [
        Path(__file__).resolve().parents[1] / 'config' / 'models.json',
        Path('kv_benchmark/config/models.json'),
        Path('benchmarks/kv_benchmark/config/models.json'),
    ]
    for p in candidates:
        if p.exists():
            return str(p)
    return 'kv_benchmark/config/models.json'


def parse_args():
    parser = argparse.ArgumentParser(description='Run HIXL KV benchmark')
    parser.add_argument(
        '--platform',
        choices=['a2', 'a3', 'a5'],
        default=None,
        help='Ascend platform (default KV ranks: 8 on A2/A3/A5)',
    )
    parser.add_argument('--trace', action='store_true', help='Enable [TRACE] logs on rank 0 (default: off)')
    parser.add_argument('--num_processes', type=int, default=None)
    parser.add_argument('--devices', default=None)
    parser.add_argument('--bench_bin', default=None, help='Path to hixl_kv_bench (auto-detected)')
    parser.add_argument('--output_dir', default='kv_benchmark/output')
    parser.add_argument(
        '--local_buffer_min',
        default='1G',
        help='Minimum device local buffer size (e.g. 1G); allocation is max(this, workload key span)',
    )
    parser.add_argument('--pool_memory', choices=['host'], default='host')
    parser.add_argument('--model', default='deepseek-r1')
    parser.add_argument('--model_config', default=None, help='Path to model config JSON (auto-detected)')
    parser.add_argument(
        '--key_counts',
        default=None,
        help='KV block/key counts, comma-separated (default: 16,32,48,64)',
    )
    parser.add_argument(
        '--transfer_threads',
        type=int,
        default=8,
        help='Worker threads for concurrent key transfers (default: 8)',
    )
    parser.add_argument(
        '--transport',
        choices=['rdma', 'fabric_mem'],
        default=None,
        help='Transport path (default: rdma on A2, fabric_mem on A3/A5)',
    )
    parser.add_argument('--base_port', type=int, default=19000)
    parser.add_argument('--listen_host', default='127.0.0.1')
    parser.add_argument('--connect_host', default='127.0.0.1')
    parser.add_argument('--warmup', type=int, default=1)
    parser.add_argument('--repeat', type=int, default=10)
    parser.add_argument('--sync_timeout_sec', type=int, default=300)
    parser.add_argument('--skip_plot', action='store_true', help='Skip automatic plot generation')
    return parser.parse_args()


def prepare_output_dir(output_dir: str) -> None:
    out_dir = Path(output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for pattern in ('kv_result_rank*.csv', 'kv_result_rank*.json', 'kv_result_all.csv'):
        for path in out_dir.glob(pattern):
            path.unlink()
    shutil.rmtree(out_dir / '.kv_sync', ignore_errors=True)


def combine_results(output_dir: str, num_processes: int) -> Path | None:
    out_dir = Path(output_dir)
    rows = []
    fieldnames = None
    for rank in range(num_processes):
        csv_path = out_dir / f'kv_result_rank{rank}.csv'
        if not csv_path.exists():
            continue
        with csv_path.open(newline='', encoding='utf-8') as csv_file:
            reader = csv.DictReader(csv_file)
            if fieldnames is None:
                fieldnames = reader.fieldnames
            rows.extend(reader)
    if not rows or fieldnames is None:
        return None
    combined = out_dir / 'kv_result_all.csv'
    with combined.open('w', newline='', encoding='utf-8') as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    return combined


def generate_plots(args):
    if args.skip_plot:
        return
    csv_path = Path(args.output_dir) / 'kv_result_all.csv'
    if not csv_path.exists():
        csv_path = Path(args.output_dir) / 'kv_result_rank0.csv'
    if not csv_path.exists():
        log.warning(f'[WARN] {csv_path} does not exist; skip plot generation')
        return
    plot_script = Path(__file__).resolve().parent / 'plot_kv_benchmark.py'
    cmd = [sys.executable, str(plot_script), str(csv_path), f'--output_dir={args.output_dir}']
    log.info('[INFO] %s', ' '.join(cmd))
    ret = subprocess.run(cmd, check=False)
    if ret.returncode != 0:
        log.info(f'[WARN] plot generation failed for {csv_path}; keep benchmark result')


def detect_platform_for_kv(platform_override: str | None) -> str:
    if platform_override is not None:
        return platform_override
    try:
        from platform_detect import detect_platform_from_npu_smi, detect_platform
        detected = detect_platform_from_npu_smi()
        if detected is not None:
            return detected
        return detect_platform(None)
    except Exception:
        return 'a2'


def _kv_process_cmd(spec: KvProcessSpec) -> list[str]:
    args = spec.args
    cmd = [
        spec.bench_bin,
        f'--rank={spec.rank}',
        f'--device_id={spec.device}',
        f'--num_processes={args.num_processes}',
        f'--local_buffer_min={args.local_buffer_min}',
        f'--pool_memory={args.pool_memory}',
        f'--model={args.model}',
        f'--model_config={spec.model_config}',
        f'--transfer_threads={args.transfer_threads}',
        f'--transport={args.transport}',
        f'--output_dir={args.output_dir}',
        f'--run_id={spec.run_id}',
        f'--base_port={args.base_port}',
        f'--listen_host={args.listen_host}',
        f'--connect_host={args.connect_host}',
        f'--warmup={args.warmup}',
        f'--repeat={args.repeat}',
        f'--sync_timeout_sec={args.sync_timeout_sec}',
        f'--key_counts={spec.key_counts}',
    ]
    if args.trace:
        cmd.append('--trace=1')
    return cmd


def _log_run_config(args, platform_id: str, devices: list[str], model_config: str, key_counts: str) -> None:
    log.info(
        '[INFO] platform=%s num_processes=%s devices=%s model=%s model_config=%s '
        'key_counts=%s transport=%s output_dir=%s',
        platform_id,
        args.num_processes,
        ','.join(devices),
        args.model,
        model_config,
        key_counts,
        args.transport,
        args.output_dir,
    )


def main():
    args = parse_args()
    key_counts = args.key_counts or '16,32,48,64'
    run_id = f'kv_{int(time.time())}_{os.getpid()}'
    platform_id = detect_platform_for_kv(args.platform)
    if args.transport is None:
        args.transport = default_kv_transport(platform_id)
    default_num = KV_PROCESS_COUNT_BY_PLATFORM.get(platform_id, 8)
    target_processes = args.num_processes if args.num_processes is not None else default_num
    if args.devices is None:
        args.devices = ','.join((str(i) for i in range(target_processes)))
    bench_bin = args.bench_bin or find_bench_bin()
    if not Path(bench_bin).exists():
        log.error(f'[ERROR] bench binary not found: {bench_bin}')
        log.error('[ERROR] Build with: bash build.sh --examples')
        return 1
    model_config = args.model_config or find_model_config()
    if not Path(model_config).exists():
        log.error(f'[ERROR] model config not found: {model_config}')
        return 1
    user_devs = [int(x) for x in args.devices.split(',') if x.strip()]
    resolved = resolve_kv_devices(platform_id, user_devs, target_processes)
    devices = [str(d) for d in resolved]
    if len(devices) < target_processes:
        log.error(
            '[ERROR] resolved device count (%d) is less than required processes (%d)',
            len(devices),
            target_processes,
        )
        return 1
    args.num_processes = len(devices)
    prepare_output_dir(args.output_dir)
    _log_run_config(args, platform_id, devices, model_config, key_counts)
    procs = []
    for rank in range(args.num_processes):
        cmd = _kv_process_cmd(KvProcessSpec(args, bench_bin, rank, devices[rank], model_config, run_id, key_counts))
        log.info('[INFO] %s', ' '.join(cmd))
        procs.append(subprocess.Popen(cmd))
    ret = max((proc.wait() for proc in procs))
    combined = combine_results(args.output_dir, args.num_processes)
    if combined is not None:
        log.info(f'[INFO] wrote {combined}')
    if ret == 0:
        generate_plots(args)
    return ret


if __name__ == '__main__':
    sys.exit(main())
