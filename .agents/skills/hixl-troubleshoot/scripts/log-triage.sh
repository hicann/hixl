#!/usr/bin/env bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deps_dir="${HOME}/hixl-troubleshooting"

usage() {
  cat <<EOF
Usage: $0 [--source NAME=LOG_DIR]... [--top N] [--context N] [LOG_DIR...]

Merge HIXL-related errors from one or more plog roots by log timestamp.

Examples:
  $0 /root/ascend/log
  $0 --source P=/logs/p/ascend/log --source D=/logs/d/ascend/log
EOF
}

timeline_args=()
source_dirs=()
plain_dirs=()
while [ $# -gt 0 ]; do
  case "$1" in
    --source)
      [ $# -ge 2 ] || { echo "--source requires NAME=LOG_DIR" >&2; exit 2; }
      spec="$2"
      label="${spec%%=*}"
      directory="${spec#*=}"
      if [ "${label}" = "${spec}" ] || [ -z "${label}" ] || [ -z "${directory}" ]; then
        echo "Invalid --source '${spec}'; expected NAME=LOG_DIR." >&2
        exit 2
      fi
      timeline_args+=(--source "${spec}")
      source_dirs+=("${directory}")
      shift 2
      ;;
    --top|--context)
      [ $# -ge 2 ] || { echo "$1 requires a value" >&2; exit 2; }
      timeline_args+=("$1" "$2")
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      plain_dirs+=("$1")
      shift
      ;;
  esac
done

if [ "${#source_dirs[@]}" -eq 0 ] && [ "${#plain_dirs[@]}" -eq 0 ]; then
  plain_dirs=("${HOME}/ascend/log")
fi

log_dirs=("${source_dirs[@]}" "${plain_dirs[@]}")
for log_dir in "${log_dirs[@]}"; do
  if [ ! -d "${log_dir}" ]; then
    echo "Log directory not found: ${log_dir}" >&2
    exit 1
  fi
done

echo "=== HIXL log triage ==="
echo
python3 "${script_dir}/log_timeline.py" "${timeline_args[@]}" "${plain_dirs[@]}"
echo

matches_file="$(mktemp)"
trap 'rm -f "${matches_file}"' EXIT
grep -rniE 'HCCL|HCCP|DRV|RUNTIME|ADXL|\[HIXL\]|HIXL CS|HixlClient|HixlServer|HixlCS|AscendDirect|HcclCommPrepare|LINK_ERROR_INFO|CheckMemCpyAttr|remoteBuffer|FabricMem|\[FabricMemEngine\]|Fabric mem transfer|Connect statistic|Direct transfer statistic|local_comm_res|halMemImport|suspect remote|cpu stuck' \
  "${log_dirs[@]}" 2>/dev/null > "${matches_file}" || true

present() { grep -qiE "$1" "${matches_file}" 2>/dev/null && printf '  %s\n' "$2" || true; }

echo "--- Module tags ---"
present '\[HCCL\]|HCCL' HCCL
present '\[HCCP\]|HCCP' HCCP
present 'DRV' DRV
present 'RUNTIME' RUNTIME
present 'ADXL' ADXL
present '\[HIXL\]' HIXL
present 'HIXL CS|HixlCS|HixlClient|HixlServer' 'HIXL CS'
present 'AscendDirect' AscendDirect
echo

echo "--- Suggested Wiki pages ---"
present '\[HixlClient\]|\[HixlServer\]|HixlCSClient|HixlCSServer' "HIXLCS性能分析.md (Wiki 参考；代码用 [HixlClient]/[HixlServer] 日志定位)"
present '\[FabricMemEngine\]|Fabric mem transfer statistic|halMemImport|suspect remote' "FabricMem模式介绍.md"
present 'Fabric mem transfer statistic' "性能统计日志解读.md (FabricMem 章节)"
present 'HcclCommPrepare|LINK_ERROR_INFO|CheckMemCpyAttr|remoteBuffer|halMemImport|suspect remote|cpu stuck' "HIXL常见问题定位手册.md"
present 'Connect statistic|Direct transfer statistic' "性能统计日志解读.md (ADXL/HCCL 章节)"
present 'Mooncake|AscendDirect|MC_LOG' "Mooncake + HIXL 快速上手指南.md (Mooncake 日志仅作时间线参考)"
present 'local_comm_res' "LOCALCOMMRES自动生成工具问题排查.md"
echo

echo "--- Suggested code repos (${deps_dir}) ---"
echo "  hixl (current workspace)"
present '\[HixlClient\]|\[HixlServer\]|HixlCSClient|HixlCSServer' "hixl/src/hixl/cs/ (HIXL_CS engine)"
present '\[FabricMemEngine\]|Fabric mem transfer statistic' "hixl/src/hixl/fabric_mem/ (FabricMem engine)"
present 'HCCL|HcclCommPrepare|LINK_ERROR_INFO|remoteBuffer' "${deps_dir}/hcomm"
present 'CheckMemCpyAttr|RtStreamSynchronize|RUNTIME' "${deps_dir}/runtime"
present 'DRV|devmm|halShmem|HCCP' "${deps_dir}/driver"
echo

echo "--- Suggested deps.sh ---"
echo "  bash .agents/skills/hixl-troubleshoot/scripts/deps.sh"
echo "--- Suggested source alignment ---"
echo "  bash .agents/skills/hixl-troubleshoot/scripts/source-align.sh --before <YYYY-MM-DD>"
