#!/usr/bin/env bash
# gp-api-retry.sh - 通过 API 重试 PR 流水线并自动轮询直到完成
#
# 用途: 使用 /pipelines/retry API 重跑最新流水线，无需评论触发
# 适用: 偶发环境失败后的重试（如 codecheck sync failed）
#
# 入参:
#   $1  PR 编号 (必填)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 输出:
#   stdout: 最终流水线状态
#
# 示例:
#   $ ./gp-api-retry.sh 2503
#   Retrying latest pipeline #529308 for PR #2503...
#   Polling... running (60s) ... running (120s) ... SUCCESS
#   Pipeline #529310 passed!

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-api-retry.sh <PR_NUMBER>" >&2
  exit 1
fi

PR_NUMBER="$1"
TOKEN="${GITCODE_API_TOKEN:?GITCODE_API_TOKEN 未设置}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
POLL_INTERVAL=60

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

LATEST=$(bash "${SCRIPT_DIR}/gp-list.sh" "$PR_NUMBER" | head -1)
PIPELINE_CONTENT_ID=$(echo "$LATEST" | grep -oP 'id=\K[^\s]+')
STATUS=$(echo "$LATEST" | grep -oP 'status=\K[^\s]+')
PIPELINE_RUN_ID=$(echo "$LATEST" | grep -oP 'pipeline_run_id=\K[^\s]+')
PIPELINE_DETAIL=$(echo "$LATEST" | sed 's/.*pipeline_detail=//')

if [ -z "$PIPELINE_CONTENT_ID" ]; then
  echo "error: PR #${PR_NUMBER} 无流水线记录，请先触发流水线" >&2
  exit 1
fi

echo "Retrying latest pipeline #${PIPELINE_CONTENT_ID} (status: ${STATUS}) for PR #${PR_NUMBER}..."

BODY=$(jq -n --arg pd "$PIPELINE_DETAIL" --arg prid "$PIPELINE_RUN_ID" '{pipeline_detail: $pd, pipeline_run_id: $prid}')
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" -X POST \
  "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/merge_requests/${PR_NUMBER}/pipelines/retry?access_token=${TOKEN}" \
  -H "Content-Type: application/json" \
  -d "$BODY")

if [ "$HTTP_CODE" -lt 200 ] || [ "$HTTP_CODE" -ge 300 ]; then
  echo "error: retry API returned HTTP $HTTP_CODE" >&2
  exit 1
fi

echo "Retry triggered (HTTP $HTTP_CODE). Waiting 15s for pipeline to start..."
sleep 15

ELAPSED=0
while true; do
  OUTPUT=$(bash "${SCRIPT_DIR}/gp-list.sh" "$PR_NUMBER" | head -1)
  CURR_STATUS=$(echo "$OUTPUT" | grep -oP 'status=\K[^\s]+')
  LATEST_ID=$(echo "$OUTPUT" | grep -oP 'id=\K[^\s]+')
  SHA=$(echo "$OUTPUT" | grep -oP 'sha=\K[^\s]+')

  if [ "$CURR_STATUS" = "success" ]; then
    echo ""
    echo "=== Pipeline #${LATEST_ID} PASSED (SHA: ${SHA:0:12}, ${ELAPSED}s) ==="
    exit 0
  elif [ "$CURR_STATUS" = "failed" ]; then
    echo ""
    echo "=== Pipeline #${LATEST_ID} FAILED (SHA: ${SHA:0:12}, ${ELAPSED}s) ==="
    echo "Run gp-analyze-failure.sh ${PR_NUMBER} to analyze"
    exit 1
  elif [ "$CURR_STATUS" = "canceled" ]; then
    echo ""
    echo "=== Pipeline #${LATEST_ID} CANCELED (${ELAPSED}s) ==="
    exit 1
  fi

  ELAPSED=$((ELAPSED + POLL_INTERVAL))
  echo -n "... ${CURR_STATUS} (${ELAPSED}s)"
  sleep "$POLL_INTERVAL"
done
