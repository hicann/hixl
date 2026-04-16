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
import os
import struct

logger = logging.getLogger(__name__)

PAGE_SIZE = os.sysconf("SC_PAGE_SIZE")
KPF_BUDDY = 10

# Base physical addresses for target ranges (PFNs built in main).
RANGES = [
    0x29580000000,
    0xa9580000000,
    0x129580000000,
    0x1a9580000000,
]

RANGE_SIZE = 682 * 1024 * 1024 * 1024  # 682GB


def get_all_nodes():
    base = os.path.join("/sys", "devices", "system", "node")
    return sorted(
        [int(d.replace("node", "")) for d in os.listdir(base) if d.startswith("node")]
    )


def get_memory_block_size():
    with open(os.path.join("/sys", "devices", "system", "memory", "block_size_bytes")) as f:
        return int(f.read().strip(), 16)


def get_node_blocks(node):
    path = os.path.join("/sys", "devices", "system", "node", f"node{node}")
    block_size = get_memory_block_size()
    blocks = []

    for name in os.listdir(path):
        if name.startswith("memory"):
            idx = int(name.replace("memory", ""))
            start = idx * block_size // PAGE_SIZE
            end = (idx * block_size + block_size) // PAGE_SIZE - 1
            blocks.append((start, end))

    return sorted(blocks)


def is_buddy(flags):
    return (flags >> KPF_BUDDY) & 1


def addr_to_pfn(addr):
    return addr // PAGE_SIZE


def pfn_to_addr(pfn):
    return pfn * PAGE_SIZE


def intersect(a_start, a_end, b_start, b_end):
    s = max(a_start, b_start)
    e = min(a_end, b_end)
    if s <= e:
        return s, e
    return None


def _reset_run_if_block_gap(prev_end, start_pfn, merged_start, prev_state):
    """Clear run state when physical blocks are not adjacent PFN ranges."""
    if prev_end is None or start_pfn <= prev_end + 1:
        return merged_start, prev_state
    return None, None


def _apply_pfn_buddy_state(pfn, free, merged_start, prev_state):
    """
    Merge consecutive buddy/free PFNs. On transition out of a free run, emit one closed segment.
    Returns (new_merged_start, new_prev_state, list of completed free (start_pfn, end_pfn)).
    """
    if prev_state is None:
        return pfn, free, []

    if free == prev_state:
        return merged_start, prev_state, []

    completed = []
    if prev_state:
        completed.append((merged_start, pfn - 1))
    return pfn, free, completed


def _iter_block_pfns(kpageflags_file, start_pfn, end_pfn):
    """Sequential reads from kpageflags; caller must seek to start_pfn * 8 before calling."""
    for pfn in range(start_pfn, end_pfn + 1):
        data = kpageflags_file.read(8)
        if not data:
            break
        flags = struct.unpack("Q", data)[0]
        yield pfn, is_buddy(flags)


def scan_node_free_segments(node):
    blocks = get_node_blocks(node)
    merged_start = None
    prev_state = None
    prev_end = None

    with open(os.path.join("/proc", "kpageflags"), "rb") as kpf:
        for start_pfn, end_pfn in blocks:
            merged_start, prev_state = _reset_run_if_block_gap(
                prev_end, start_pfn, merged_start, prev_state)

            kpf.seek(start_pfn * 8)
            for pfn, free in _iter_block_pfns(kpf, start_pfn, end_pfn):
                merged_start, prev_state, done = _apply_pfn_buddy_state(
                    pfn, free, merged_start, prev_state)
                yield from done

            prev_end = end_pfn

        if prev_state:
            yield (merged_start, prev_end)


def fmt(addr):
    return f"0x{addr:016x}"


def run_for_node(node, min_mb, target_ranges):
    logger.info("")
    logger.info("===== NUMA node %s =====", node)

    total_intersect_mb = 0

    for free_start, free_end in scan_node_free_segments(node):
        for r_start, r_end in target_ranges:
            inter = intersect(free_start, free_end, r_start, r_end)
            if not inter:
                continue

            s, e = inter
            size_mb = (e - s + 1) * PAGE_SIZE / 1024 / 1024

            if size_mb < min_mb:
                continue

            total_intersect_mb += size_mb

            logger.info(
                "FREE∩RANGE : %s - %s  size=%.2f MB",
                fmt(pfn_to_addr(s)),
                fmt(pfn_to_addr(e)),
                size_mb,
            )

    logger.info("Total intersect FREE: %.2f MB", total_intersect_mb)


def main():
    import argparse

    parser = argparse.ArgumentParser(description="NUMA free memory intersect tool (all nodes supported)")
    parser.add_argument("-n", "--node", type=int, help="NUMA node ID (default: all)")
    parser.add_argument("-m", "--min-mb", type=float, default=2048)
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Enable debug logging",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(message)s",
    )

    # Build target PFN ranges from configured base addresses.
    target_ranges = []
    for base in RANGES:
        start = addr_to_pfn(base)
        end = addr_to_pfn(base + RANGE_SIZE - 1)
        target_ranges.append((start, end))

    # Select NUMA nodes to scan.
    if args.node is not None:
        nodes = [args.node]
    else:
        nodes = get_all_nodes()

    logger.info("Scanning NUMA nodes...\n")

    for node in nodes:
        run_for_node(node, args.min_mb, target_ranges)


if __name__ == "__main__":
    main()
