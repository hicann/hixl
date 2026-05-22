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

"""Plot a single HIXL communication benchmark CSV result.

Generates bandwidth vs block_size and P99 latency vs block_size charts.
For batch chart generation across all directions/transports, use render_perf_md.py.
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

_BENCHMARKS_DIR = Path(__file__).resolve().parents[2]
if str(_BENCHMARKS_DIR) not in sys.path:
    sys.path.insert(0, str(_BENCHMARKS_DIR))

from benchmark_log import configure_logging  # noqa: E402

configure_logging()
log = logging.getLogger(__name__)

BLOCK_ORDER = ["16K", "32K", "64K", "128K", "256K", "512K", "1M", "2M", "4M", "8M"]


def block_sort_key(label: str) -> int:
    try:
        return BLOCK_ORDER.index(label)
    except ValueError:
        return 999


# ---------------------------------------------------------------------------
# Single-run mode
# ---------------------------------------------------------------------------


def plot_single(csv_path: Path, output_dir: Path) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        log.warning("[WARN] matplotlib is not installed; skip plot generation")
        return

    rows = _load_csv(csv_path)
    if not rows:
        log.warning(f"[WARN] no data in {csv_path}")
        return

    direction = rows[0].get("direction", "unknown")
    transport = rows[0].get("transport", "unknown")
    title = f"{direction} ({transport})"

    by_block = _avg_by_block(rows, "bandwidth_gbps")
    _line_chart(
        plt,
        ChartPlotSpec(
            by_block=by_block,
            title=title,
            xlabel="Block",
            ylabel="Bandwidth (GB/s)",
            output=output_dir / f"comm_bandwidth_{direction}_{transport}.png",
        ),
    )

    by_block_p99 = _avg_by_block(rows, "p99_us")
    if by_block_p99:
        _line_chart(
            plt,
            ChartPlotSpec(
                by_block=by_block_p99,
                title=f"{title} — P99",
                xlabel="Block",
                ylabel="P99 (us)",
                output=output_dir / f"comm_p99_{direction}_{transport}.png",
            ),
        )


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _load_csv(path: Path) -> list[dict]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def _avg_by_block(rows: list[dict], field: str) -> dict[str, float]:
    groups: dict[str, list[float]] = defaultdict(list)
    for r in rows:
        try:
            val = float(r[field])
            blk = r.get("block_size", "")
        except (KeyError, ValueError):
            continue
        groups[blk].append(val)
    return {k: statistics.mean(v) for k, v in groups.items()}


@dataclass
class ChartPlotSpec:
    by_block: dict
    title: str
    xlabel: str
    ylabel: str
    output: Path


def _line_chart(plt, spec: ChartPlotSpec) -> None:
    items = sorted(spec.by_block.items(), key=lambda kv: block_sort_key(kv[0]))
    if not items:
        return
    labels, ys = zip(*items)
    xs = list(range(len(labels)))
    plt.figure(figsize=(9, 5))
    plt.plot(xs, ys, marker="o", linestyle="-", linewidth=1.5, markersize=5)
    plt.xticks(xs, labels, rotation=35, ha="right")
    plt.xlabel(spec.xlabel)
    plt.ylabel(spec.ylabel)
    plt.title(spec.title)
    plt.grid(True, linestyle=":", alpha=0.4)
    plt.tight_layout()
    plt.savefig(spec.output, dpi=140)
    plt.close()
    log.info(f"[INFO] wrote {spec.output}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot single HIXL communication benchmark CSV")
    parser.add_argument("--csv", type=Path, required=True, help="CSV from hixl_comm_bench")
    parser.add_argument("--output_dir", type=Path, default=None, help="Output directory for PNGs")
    args = parser.parse_args()

    out_dir = args.output_dir or args.csv.parent
    out_dir.mkdir(parents=True, exist_ok=True)
    plot_single(args.csv, out_dir)


if __name__ == "__main__":
    main()
