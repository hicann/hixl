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

# gp-log-full.sh - 下载 Job 的全量日志
#
# 原理: 使用 raw.gitcode.com 的 download-log 接口下载全量文本日志
#
# 用法: gp-log-full.sh <pipeline_id> <pipeline_run_id> <job_id> <pipeline_detail>
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 输出:
#   全量日志保存到 pipeline_logs/<job_id>_full.log
#   stdout 输出文件路径和总大小

set -euo pipefail

if [ -z "${1:-}" ] || [ -z "${2:-}" ] || [ -z "${3:-}" ] || [ -z "${4:-}" ]; then
  echo "用法: gp-log-full.sh <pipeline_id> <pipeline_run_id> <job_id> <pipeline_detail>" >&2
  exit 1
fi

PIPELINE_ID="$1"
PIPELINE_RUN_ID="$2"
JOB_ID="$3"
PIPELINE_DETAIL="$4"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"
REGION="${GP_REGION:-cn-north-4}"
LOG_LEVEL="${GP_LOG_LEVEL:-}"
COMPRESS="${GP_LOG_COMPRESS:-}"

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

mkdir -p pipeline_logs
OUTPUT_FILE="pipeline_logs/${JOB_ID}_full.log"
RESPONSE_FILE="pipeline_logs/${JOB_ID}_full.response"
rm -f "$OUTPUT_FILE" "$RESPONSE_FILE"

DETAIL_RESPONSE=$(curl -s --request POST \
  "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pipelines/${PIPELINE_ID}/pipeline-runs/detail?access_token=${TOKEN}" \
  --header 'Content-Type: application/json' \
  --data-raw "$(jq -n --arg run_id "$PIPELINE_RUN_ID" --arg detail "$PIPELINE_DETAIL" '{pipeline_run_id: $run_id, pipeline_detail: $detail}')")

STEP_ID=$(echo "$DETAIL_RESPONSE" | jq -r --arg job_id "$JOB_ID" '
  .stages[]?.jobs[]? | select(.id == $job_id or .job_run_id == $job_id) | .steps[0].id // empty
' | head -n 1)

RECORD_ID="$JOB_ID"
if [ -n "$STEP_ID" ]; then
  STEP_OUTPUT=$(curl -s --request POST \
    "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pipelines/${PIPELINE_ID}/pipeline-runs/${PIPELINE_RUN_ID}/steps/gitcode/outputs?access_token=${TOKEN}" \
    --header 'Content-Type: application/json' \
    --data-raw "$(jq -n --arg detail "$PIPELINE_DETAIL" --arg step_id "$STEP_ID" '{pipeline_detail: $detail, step_run_ids: $step_id}')")
  RESOLVED_RECORD_ID=$(echo "$STEP_OUTPUT" | jq -r '.step_outputs[0].output_result[]? | select(.key == "recordId") | .value' | head -n 1)
  if [ -n "$RESOLVED_RECORD_ID" ]; then
    RECORD_ID="$RESOLVED_RECORD_ID"
  fi
fi

DOWNLOAD_URL="https://raw.gitcode.com/${GP_OWNER}/${GP_REPO}/pipeline/${PIPELINE_ID}/${PIPELINE_RUN_ID}/build-log/${RECORD_ID}/download-log?region=${REGION}&log_level=${LOG_LEVEL}&compress=${COMPRESS}"

curl -sS -L -o "$RESPONSE_FILE" \
  --header "Authorization: ${TOKEN}" \
  "$DOWNLOAD_URL"

if [ ! -s "$RESPONSE_FILE" ]; then
  echo "日志下载失败或文件为空: ${DOWNLOAD_URL}" >&2
  exit 1
fi

FILE_TYPE=$(file -b "$RESPONSE_FILE")

if echo "$FILE_TYPE" | grep -q 'JSON'; then
  ERROR_MSG=$(jq -r '.error_msg // .message // empty' "$RESPONSE_FILE" 2>/dev/null || true)
  echo "download-log 接口返回错误: ${ERROR_MSG}" >&2
  echo "download_url=${DOWNLOAD_URL}" >&2
  echo "job_id=${JOB_ID} step_id=${STEP_ID:-} record_id=${RECORD_ID}" >&2
  echo "响应已保存到: ${RESPONSE_FILE}" >&2
  exit 1
fi

mv "$RESPONSE_FILE" "$OUTPUT_FILE"

FILE_SIZE=$(wc -c < "$OUTPUT_FILE")
echo "日志已保存到: ${OUTPUT_FILE} (${FILE_SIZE} bytes)"
