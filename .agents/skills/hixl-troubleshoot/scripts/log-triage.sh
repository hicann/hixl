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

log_dir="${1:-${HOME}/ascend/log}"
context_lines=3
deps_dir="${HOME}/hixl-troubleshooting"

if [ ! -d "${log_dir}" ]; then
  echo "Log directory not found: ${log_dir}" >&2
  exit 1
fi

echo "=== HIXL log triage: ${log_dir} ==="
echo

matches_file="$(mktemp)"
trap 'rm -f "${matches_file}"' EXIT
grep -rniE 'HCCL|HCCP|DRV|RUNTIME|ADXL|\[HIXL\]|HIXL CS|HixlClient|HixlServer|HixlCS|AscendDirect|HcclCommPrepare|LINK_ERROR_INFO|CheckMemCpyAttr|remoteBuffer|FabricMem|\[FabricMemEngine\]|Fabric mem transfer|Connect statistic|Direct transfer statistic|local_comm_res|halMemImport|suspect remote|cpu stuck' \
  "${log_dir}" 2>/dev/null > "${matches_file}" || true

first_error="$(grep -rniE "\[ERROR\]" "${log_dir}" 2>/dev/null \
  | grep -vi mooncake \
  | sort -t: -k1,1 -k2,2n | head -1 || true)"

if [ -z "${first_error}" ]; then
  first_error="$(grep -rni "\[ERROR\]" "${log_dir}" 2>/dev/null | sort -t: -k1,1 -k2,2n | head -1 || true)"
fi

if [ -z "${first_error}" ]; then
  echo "No [ERROR] lines found under ${log_dir}"
  exit 0
fi

echo "--- First [ERROR] (HIXL/plog preferred) ---"
echo "${first_error}"
echo

error_file="${first_error%%:*}"
error_line="${first_error#*:}"
error_line="${error_line%%:*}"
start=$((error_line - context_lines))
[ "${start}" -lt 1 ] && start=1
end=$((error_line + context_lines))

echo "--- Context (lines ${start}-${end} of ${error_file}) ---"
sed -n "${start},${end}p" "${error_file}" 2>/dev/null || true
echo

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

echo "--- Mooncake timeline (reference only, not code source) ---"
grep -rniE "transfer_task\.cpp|TransferExecutor register mem|Failed to complete transfers|Sync batch data transfer timeout" "${log_dir}" 2>/dev/null \
  | head -10 || echo "(no Mooncake timeline lines)"
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
