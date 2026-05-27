#!/usr/bin/env bash
# gp-log.sh - 查询失败 Job 的日志（末尾错误摘要）
#
# 用途: 获取指定 Job 的日志末尾，并用 grep 提取 error/fail/fatal 相关行
# 适用: 定位编译错误、测试失败的具体原因
#
# 入参:
#   $1  pipeline_id (当前流水线的 pipeline_id，子流水线场景下用子流水线的)
#   $2  pipeline_run_id (当前流水线的 pipeline_run_id)
#   $3  job_id (失败 Job 的 id)
#   $4  pipeline_detail (完整 JSON 字符串)
#
# 可选入参:
#   $5  lines 提取行数，默认 20，即返回最后 20 行包含 error/fail/fatal 的日志
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout, 纯文本):
#   包含 error/fail/fatal 的日志行，最多返回 $5 行
#   如果未找到错误行，返回日志最后 50 行
#
# 示例:
#   $ ./gp-log.sh dcd16185... c3d9c366... b8d3230d... '{"hook_id":"42205",...}' 20
#   [2026/04/30 14:39:05] file_constant_kernel.cc:41:15: error: expected ';' at end of member declaration
#   [2026/04/30 14:39:05] make[2]: *** [...] Error 1
#   [2026/04/30 14:39:05] Failed command: make all -j8

set -euo pipefail

if [ -z "${1:-}" ] || [ -z "${2:-}" ] || [ -z "${3:-}" ] || [ -z "${4:-}" ]; then
  echo "用法: gp-log.sh <pipeline_id> <pipeline_run_id> <job_id> <pipeline_detail> [lines]" >&2
  exit 1
fi

PIPELINE_ID="$1"
PIPELINE_RUN_ID="$2"
JOB_ID="$3"
PIPELINE_DETAIL="$4"
MAX_LINES="${5:-20}"
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

BODY=$(jq -n --arg detail "$PIPELINE_DETAIL" \
  '{pipeline_detail: $detail, start_offset: "0", end_offset: "0", limit: 500, sort: "desc"}')

LOG_JSON=$(curl -s --request POST \
  "https://api.gitcode.com/api/v5/repos/${GP_OWNER}/${GP_REPO}/pipelines/${PIPELINE_ID}/pipeline-runs/${PIPELINE_RUN_ID}/jobs/${JOB_ID}/logs?access_token=${TOKEN}" \
  --header 'Content-Type: application/json' \
  --data-raw "$BODY")

echo "$LOG_JSON" | jq -r '.log' | grep -iE 'error|fatal|failed|FAIL' | tail -n "$MAX_LINES" || \
  echo "$LOG_JSON" | jq -r '.log' | tail -n 50
