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

# gp-sub-output.sh - 查询子流水线步骤输出
#
# 用途: 对于 task=official_devcloud_subPipeline 的 Job，获取其子流水线的 pipeline_id 和 pipeline_run_id
# 适用: 定位子流水线后，获取子流水线标识以调用 gp-detail.sh
#
# 入参:
#   $1  pipeline_id (父流水线的 pipeline_id)
#   $2  pipeline_run_id (父流水线的 pipeline_run_id)
#   $3  pipeline_detail (完整 JSON 字符串)
#   $4  step_run_id (子流水线步骤的 step id)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout, 纯文本):
#   sub_pipeline_id=<子流水线pipeline_id> sub_pipeline_run_id=<子流水线run_id>
#
# 示例:
#   $ ./gp-sub-output.sh c85338dd... 979a91be... '{"hook_id":"42205",...}' 20357724a95b48aa98b5f3934c833cfe
#   sub_pipeline_id=dcd161850837402293f0c47cda6b9921 sub_pipeline_run_id=c3d9c3663189481ca8d812c3046a4d95

set -euo pipefail

if [ -z "${1:-}" ] || [ -z "${2:-}" ] || [ -z "${3:-}" ] || [ -z "${4:-}" ]; then
  echo "用法: gp-sub-output.sh <pipeline_id> <pipeline_run_id> <pipeline_detail> <step_run_id>" >&2
  exit 1
fi

PIPELINE_ID="$1"
PIPELINE_RUN_ID="$2"
PIPELINE_DETAIL="$3"
STEP_RUN_ID="$4"
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

BODY=$(jq -n --arg detail "$PIPELINE_DETAIL" --arg step_id "$STEP_RUN_ID" \
  '{pipeline_detail: $detail, step_run_ids: $step_id}')

curl -s --request POST \
  "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pipelines/${PIPELINE_ID}/pipeline-runs/${PIPELINE_RUN_ID}/steps/gitcode/outputs?access_token=${TOKEN}" \
  --header 'Content-Type: application/json' \
  --data-raw "$BODY" \
  | jq -r '
    (.step_outputs[0].output_result[]? | select(.key == "recordId") | "sub_record_id=\(.value)"),
    (.step_outputs[0].output_result[] as $item | "sub_\($item.key)=\($item.value)")
  ' | paste -sd' ' -
