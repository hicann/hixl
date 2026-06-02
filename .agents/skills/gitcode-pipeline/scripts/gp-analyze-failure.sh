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

# gp-analyze-failure.sh - 一键分析流水线失败原因
#
# 用途: 自动穿透子流水线层级，获取所有失败Job的日志摘要
# 适用: 流水线失败后快速定位编译错误或测试失败
#
# 入参:
#   $1  PR 编号
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#   GP_ANALYZE_LOG_DIR (可选，日志保存目录，默认 pipeline_logs)
#
# 输出:
#   stdout: 失败摘要信息
#   文件: 日志保存到 ${GP_ANALYZE_LOG_DIR}/ 目录
#
# 示例:
#   $ ./gp-analyze-failure.sh 2503
#   === Pipeline #529022 FAILED ===
#   [FAILED JOB] UT_Test_ge_common (sub-pipeline: cann_ge_llt)
#     Log saved: pipeline_logs/pr2503_UT_Test_ge_common_20260502_005225.log
#     Summary: 92% tests passed, 1 tests failed out of 12
#     Failed: ut_libge_multiparts_utest

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-analyze-failure.sh <PR_NUMBER>" >&2
  exit 1
fi

PR_NUMBER="$1"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"
LOG_DIR="${GP_ANALYZE_LOG_DIR:-pipeline_logs}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ -z "${GP_OWNER:-}" ] || [ -z "${GP_REPO:-}" ]; then
  repo_url=$(git remote get-url origin 2>/dev/null)
  if [[ "$repo_url" == git@* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\2|')
  elif [[ "$repo_url" == *gitcode.com/* ]]; then
    GP_OWNER=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\1|')
    GP_REPO=$(echo "$repo_url" | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\2|')
  else
    echo "无法从 git remote 检测仓库信息，请设置 GP_OWNER 和 GP_REPO" >&2
    exit 1
  fi
fi

mkdir -p "$LOG_DIR"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)

get_failed_job_log() {
  local pid="$1"
  local prid="$2"
  local jid="$3"
  local jname="$4"
  local detail="$5"
  local logfile="${LOG_DIR}/pr${PR_NUMBER}_${jname}_${TIMESTAMP}.log"

  BODY=$(jq -n --arg detail "$detail" \
    '{pipeline_detail: $detail, start_offset: "0", end_offset: "0", limit: 500, sort: "desc"}')

  curl -s --request POST \
    "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pipelines/${pid}/pipeline-runs/${prid}/jobs/${jid}/logs?access_token=${TOKEN}" \
    --header 'Content-Type: application/json' \
    --data-raw "$BODY" | jq -r '.log' > "$logfile"

  LINES=$(wc -l < "$logfile")
  echo "    Log saved: $logfile ($LINES lines)"

  if [ "$LINES" -gt 1 ]; then
    echo "    --- Summary ---"
    grep -E "tests passed|tests failed|The following tests FAILED|exit code" "$logfile" 2>/dev/null | head -10 | while IFS= read -r line; do
      echo "    $line"
    done
    grep -iE "error:|fatal error:" "$logfile" 2>/dev/null | grep -v "ErrorNo" | head -5 | while IFS= read -r line; do
      echo "    $line"
    done
  fi
}

analyze_pipeline() {
  local pid="$1"
  local prid="$2"
  local detail="$3"
  local indent="${4:-}"

  local detail_output
  detail_output=$(bash "${SCRIPT_DIR}/gp-detail.sh" "$pid" "$prid" "$detail")

  local pipeline_status
  pipeline_status=$(echo "$detail_output" | head -1 | grep -oP 'status=\K\S+')

  if [ "$pipeline_status" != "FAILED" ] && [ "$pipeline_status" != "CANCELED" ]; then
    return 0
  fi

  echo "$detail_output" | grep "FAILED" | while IFS= read -r line; do
    local jid jname task step_id
    jname=$(echo "$line" | grep -oP '(?<=\[job\]\s+)[^:]+' | xargs)
    jid=$(echo "$line" | grep -oP 'id=\K[^\s]+')
    task=$(echo "$line" | grep -oP 'task=\K[^\s]+')
    step_id=$(echo "$line" | grep -oP 'step_id=\K[^\s]+' || true)

    if [ -z "$jid" ]; then
      continue
    fi

    if [ "$task" = "official_devcloud_subPipeline" ] && [ -n "$step_id" ]; then
      echo "${indent}[FAILED JOB] $jname (sub-pipeline)"
      local sub_output
      sub_output=$(bash "${SCRIPT_DIR}/gp-sub-output.sh" "$pid" "$prid" "$detail" "$step_id")
      local sub_pid sub_prid
      sub_pid=$(echo "$sub_output" | grep -oP 'sub_pipeline_id=\K[^\s]+')
      sub_prid=$(echo "$sub_output" | grep -oP 'sub_pipeline_run_id=\K[^\s]+')
      if [ -n "$sub_pid" ] && [ -n "$sub_prid" ]; then
        analyze_pipeline "$sub_pid" "$sub_prid" "$detail" "${indent}  "
      fi
    else
      echo "${indent}[FAILED JOB] $jname (direct)"
      get_failed_job_log "$pid" "$prid" "$jid" "$jname" "$detail"
    fi
  done
}

PIPELINES=$(bash "${SCRIPT_DIR}/gp-list.sh" "$PR_NUMBER")

LATEST_LINE=$(echo "$PIPELINES" | head -1)
LATEST_STATUS=$(echo "$LATEST_LINE" | grep -oP 'status=\K[^\s]+')
LATEST_ID=$(echo "$LATEST_LINE" | grep -oP 'id=\K[^\s]+')
PID=$(echo "$LATEST_LINE" | grep -oP 'pipeline_id=\K[^\s]+')
PRID=$(echo "$LATEST_LINE" | grep -oP 'pipeline_run_id=\K[^\s]+')
DETAIL=$(echo "$LATEST_LINE" | grep -oP 'pipeline_detail=\K.*')

echo "=== Pipeline #${LATEST_ID} ${LATEST_STATUS} ==="

if [ "$LATEST_STATUS" = "success" ]; then
  echo "Pipeline passed, nothing to analyze."
  exit 0
fi

analyze_pipeline "$PID" "$PRID" "$DETAIL"

echo ""
echo "=== Analysis complete. Logs in: $LOG_DIR/ ==="
