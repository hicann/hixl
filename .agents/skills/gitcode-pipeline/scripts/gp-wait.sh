#!/usr/bin/env bash
# gp-wait.sh - 循环轮询流水线状态直到完成
#
# 用途: 自动每隔 60 秒查询一次流水线状态，直到状态变为 success/failed/canceled
# 适用: 触发 CI 后等待完成，避免手动重复调用 gp-list.sh
#
# 入参:
#   $1  PR 编号 (必填)
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#   GP_OWNER           (可选，默认从 git remote 自动检测)
#   GP_REPO            (可选，默认从 git remote 自动检测)
#
# 返回值 (stdout):
#   运行中: 每 60 秒输出一行状态摘要
#   完成时: 输出最终状态并退出 (exit 0 for success, exit 1 for failed/canceled)
#
# 示例:
#   $ ./gp-wait.sh 2479
#   [2026-05-27 10:05:00] status=running sha=794f3dad8c5e elapsed=0m
#   [2026-05-27 10:06:00] status=running sha=794f3dad8c5e elapsed=1m
#   [2026-05-27 10:07:00] status=success sha=794f3dad8c5e elapsed=2m
#   ✅ Pipeline completed: success
#
# 注意: 调用此脚本时必须设置足够长的超时时间（建议 60 分钟 / 3600000 ms）

set -euo pipefail

if [ -z "${1:-}" ]; then
  echo "用法: gp-wait.sh <PR_NUMBER>" >&2
  exit 1
fi

PR_NUMBER="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
START_TIME=$(date +%s)
ITERATION=0

echo "开始轮询 PR #${PR_NUMBER} 流水线状态（每 60 秒检查一次）..."

while true; do
  ITERATION=$((ITERATION + 1))
  CURRENT_TIME=$(date +%s)
  ELAPSED_MINUTES=$(( (CURRENT_TIME - START_TIME) / 60 ))
  TIMESTAMP=$(date '+%Y-%m-%d %H:%M:%S')

  # 调用 gp-list.sh 获取当前状态
  RESULT=$(bash "${SCRIPT_DIR}/gp-list.sh" "${PR_NUMBER}")

  if [ -z "${RESULT}" ]; then
    echo "[${TIMESTAMP}] 未找到流水线记录，等待 60 秒后重试..."
    sleep 60
    continue
  fi

  # 提取状态
  STATUS=$(echo "${RESULT}" | grep -oP 'status=\K[^ ]+')
  SHA=$(echo "${RESULT}" | grep -oP 'sha=\K[^ ]+')

  if [ "${STATUS}" = "running" ]; then
    echo "[${TIMESTAMP}] status=running sha=${SHA} elapsed=${ELAPSED_MINUTES}m (第 ${ITERATION} 次检查)"
    sleep 60
  else
    # 状态不再是 running，立即退出
    echo "[${TIMESTAMP}] status=${STATUS} sha=${SHA} elapsed=${ELAPSED_MINUTES}m"
    if [ "${STATUS}" = "success" ]; then
      echo "✅ 流水线完成: ${STATUS}"
      exit 0
    else
      echo "❌ 流水线结束: ${STATUS}"
      exit 1
    fi
  fi
done
