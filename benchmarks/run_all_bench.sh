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
#
# Run all HIXL benchmarks and generate perf.md.
#
# Usage:
#   bash benchmarks/run_all_bench.sh                    # single machine
#   bash benchmarks/run_all_bench.sh --skip-kv --loops 10
#   bash benchmarks/run_all_bench.sh --hixl-option OPTION_KEY=VALUE  # comm bench, repeatable
#

set -e

BASEPATH=$(cd "$(dirname "$0")/.."; pwd)
cd "$BASEPATH"

# --- usage ---
usage() {
  echo "Usage:"
  echo "  bash benchmarks/run_all_bench.sh [OPTIONS]"
  echo ""
  echo "Options:"
  echo "  --loops <N>             Repeat full block-step ladder per comm combo (default: 5)"
  echo "  --device-ids <LIST>     Device IDs, comma-separated (default: 0,1,2,3,4,5,6,7)"
  echo "  --platform <a2|a3|a5>   Skip npu-smi detection and use this platform"
  echo "  --target-host <IP>      Deprecated. Use run_comm_benchmark.py --role for dual-machine mode."
  echo "  --skip-comm             Skip communication benchmarks"
  echo "  --skip-kv               Skip KV cache benchmarks"
  echo "  --hixl-option <K=V>     Extra HIXL init option for comm bench (repeatable)"
  echo "  --output <PATH>         perf.md output path (default: benchmarks/perf.md)"
  echo "  -h, --help              Print this message"
  echo ""
  echo "Examples:"
  echo "  bash benchmarks/run_all_bench.sh"
  echo "  bash benchmarks/run_all_bench.sh --loops 10 --device-ids 0,1"
}

# --- check CANN environment ---
check_env() {
  if ! command -v npu-smi &>/dev/null; then
    echo "[WARN] npu-smi not found. Trying to source CANN environment..."
    if [ -f /usr/local/Ascend/cann/set_env.sh ]; then
      # shellcheck source=/dev/null
      source /usr/local/Ascend/cann/set_env.sh
    fi
    if ! command -v npu-smi &>/dev/null; then
      echo "[WARN] npu-smi still not found; run_all_benchmarks.py will prompt for platform."
    fi
  fi
}

# --- main ---
LOOPS=5
DEVICE_IDS="0,1,2,3,4,5,6,7"
PLATFORM=""
TARGET_HOST="127.0.0.1"
SKIP_COMM=""
SKIP_KV=""
OUTPUT=""
EXTRA_PY=()

require_option_value() {
  local opt="$1"
  local argc="$2"
  local val="${3:-}"
  if [ "$argc" -lt 2 ] || [ -z "$val" ] || [[ "$val" == -* ]]; then
    echo "[ERROR] Option $opt requires a non-empty value." >&2
    usage >&2
    exit 1
  fi
}

require_nonempty_eq_value() {
  local opt="$1"
  local val="$2"
  if [ -z "$val" ]; then
    echo "[ERROR] Option $opt requires a non-empty value." >&2
    usage >&2
    exit 1
  fi
}

while [ $# -gt 0 ]; do
  case "$1" in
    --loops)
      require_option_value "$1" "$#" "${2:-}"
      LOOPS="$2"; shift 2 ;;
    --loops=*)
      require_nonempty_eq_value "--loops=" "${1#*=}"
      LOOPS="${1#*=}"; shift ;;
    --device-ids)
      require_option_value "$1" "$#" "${2:-}"
      DEVICE_IDS="$2"; shift 2 ;;
    --device-ids=*)
      require_nonempty_eq_value "--device-ids=" "${1#*=}"
      DEVICE_IDS="${1#*=}"; shift ;;
    --platform)
      require_option_value "$1" "$#" "${2:-}"
      PLATFORM="$2"; shift 2 ;;
    --platform=*)
      require_nonempty_eq_value "--platform=" "${1#*=}"
      PLATFORM="${1#*=}"; shift ;;
    --target-host)
      require_option_value "$1" "$#" "${2:-}"
      TARGET_HOST="$2"; shift 2 ;;
    --target-host=*)
      require_nonempty_eq_value "--target-host=" "${1#*=}"
      TARGET_HOST="${1#*=}"; shift ;;
    --skip-comm)
      SKIP_COMM="--skip_comm"; shift ;;
    --skip-kv)
      SKIP_KV="--skip_kv"; shift ;;
    --hixl-option)
      require_option_value "$1" "$#" "${2:-}"
      EXTRA_PY+=(--hixl_option "$2")
      shift 2 ;;
    --hixl-option=*)
      require_nonempty_eq_value "--hixl-option=" "${1#*=}"
      EXTRA_PY+=(--hixl_option "${1#*=}")
      shift ;;
    --output)
      require_option_value "$1" "$#" "${2:-}"
      OUTPUT="$2"; shift 2 ;;
    --output=*)
      require_nonempty_eq_value "--output=" "${1#*=}"
      OUTPUT="${1#*=}"; shift ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown option: $1"
      usage; exit 1 ;;
  esac
done

if ! [[ "$LOOPS" =~ ^[1-9][0-9]*$ ]]; then
  echo "[ERROR] --loops must be a positive integer: ${LOOPS}" >&2
  exit 1
fi

if [ "$TARGET_HOST" != "127.0.0.1" ]; then
  echo "[ERROR] run_all_bench.sh does not support true dual-machine orchestration."
  echo "[ERROR] Use benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --role=target/initiator instead."
  exit 1
fi

check_env

echo "============================================"
echo "  HIXL Benchmark Runner"
echo "============================================"
echo "  Loops:       ${LOOPS}"
echo "  Devices:     ${DEVICE_IDS}"
echo "  Target host: ${TARGET_HOST}"
echo "============================================"
echo ""

# Build benchmarks if needed
COMM_BIN="${BASEPATH}/build/benchmarks/comm_benchmark/hixl_comm_bench"
KV_BIN="${BASEPATH}/build/benchmarks/kv_benchmark/hixl_kv_bench"
if { [ -z "$SKIP_COMM" ] && [ ! -f "$COMM_BIN" ]; } ||
   { [ -z "$SKIP_KV" ] && [ ! -f "$KV_BIN" ]; }; then
  echo "[INFO] Benchmarks not built yet, building..."
  bash "${BASEPATH}/build.sh" --examples
fi

# Build Python args
PY_ARGS=(--loops "${LOOPS}" --device_ids "${DEVICE_IDS}")
if [ -n "$PLATFORM" ]; then
  PY_ARGS+=(--platform "${PLATFORM}")
fi
if [ -n "$SKIP_COMM" ]; then
  PY_ARGS+=("${SKIP_COMM}")
fi
if [ -n "$SKIP_KV" ]; then
  PY_ARGS+=("${SKIP_KV}")
fi
if [ "$TARGET_HOST" != "127.0.0.1" ]; then
  PY_ARGS+=(--target_host "${TARGET_HOST}")
fi
if [ -n "$OUTPUT" ]; then
  PY_ARGS+=(--output "${OUTPUT}")
fi

# Run benchmarks
python3 "${BASEPATH}/benchmarks/run_all_benchmarks.py" "${PY_ARGS[@]}" "${EXTRA_PY[@]}"

# Print output location
if [ -n "$OUTPUT" ]; then
  echo "[INFO] perf.md written to: ${OUTPUT}"
else
  echo "[INFO] perf.md written to: ${BASEPATH}/benchmarks/perf.md"
fi
