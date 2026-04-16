#!/bin/bash
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

# Launch N processes for fabric_mem_kv_benchmark (rank i uses device i by default).
# Merges all rank stdout/stderr into one log file and appends a SUMMARY (mean get over ranks;
# rank-0 put / get-max lines pass through from the binary).
#
# Usage: run_fabric_mem_kv_benchmark.sh <host_ip> <base_port> <binary> [world_size] [log_file]
#
# The awk summary script lives next to this file: fabric_mem_kv_benchmark_summary.awk

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUMMARY_AWK="${SCRIPT_DIR}/fabric_mem_kv_benchmark_summary.awk"

HOST_IP="${1:-127.0.0.1}"
BASE_PORT="${2:-22000}"
BIN="${3:-./fabric_mem_kv_benchmark}"
WORLD="${4:-16}"
LOGFILE="${5:-fabric_mem_kv_benchmark.log}"

if [[ ! -f "${SUMMARY_AWK}" ]]; then
  echo "error: missing ${SUMMARY_AWK}" >&2
  exit 1
fi

SYNC_DIR="${TMPDIR:-/tmp}/fabric_mem_kv_sync_$$"
LOG_PART_DIR="${TMPDIR:-/tmp}/fabric_mem_kv_ranklogs_$$"

cleanup() {
  if [[ -n "${SYNC_DIR}" && -d "${SYNC_DIR}" ]]; then
    rm -r -- "${SYNC_DIR}"
  fi
  if [[ -n "${LOG_PART_DIR}" && -d "${LOG_PART_DIR}" ]]; then
    rm -r -- "${LOG_PART_DIR}"
  fi
}
trap cleanup EXIT

mkdir -p "${LOG_PART_DIR}"

for R in $(seq 0 $((WORLD - 1))); do
  "${BIN}" "${R}" "${R}" "${HOST_IP}" "${BASE_PORT}" "${SYNC_DIR}" "${WORLD}" \
    >"${LOG_PART_DIR}/rank_${R}.log" 2>&1 &
done
wait

merge_rank_logs() {
  local R
  for R in $(seq 0 $((WORLD - 1))); do
    cat "${LOG_PART_DIR}/rank_${R}.log"
  done
}

{
  echo "========== FULL OUTPUT (ranks 0..$((WORLD - 1))) =========="
  for R in $(seq 0 $((WORLD - 1))); do
    echo ""
    echo "---------- rank ${R} ----------"
    cat "${LOG_PART_DIR}/rank_${R}.log"
  done
  echo ""
  echo "========== SUMMARY =========="
  merge_rank_logs | awk -f "${SUMMARY_AWK}"
} >"${LOGFILE}"

echo "Wrote full log and SUMMARY to: ${LOGFILE}"
