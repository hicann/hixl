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

# gp-trigger.sh - 触发 PR 流水线
#
# 用途: 通过发送 compile 评论触发 PR 的 CI 流水线
# 适用: 无流水线或需要重新触发时
#
# 入参:
#   $1  PR 编号 (必填)
#
# 评论内容固定为 "compile"，不接受自定义
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout, 纯文本):
#   ok 如果评论发送成功
#   error:<错误信息> 如果失败
#
# 示例:
#   $ ./gp-trigger.sh 2479
#   ok

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-trigger.sh <PR_NUMBER>" >&2
  exit 1
fi

if [ -n "${2:-}" ]; then
  echo "错误: 不接受额外参数，评论内容固定为 compile" >&2
  exit 1
fi

PR_NUMBER="$1"
COMMENT="compile"
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

RESPONSE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
  -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pulls/${PR_NUMBER}/comments" \
  -d "{\"body\": \"${COMMENT}\"}")

if [ "$RESPONSE" -ge 200 ] && [ "$RESPONSE" -lt 300 ]; then
  echo "ok"
else
  echo "error: HTTP $RESPONSE"
fi
