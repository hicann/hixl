#!/usr/bin/env bash
# gp-cov.sh - 获取覆盖率报告并解压
#
# 用法: gp-cov.sh <pipeline_id> <pipeline_run_id> <job_id> <pipeline_detail> [output_dir]
#
# 流程:
#   1. 调用 gp-log-full.sh 获取全量日志
#   2. 从日志中提取覆盖率报告链接 (https开头, .tar.gz结尾)
#   3. 下载压缩包
#   4. 解压到指定目录
#   5. 输出解压后的目录路径
#
# 环境变量:
#   GITCODE_API_TOKEN  (必填)
#
# 输出:
#   解压后的覆盖率报告目录路径

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "${1:-}" ] || [ -z "${2:-}" ] || [ -z "${3:-}" ] || [ -z "${4:-}" ]; then
  echo "用法: gp-cov.sh <pipeline_id> <pipeline_run_id> <job_id> <pipeline_detail> [output_dir]" >&2
  exit 1
fi

PIPELINE_ID="$1"
PIPELINE_RUN_ID="$2"
JOB_ID="$3"
PIPELINE_DETAIL="$4"
OUTPUT_DIR="${5:-pipeline_cov}"

echo "[Step 1] 获取全量日志..."
bash "${SCRIPT_DIR}/gp-log-full.sh" "$PIPELINE_ID" "$PIPELINE_RUN_ID" "$JOB_ID" "$PIPELINE_DETAIL"

LOG_FILE="pipeline_logs/${JOB_ID}_full.log"
if [ ! -f "$LOG_FILE" ]; then
  echo "日志文件不存在: $LOG_FILE" >&2
  exit 1
fi

echo "[Step 2] 提取覆盖率报告链接..."
COV_URL=$(grep "覆盖率报告链接" "$LOG_FILE" | grep -oP 'https://[^\s]+\.tar\.gz' | head -1)

if [ -z "$COV_URL" ]; then
  echo "未找到覆盖率报告链接" >&2
  exit 1
fi

echo "覆盖率报告链接: $COV_URL"

echo "[Step 3] 下载覆盖率报告..."
mkdir -p "$OUTPUT_DIR"
TAR_FILE="${OUTPUT_DIR}/${JOB_ID}_cov.tar.gz"

curl -s -o "$TAR_FILE" "$COV_URL"
if [ ! -f "$TAR_FILE" ] || [ "$(wc -c < "$TAR_FILE")" -eq 0 ]; then
  echo "下载失败: $COV_URL" >&2
  exit 1
fi

echo "下载完成: $TAR_FILE ($(wc -c < "$TAR_FILE") bytes)"

echo "[Step 4] 解压覆盖率报告..."
EXTRACT_DIR="${OUTPUT_DIR}/${JOB_ID}_cov"
mkdir -p "$EXTRACT_DIR"
tar -xzf "$TAR_FILE" -C "$EXTRACT_DIR" --strip-components=0 2>/dev/null || tar -xzf "$TAR_FILE" -C "$EXTRACT_DIR"

rm "$TAR_FILE"

echo "[Step 5] 列出解压后的目录..."
echo ""
echo "覆盖率报告目录: $EXTRACT_DIR"
ls -la "$EXTRACT_DIR"

echo ""
echo "提示: 覆盖率报告位于 $EXTRACT_DIR，agent可根据skill描述决定下一步操作"
echo "$EXTRACT_DIR"