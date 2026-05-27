#!/usr/bin/env bash
# gp-retry-api.sh - 通过 API 重试指定流水线
#
# 用途: 使用 retry API 重跑指定流水线，不产生新 pipeline 记录
# 适用: 偶发环境失败（API 限流、codecheck sync failed 等）需要重跑时
#       区别于 gp-trigger.sh（compile 评论触发全新流水线）
#
# 入参:
#   $1  PR 编号 (必填)
#   $2  流水线 content id (必填，即 gp-list.sh 输出中的 id 字段)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout, 纯文本):
#   ok      如果重试请求成功
#   error:<错误信息> 如果失败
#
# 示例:
#   $ ./gp-retry-api.sh 2503 529310
#   ok

set -euo pipefail

if [ -z "${1:-}" ] || [ -z "${2:-}" ]; then
  echo "用法: gp-retry-api.sh <PR_NUMBER> <PIPELINE_CONTENT_ID>" >&2
  exit 1
fi

PR_NUMBER="$1"
CONTENT_ID="$2"
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

ENCODED_REPO=$(printf '%s' "${GP_OWNER}/${GP_REPO}" | jq -sRr @uri)

BODY=$(jq -n --argjson id "$CONTENT_ID" '{"pipeline_detail": ("{\"id\":" + ($id | tostring) + "}")}')

RESPONSE=$(curl -s -o /tmp/gp-retry-resp.json -w "%{http_code}" -X POST \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/merge_requests/${PR_NUMBER}/pipelines/retry?access_token=${TOKEN}" \
  -d "$BODY")

if [ "$RESPONSE" -ge 200 ] && [ "$RESPONSE" -lt 300 ]; then
  echo "ok"
else
  echo "error: HTTP $RESPONSE $(cat /tmp/gp-retry-resp.json | head -c 200)"
fi
