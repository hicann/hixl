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

# gp-retry.sh - 重触发 CI 并自动轮询直到完成
#
# 用途: 先尝试 API retry（重跑已有流水线），失败则评论触发全新流水线
# 适用: 一键重试 CI，无需关心触发方式
#
# 策略:
#   1. 优先调用 gp-api-retry.sh（API retry，不产生新记录）
#   2. 若 API retry 失败（无流水线记录/接口报错），降级为 gp-trigger.sh（评论触发）
#
# 入参:
#   $1  PR 编号
#   $2  触发评论内容 (可选，默认 "compile"，仅降级时使用)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#
# 示例:
#   $ ./gp-retry.sh 2503
#   Trying API retry for PR #2503...
#   => ok, polling...
#   $ ./gp-retry.sh 2503
#   Trying API retry for PR #2503...
#   => API retry failed, falling back to comment trigger...
#   Triggered via comment, polling...

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-retry.sh <PR_NUMBER> [comment]" >&2
  exit 1
fi

PR_NUMBER="$1"
COMMENT="${2:-compile}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
POLL_INTERVAL=60

echo "Trying API retry for PR #${PR_NUMBER}..."
if bash "${SCRIPT_DIR}/gp-api-retry.sh" "$PR_NUMBER"; then
  exit 0
fi

echo "=> API retry failed, falling back to comment trigger..."
echo "Triggering pipeline for PR #${PR_NUMBER} with comment: ${COMMENT}"
bash "${SCRIPT_DIR}/gp-trigger.sh" "$PR_NUMBER" "$COMMENT"

echo "Waiting 15s for pipeline to start..."
sleep 15

ELAPSED=0
while true; do
  OUTPUT=$(bash "${SCRIPT_DIR}/gp-list.sh" "$PR_NUMBER" | head -1)
  STATUS=$(echo "$OUTPUT" | grep -oP 'status=\K[^\s]+')
  LATEST_ID=$(echo "$OUTPUT" | grep -oP 'id=\K[^\s]+')
  SHA=$(echo "$OUTPUT" | grep -oP 'sha=\K[^\s]+')

  if [ "$STATUS" = "success" ]; then
    echo ""
    echo "=== Pipeline #${LATEST_ID} PASSED (SHA: ${SHA:0:12}, ${ELAPSED}s) ==="
    exit 0
  elif [ "$STATUS" = "failed" ]; then
    echo ""
    echo "=== Pipeline #${LATEST_ID} FAILED (SHA: ${SHA:0:12}, ${ELAPSED}s) ==="
    echo "Run gp-analyze-failure.sh ${PR_NUMBER} to analyze"
    exit 1
  elif [ "$STATUS" = "canceled" ]; then
    echo ""
    echo "=== Pipeline #${LATEST_ID} CANCELED (${ELAPSED}s) ==="
    exit 1
  fi

  ELAPSED=$((ELAPSED + POLL_INTERVAL))
  echo -n "... ${STATUS} (${ELAPSED}s)"
  sleep "$POLL_INTERVAL"
done
