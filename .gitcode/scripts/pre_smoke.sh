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

echo "start run test case, please wait ..."
cd ${WORKSPACE}

export ASCEND_GLOBAL_LOG_LEVEL=1
export ASCEND_HOST_LOG_FILE_NUM=1000
export ASCEND_SLOG_PRINT_TO_STDOUT=0
source /usr/local/Ascend/cann/set_env.sh

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
  # run_example.sh is executed after cd ./examples, so its tee log is not ${WORKSPACE}/run_test.log.
  if [ -f "${WORKSPACE}/examples/run_test.log" ]; then
    cp -a "${WORKSPACE}/examples/run_test.log" "${stage}/examples_run_test.log"
    echo "packed ${WORKSPACE}/examples/run_test.log"
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

log "init test case, please wait ..."

ln -sf /usr/bin/python3.10 /opt/conda/bin/python3
python3 --version

source /usr/local/Ascend/cann/set_env.sh
wget -nv https://ascend-ci.obs.cn-north-4.myhuaweicloud.com/${obs_path}/cann-hixl_linux-aarch64_ubuntu24.run
wget -nv https://ascend-ci.obs.cn-north-4.myhuaweicloud.com/${obs_path}/examples_arm_ubuntu24.tar.gz
mkdir -p build
tar -zxf examples_arm_ubuntu24.tar.gz -C build
cd build
ls -l
cd examples
ls -l
pwd
cd ${WORKSPACE}
cp /usr/local/Ascend/cann/opp/built-in/op_impl/aicpu/kernel/cann-hixl-compat.tar.gz  /usr/local/
chmod +x ./*.run
bash ./*.run --install-path=/usr/local/Ascend --full --quiet --pylocal 2>&1 | tee -a ./run_test.log
rm -rf /usr/local/Ascend/cann/opp/built-in/op_impl/aicpu/kernel/cann-hixl-compat.tar.gz
mv /usr/local/cann-hixl-compat.tar.gz /usr/local/Ascend/cann/opp/built-in/op_impl/aicpu/kernel/
export PATH=$PATH:/usr/local/Ascend/driver/tools
source /usr/local/Ascend/cann/set_env.sh && cd ./examples && bash run_example.sh 0 1  2>&1 | tee -a ./run_test.log


# ==============================
# 检查测试结果。日志在 EXIT 时打包，失败也会生成 slog.tar.gz。
# ==============================
log "checking test results ..."

date_time=`date +%Y%m%d`"."`date +%H%M%S`
if grep -w -e "execute samples success" "./run_test.log"; then
  echo "$date_time : run test case success"
else
  echo "$date_time : run test case failed"
  exit 1
fi
