#!/bin/bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

set -euo pipefail

: "${WORKSPACE:?WORKSPACE is not set}"

echo "start run A5 smoke test, please wait ..."
cd "${WORKSPACE}"

log() {
  local dt
  dt=$(date '+%Y%m%d.%H%M%S')
  echo "===================================================================="
  echo "$dt : $*"
  echo "===================================================================="
}

copy_log_dir() {
  local src="$1"
  local dest="$2"
  if [ ! -d "${src}" ]; then
    return 0
  fi
  mkdir -p "${dest}"
  cp -a "${src}/." "${dest}/"
  echo "packed logs from ${src}"
}

package_logs() {
  local stage="${WORKSPACE}/.slog_pack"
  rm -rf "${stage}"
  mkdir -p "${stage}"
  copy_log_dir "/root/ascend/log" "${stage}/root_ascend_log"
  if [ -n "${HOME:-}" ] && [ "${HOME}/ascend/log" != "/root/ascend/log" ]; then
    copy_log_dir "${HOME}/ascend/log" "${stage}/home_ascend_log"
  fi
  if [ -n "${ASCEND_PROCESS_LOG_PATH:-}" ] && [ "${ASCEND_PROCESS_LOG_PATH}" != "/root/ascend/log" ]; then
    copy_log_dir "${ASCEND_PROCESS_LOG_PATH}" "${stage}/process_log"
  fi
  if [ -f "${WORKSPACE}/run_test.log" ]; then
    cp -a "${WORKSPACE}/run_test.log" "${stage}/run_test.log"
    echo "packed ${WORKSPACE}/run_test.log"
  fi
  tar -zcf "${WORKSPACE}/slog.tar.gz" -C "${stage}" .
  rm -rf "${stage}"
  echo "packed slog archive: ${WORKSPACE}/slog.tar.gz"
}

on_exit() {
  local status=$?
  trap - EXIT
  set +e
  package_logs
  exit "${status}"
}
trap on_exit EXIT

export ASCEND_GLOBAL_LOG_LEVEL=1
export ASCEND_HOST_LOG_FILE_NUM=1000
export ASCEND_SLOG_PRINT_TO_STDOUT=0
source /usr/local/Ascend/cann/set_env.sh

log "init A5 smoke environment, please wait ..."

# ==============================
# 下载并安装 x86 编译产物
# ==============================
install_hixl_run() {
  local pkg="$1"
  wget -nv "https://ascend-ci.obs.cn-north-4.myhuaweicloud.com/${obs_path}/${pkg}"
  local compat_tar="/usr/local/Ascend/cann/opp/built-in/op_impl/aicpu/kernel/cann-hixl-compat.tar.gz"
  if [ -f "${compat_tar}" ]; then
    cp "${compat_tar}" /usr/local/
  fi
  chmod +x "./${pkg}"
  bash "./${pkg}" --install-path=/usr/local/Ascend --full --quiet --pylocal 2>&1 | tee -a ./run_test.log
  if [ -f /usr/local/cann-hixl-compat.tar.gz ]; then
    rm -rf "${compat_tar}"
    mv /usr/local/cann-hixl-compat.tar.gz "$(dirname "${compat_tar}")/"
  fi
  export PATH="${PATH}:/usr/local/Ascend/driver/tools"
  # shellcheck disable=SC1091
  source /usr/local/Ascend/cann/set_env.sh
}

python3 --version
install_hixl_run "cann-hixl_linux-x86_64_ubuntu24.run"

# ==============================
# 运行测试
# ==============================
# A5 冒烟用例暂未接入，先留空。接入时在此处执行用例并将输出 tee 到 ${WORKSPACE}/run_test.log，
# 成功标记须仅由用例输出产生，并在下方"检查测试结果"段按 pre_smoke.sh 的 if/else + exit 1 形式判定。
log "A5 smoke test cases not ready yet, skip running ..."

# ==============================
# 检查测试结果。日志在 EXIT 时打包，失败也会上传 slog.tar.gz。
# ==============================
log "checking test results ..."

date_time=`date +%Y%m%d`"."`date +%H%M%S`
# 用例暂未接入，本阶段无可判定的被测对象（set -e 已保证安装链路失败时非零退出）。
# 接入用例后，成功标记只由用例输出产生，并在此处判定，参考 pre_smoke.sh 的写法：
#   if grep -w -e "<用例成功标记>" "./run_test.log"; then
#     echo "$date_time : run test case success"
#   else
#     echo "$date_time : run test case failed"
#     exit 1
#   fi
echo "$date_time : A5 smoke framework run success (no test cases yet)"
