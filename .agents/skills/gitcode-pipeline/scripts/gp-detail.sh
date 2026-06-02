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

# gp-detail.sh - 查询流水线详情（阶段和 Job 列表）
#
# 用途: 查询流水线各阶段/Job的状态，仅提取关键信息，过滤掉冗长的 inputs/env 等字段
# 适用: 流水线失败后定位哪些 Job 失败
#
# 入参:
#   $1  pipeline_id (长字符串，如 c85338dd05464b82b0a098dacc32415d)
#   $2  pipeline_run_id (长字符串，如 979a91bed71a49378aac5cb0e1a1f87d)
#   $3  pipeline_detail (完整 JSON 字符串)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout, 纯文本):
#   第一行: pipeline_name=<名称> status=<状态>
#   后续行: 每个阶段一行，每个失败/跳过的 Job 一行
#
# 格式:
#   pipeline_name=cann_ge_all status=FAILED
#   [stage] 获取pr文件: COMPLETED
#   [job]   执行shell: COMPLETED task=official_shell_plugin
#   [stage] 子流水线: FAILED
#   [job]   static-check: COMPLETED task=official_devcloud_subPipeline
#   [job]   compile: FAILED task=official_devcloud_subPipeline step_id=20357724a95b48aa98b5f3934c833cfe
#   [job]   llt: FAILED task=official_devcloud_subPipeline step_id=0aa38cc75b8246f9907f7d7b69dd7ca1
#
# 示例:
#   $ ./gp-detail.sh c85338dd... 979a91be... '{"hook_id":"42205",...}'

set -euo pipefail

if [ -z "${1:-}" ] || [ -z "${2:-}" ] || [ -z "${3:-}" ]; then
  echo "用法: gp-detail.sh <pipeline_id> <pipeline_run_id> <pipeline_detail>" >&2
  exit 1
fi

PIPELINE_ID="$1"
PIPELINE_RUN_ID="$2"
PIPELINE_DETAIL="$3"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"

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

BODY=$(jq -n --arg run_id "$PIPELINE_RUN_ID" --arg detail "$PIPELINE_DETAIL" \
  '{pipeline_run_id: $run_id, pipeline_detail: $detail}')

curl -s --request POST \
  "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pipelines/${PIPELINE_ID}/pipeline-runs/detail?access_token=${TOKEN}" \
  --header 'Content-Type: application/json' \
  --data-raw "$BODY" \
  | jq -r '
    "pipeline_name=\(.name) status=\(.status)",
    (.stages[] | "[stage] \(.name): \(.status)"),
    (.stages[] | .jobs[] |
      "[job]   \(.name): \(.status) id=\(.id) task=\(.steps[0].task)" +
      (if .steps[0].task == "official_devcloud_subPipeline" then " step_id=\(.steps[0].id)" else "" end) +
      (if .message then " msg=\(.message[0:120])" else "" end)
    )
  '
