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
cd /home/taskspace
WORKSPACE=/home/taskspace

export ASCEND_GLOBAL_LOG_LEVEL=2
export ASCEND_SLOG_PRINT_TO_STDOUT=0
source /usr/local/Ascend/cann/set_env.sh

log() {
  local dt
  dt=$(date '+%Y%m%d.%H%M%S')
  echo "===================================================================="
  echo "$dt : $*"
  echo "===================================================================="
}

log "init test case, please wait ..."


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
# 打包log
# ==============================
mkdir -p /root/ascend
slog_name="slog.tar.gz"
tar -zcf "${slog_name}" -C /root/ascend log

# upload plog
if python3 /home/upload.py --bucket-name "ascend-ci" --action upload  --local-file "slog.tar.gz" --obs-object-key "${obs_smoke_path}/${slog_name}"; then
  echo "::set-output var=plog_url:https://ascend-ci.obs.cn-north-4.myhuaweicloud.com/${obs_smoke_path}/slog.tar.gz"
fi

# ==============================
# 检查测试结果
# ==============================
log "checking test results ..."

date_time=`date +%Y%m%d`"."`date +%H%M%S`
if grep -w -e "execute samples success" "./run_test.log"; then
  echo "$date_time : run test case success"
else
  echo "$date_time : run test case failed"
  exit 1
fi
