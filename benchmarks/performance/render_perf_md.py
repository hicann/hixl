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

"""Render perf.md directly from hixl_comm_bench CSV outputs.

Reads all comm_result_*.csv files, computes average bandwidth per
(direction, transport, block_size), and writes a single-platform perf.md.
Bandwidth values are decimal GB/s (10^9 bytes per GB), matching hixl_comm_bench output.
Also prints the performance table to stdout.
"""
from __future__ import annotations

import argparse
import csv
import logging
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

_BENCHMARKS_DIR = Path(__file__).resolve().parents[1]
if str(_BENCHMARKS_DIR) not in sys.path:
    sys.path.insert(0, str(_BENCHMARKS_DIR))
from benchmark_log import configure_logging  # noqa: E402

configure_logging()
log = logging.getLogger(__name__)

DIRECTION_ORDER = ['D2rD', 'D2rH', 'H2rH', 'H2rD', 'rD2D', 'rH2D', 'rH2H', 'rD2H']
TRANSPORT_ORDER = ['hccs', 'rdma', 'fabric_mem', 'uboe']
HCCS_DIRECTIONS_A2 = frozenset({'D2rD', 'rD2D'})
HCCS_DIRECTIONS_A3 = frozenset({'D2rD', 'rD2D', 'H2rD', 'rD2H'})
UBOE_DIRECTIONS = frozenset(DIRECTION_ORDER)
DIRECTION_LABEL: dict[str, str] = {
    'D2rD': 'D → rD (write D2D)',
    'rD2D': 'rD → D (read D2D)',
    'D2rH': 'D → rH (write D2H)',
    'rH2D': 'rH → D (read D2H)',
    'H2rH': 'H → rH (write H2H)',
    'rH2H': 'rH → H (read H2H)',
    'H2rD': 'H → rD (write H2D)',
    'rD2H': 'rD → H (read H2D)',
}
TRANSPORT_LABEL: dict[str, str] = {'hccs': 'HCCS', 'rdma': 'ROCE', 'uboe': 'UBOE', 'fabric_mem': 'FabricMem'}
BLOCK_SORT_ORDER = ['16K', '32K', '64K', '128K', '256K', '512K', '1M', '2M', '4M', '8M']


def block_sort_key(b: str) -> int:
    try:
        return BLOCK_SORT_ORDER.index(b)
    except ValueError:
        return 999


def block_label_from_bytes(bs: int | str) -> str:
    if isinstance(bs, str):
        bs = int(bs.strip())
    if bs % (1024 * 1024) == 0:
        n = bs // (1024 * 1024)
        return f'{n}M'
    if bs % 1024 == 0:
        n = bs // 1024
        return f'{n}K'
    return str(bs)


def fmt_cell(val: Any) -> str:
    if val is None:
        return '——'
    if isinstance(val, float):
        s = f'{val:.3f}'.rstrip('0').rstrip('.')
        return s
    return str(val)


def hccs_supported_directions(platform: str) -> frozenset[str]:
    pid = normalize_platform(platform)
    if pid == 'a5':
        return frozenset()
    if pid == 'a3':
        return HCCS_DIRECTIONS_A3
    return HCCS_DIRECTIONS_A2


def cell_not_supported(direction: str, transport: str, platform: str) -> bool:
    if transport == 'uboe' and platform in ('a2', 'a3'):
        return True
    if transport != 'hccs':
        return False
    return direction not in hccs_supported_directions(platform)


def format_bandwidth_cell(
    direction: str,
    transport: str,
    block_label: str,
    lookup: dict[str, dict[str, dict[str, float]]],
    platform: str,
) -> str:
    if cell_not_supported(direction, transport, platform):
        return '不支持'
    val = lookup.get(direction, {}).get(transport, {}).get(block_label)
    return fmt_cell(val)


def load_csvs(csv_dir: Path) -> list[dict]:
    """Load all comm_result_*.csv and return list of merged rows."""
    rows: list[dict] = []
    for csv_path in sorted(csv_dir.glob('comm_result_*.csv')):
        with csv_path.open(newline='', encoding='utf-8') as f:
            rows.extend(list(csv.DictReader(f)))
    return rows


def build_lookup(rows: list[dict]) -> dict[str, dict[str, dict[str, float]]]:
    """Build direction -> transport -> block_label -> avg_bandwidth_gbps."""
    groups: dict[str, dict[str, dict[str, list[float]]]] = defaultdict(
        lambda: defaultdict(lambda: defaultdict(list))
    )
    for r in rows:
        direction = r.get('direction', '').strip()
        transport = r.get('transport', '').strip()
        try:
            bw = float(r['bandwidth_gbps'])
            blk = int(r['block_size'])
        except (KeyError, ValueError):
            continue
        if not direction or not transport:
            continue
        label = block_label_from_bytes(blk)
        groups[direction][transport][label].append(bw)
    result: dict[str, dict[str, dict[str, float]]] = {}
    for (dname, tports) in groups.items():
        result[dname] = {}
        for (tport, blocks) in tports.items():
            result[dname][tport] = {b: statistics.mean(vals) for (b, vals) in blocks.items()}
    return result


def collect_columns_with_data(lookup: dict[str, dict[str, dict[str, float]]]) -> list[tuple[str, str]]:
    """Return ordered list of (direction, transport) pairs that have numeric data."""
    cols: list[tuple[str, str]] = []
    for dname in DIRECTION_ORDER:
        if dname not in lookup:
            continue
        for tport in TRANSPORT_ORDER:
            if tport in lookup[dname] and lookup[dname][tport]:
                cols.append((dname, tport))
    return cols


def infer_deployment_from_rows(rows: list[dict]) -> str:
    """Infer single vs dual-machine from CSV pattern (benchmark_group)."""
    for row in rows:
        pattern = row.get('pattern', '').strip().lower()
        if pattern == 'dual':
            return 'dual'
    return 'single'


def transports_for_platform(platform: str, deployment: str) -> list[str]:
    """Transports to show in perf tables for the given platform."""
    pid = normalize_platform(platform)
    if pid == 'a2':
        if deployment == 'dual':
            return ['rdma']
        return ['hccs', 'rdma']
    if pid == 'a3':
        return ['hccs', 'rdma', 'fabric_mem']
    return ['rdma', 'fabric_mem', 'uboe']


def collect_columns_for_platform(platform: str, deployment: str = 'single') -> list[tuple[str, str]]:
    """Full direction × transport matrix for perf tables (includes unsupported HCCS columns)."""
    cols: list[tuple[str, str]] = []
    for dname in DIRECTION_ORDER:
        for tport in transports_for_platform(platform, deployment):
            cols.append((dname, tport))
    return cols


def render_table(lookup: dict[str, dict[str, dict[str, float]]], platform: str, deployment: str = 'single') -> str:
    cols = collect_columns_for_platform(platform, deployment)
    all_blocks: set[str] = set()
    for (_dname, _tport) in collect_columns_with_data(lookup):
        all_blocks.update(lookup.get(_dname, {}).get(_tport, {}).keys())
    ordered_blocks = sorted(all_blocks, key=block_sort_key)
    if not ordered_blocks:
        return '*(No bandwidth data in CSVs — run comm benchmarks first)*\n\n'
    lines: list[str] = []
    header = '| **Block** |'
    sep = '| :---: |'
    for (dname, tport) in cols:
        header += f' **{dname}<br>{TRANSPORT_LABEL.get(tport, tport)}** |'
        sep += ' :---: |'
    lines.append(header)
    lines.append(sep)
    for bs in ordered_blocks:
        row = f'| {bs} |'
        for (dname, tport) in cols:
            row += f' {format_bandwidth_cell(dname, tport, bs, lookup, platform)} |'
        lines.append(row)
    return '\n'.join(lines) + '\n'


def _table_column_header(dname: str, tport: str) -> str:
    return f'{dname}/{TRANSPORT_LABEL.get(tport, tport)}'


def _build_table_matrix(
    lookup: dict[str, dict[str, dict[str, float]]],
    platform: str,
    deployment: str = 'single',
) -> tuple[list[str], list[list[str]]] | None:
    cols = collect_columns_for_platform(platform, deployment)
    all_blocks: set[str] = set()
    for (dname, tport) in collect_columns_with_data(lookup):
        all_blocks.update(lookup.get(dname, {}).get(tport, {}).keys())
    ordered_blocks = sorted(all_blocks, key=block_sort_key)
    if not ordered_blocks:
        return None
    headers = ['Block'] + [_table_column_header(dname, tport) for (dname, tport) in cols]
    rows: list[list[str]] = []
    for bs in ordered_blocks:
        rows.append([bs] + [format_bandwidth_cell(dname, tport, bs, lookup, platform) for (dname, tport) in cols])
    return (headers, rows)


def render_table_terminal(
    lookup: dict[str, dict[str, dict[str, float]]],
    platform: str,
    deployment: str = 'single',
) -> str:
    """Render an ASCII table for terminal output (GB/s)."""
    matrix = _build_table_matrix(lookup, platform, deployment)
    if matrix is None:
        return '(No bandwidth data in CSVs — run comm benchmarks first)'
    (headers, rows) = matrix
    widths: list[int] = []
    for (col_idx, header) in enumerate(headers):
        width = len(header)
        for row in rows:
            width = max(width, len(row[col_idx]))
        widths.append(width)

    def pad_row(cells: list[str]) -> str:
        return '| ' + ' | '.join((cell.ljust(widths[idx]) for (idx, cell) in enumerate(cells))) + ' |'

    def separator() -> str:
        return '+-' + '-+-'.join(('-' * width for width in widths)) + '-+'
    lines = [separator(), pad_row(headers), separator()]
    lines.extend((pad_row(row) for row in rows))
    lines.append(separator())
    return '\n'.join(lines)


def direction_legend() -> str:
    lines = [
        '### 方向命名速查\n\n',
        '方向名格式为 `源 → 远程目标`，其中 **D**=Device、**H**=Host、**r**=remote。\n\n',
        '| 方向 | 含义 | 操作 |\n',
        '| :--- | :--- | :---: |\n',
        '| **D2rD** | Device 写往远程 Device | write |\n',
        '| **rD2D** | 从远程 Device 读回 Device | read |\n',
        '| **D2rH** | Device 写往远程 Host | write |\n',
        '| **rH2D** | 从远程 Host 读回 Device | read |\n',
        '| **H2rH** | Host 写往远程 Host | write |\n',
        '| **rH2H** | 从远程 Host 读回 Host | read |\n',
        '| **H2rD** | Host 写往远程 Device | write |\n',
        '| **rD2H** | 从远程 Device 读回 Host | read |\n',
        '\n',
    ]
    return ''.join(lines)


def normalize_platform(platform: str) -> str:
    """Accept a2/a3/a5 or legacy atlas_* aliases."""
    p = platform.strip()
    legacy = {'atlas_a2': 'a2', 'atlas_a3': 'a3', 'atlas_a5': 'a5'}
    return legacy.get(p, p)


def _a2_intro(deployment: str) -> str:
    lines = [
        '本页展示 HIXL 在 **昇腾 A2**（Atlas 800I A2 / A200I A2 Box）上的通信性能数据。\n\n'
        '**测试方式**：'
    ]
    if deployment == 'dual':
        lines.append(
            '跨两台机器分别运行 `hixl_comm_bench` 的 target 和 initiator，'
            '测量不同 block size（16K～2M）下的有效带宽。\n\n'
            '**约束**：**A2 双机场景不支持 HCCS**，请使用 **RDMA**。\n\n'
        )
    else:
        lines.append(
            '在同一台机器上运行 `hixl_comm_bench` 的 target 和 initiator，'
            '测量不同 block size（16K～2M）下的有效带宽。\n\n'
            '**约束**：**HCCS** 在 A2（Ascend910B-class）上仅 **D2D**'
            '（`D2rD` / `rD2D`）；涉及 Host 或其它方向请使用 **RDMA**。\n\n'
        )
    return ''.join(lines)


def _platform_intro_body(pid: str, deployment: str) -> str:
    if pid == 'a2':
        return _a2_intro(deployment)
    if pid == 'a3':
        return (
            '本页展示 HIXL 在 **昇腾 A3**（Atlas A3 训练/推理系列）上的通信性能数据。\n\n'
            '**测试方式**：在同一台机器上运行 `hixl_comm_bench` 的 target 和 initiator，'
            '测量不同 block size（16K～2M）下的有效带宽。\n\n'
            '**约束**：**HCCS** 在 A3（Ascend910-class）上支持 **D2D**（`D2rD` / `rD2D`）'
            '以及 **H2rD** / **rD2H**；其余方向请使用 **RDMA** 或 **FabricMem**。\n\n'
        )
    if pid == 'a5':
        return (
            '本页展示 HIXL 在 **昇腾 A5（Ascend950-class）** 上的通信性能数据。\n\n'
            '**测试方式**：在同一台机器上运行 `hixl_comm_bench` 的 target 和 initiator，'
            '测量不同 block size（16K～2M）下的有效带宽。\n\n'
            '**约束**：**Ascend950-class 产品上 comm benchmark 不使用 HCCS**；'
            '通信方向请查看 **RDMA** 或 **FabricMem** 列。表中 **HCCS** 列均为 **不支持**。\n\n'
        )
    return ''


def _table_reader_note(deployment: str) -> str:
    lines = [
        '### 如何读懂表格\n\n'
        '- **Block** 列：每次传输的数据块大小（16K = 16 KiB，1M = 1 MiB）。\n'
        '- **方向+传输** 列：例如 `D2rD HCCS` 表示从本地 Device 写往远程 Device（HCCS 传输），'
        '`rH2D ROCE` 表示从远程 Host 读回本地 Device（ROCE 传输）。\n'
        '- 数值为有效带宽（GB/s）；**不支持** 表示该传输路径在该平台上不支持该方向 '
        '（例如 A5 不提供 HCCS comm；A2 上 HCCS 仅 D2D）；**——** 表示尚未采集到数据。\n'
        '- 所有数据来自'
    ]
    if deployment == 'dual':
        lines.append('双机 1:1（一个 initiator + 一个 target）测试。\n\n')
    else:
        lines.append('单机 1:1（一个 initiator + 一个 target）测试。\n\n')
    return ''.join(lines)


def platform_intro(platform: str, deployment: str) -> str:
    pid = normalize_platform(platform)
    lines = [f'## {platform_title(platform)}\n\n']
    lines.append(_platform_intro_body(pid, deployment))
    lines.append(_table_reader_note(deployment))
    return ''.join(lines)


def platform_title(platform: str) -> str:
    pid = normalize_platform(platform)
    titles = {
        'a2': '昇腾 A2 (Ascend910B) 通信性能',
        'a3': '昇腾 A3 (Ascend910) 通信性能',
        'a5': '昇腾 A5 (Ascend950) 通信性能',
    }
    return titles.get(pid, f'{pid} 通信性能')


def _chart_block_axes(lookup: dict[str, dict[str, dict[str, float]]]) -> tuple[list[str], set[str]]:
    block_sizes: set[str] = set()
    for tports in lookup.values():
        for blocks in tports.values():
            block_sizes.update(blocks.keys())
    ordered_blocks = sorted(block_sizes, key=block_sort_key)
    transports_seen: set[str] = set()
    for tports in lookup.values():
        for tport, blocks in tports.items():
            if blocks:
                transports_seen.add(tport)
    return ordered_blocks, transports_seen


@dataclass
class TransportChartSpec:
    mpl: Any
    lookup: dict[str, dict[str, dict[str, float]]]
    platform: str
    output_dir: Path
    tport: str
    ordered_blocks: list[str]
    colors: list[str]


def _render_transport_chart(spec: TransportChartSpec) -> str:
    mpl = spec.mpl
    fig, ax = mpl.subplots(figsize=(12, 6))
    color_idx = 0
    for dname in DIRECTION_ORDER:
        bw_map = spec.lookup.get(dname, {}).get(spec.tport, {})
        if not bw_map:
            continue
        xs: list[int] = []
        ys: list[float] = []
        for block in spec.ordered_blocks:
            if block not in bw_map:
                continue
            xs.append(len(xs))
            ys.append(bw_map[block])
        if not ys:
            continue
        color = spec.colors[color_idx % len(spec.colors)]
        color_idx += 1
        ax.plot(xs, ys, marker='o', linestyle='-', color=color, linewidth=1.2, markersize=4, label=dname)
    ax.set_xticks(range(len(spec.ordered_blocks)))
    ax.set_xticklabels(spec.ordered_blocks, rotation=35, ha='right', fontsize=8)
    ax.set_xlabel('Block', fontsize=10)
    ax.set_ylabel('Bandwidth (GB/s)', fontsize=10)
    ax.set_title(f'{spec.platform} — {TRANSPORT_LABEL.get(spec.tport, spec.tport)}', fontsize=12)
    ax.grid(True, linestyle=':', alpha=0.4)
    ax.legend(fontsize=8, loc='upper left')
    fig.tight_layout()
    out_png = spec.output_dir / f'{spec.platform}_{spec.tport}_bandwidth.png'
    out_png.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_png, dpi=140)
    mpl.close(fig)
    log.info(f'[INFO] wrote {out_png}')
    rel = Path('performance/figures') / out_png.name
    return f'\n#### {TRANSPORT_LABEL.get(spec.tport, spec.tport)} 带宽折线图\n\n![]({rel.as_posix()})\n\n'


def render_charts(lookup: dict[str, dict[str, dict[str, float]]], platform: str, output_dir: Path) -> list[str]:
    """Generate per-transport bandwidth PNGs. Returns relative paths to embed."""
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as mpl
    except ImportError:
        log.warning('[WARN] matplotlib not installed; skip PNG charts')
        return []
    ordered_blocks, transports_seen = _chart_block_axes(lookup)
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b', '#e377c2', '#7f7f7f']
    embeds: list[str] = []
    for tport in sorted(transports_seen, key=lambda t: TRANSPORT_ORDER.index(t) if t in TRANSPORT_ORDER else 99):
        spec = TransportChartSpec(mpl, lookup, platform, output_dir, tport, ordered_blocks, colors)
        embeds.append(_render_transport_chart(spec))
    return embeds


def main() -> None:
    ap = argparse.ArgumentParser(description='Render perf.md from hixl_comm_bench CSV outputs')
    ap.add_argument('--csv-dir', type=Path, required=True, help='Directory containing comm_result_*.csv files')
    ap.add_argument(
        '--platform',
        type=str,
        required=True,
        help='Platform ID: a2, a3, or a5 (legacy: atlas_a2, atlas_a3, atlas_a5)',
    )
    ap.add_argument('--output', type=Path, default=None, help='Output perf.md path (default: benchmarks/perf.md)')
    ap.add_argument('--no-plots', action='store_true', help='Skip PNG chart generation')
    args = ap.parse_args()
    rows = load_csvs(args.csv_dir)
    if not rows:
        log.warning(f'[WARN] No CSV rows found in {args.csv_dir}')
        return
    platform = normalize_platform(args.platform)
    deployment = infer_deployment_from_rows(rows)
    if deployment == 'dual':
        log.info('[INFO] Detected dual-machine benchmark data from CSV pattern')
    lookup = build_lookup(rows)
    data_cols = collect_columns_with_data(lookup)
    if not data_cols:
        log.warning('[WARN] No direction+transport combos with numeric bandwidth data found')
    intro = platform_intro(platform, deployment)
    legend = direction_legend()
    table = render_table(lookup, platform, deployment)
    sections = [intro, legend, '### 性能数据\n\n', table]
    benchmarks_dir = Path(__file__).resolve().parents[1]
    figures_dir = benchmarks_dir / 'performance' / 'figures'
    if not args.no_plots:
        chart_embeds = render_charts(lookup, platform, figures_dir)
        sections.extend(chart_embeds)
    md_content = ''.join(sections)
    out_path = args.output or benchmarks_dir / 'perf.md'
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(md_content, encoding='utf-8')
    log.info(f'[INFO] wrote {out_path}')
    log.info('\n' + '=' * 70)
    log.info(f'  {platform_title(platform)}')
    log.info('=' * 70)
    log.info('  Bandwidth (GB/s, decimal)')
    log.info()
    log.info(render_table_terminal(lookup, platform, deployment))


if __name__ == '__main__':
    main()
