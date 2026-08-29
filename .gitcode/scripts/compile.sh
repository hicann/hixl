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
set -e
DP_ASSERT_EQUAL()
{
    local actual="$1"
    local expected="$2"
    local msg="$3"
    if [ "${actual}" != "${expected}" ]; then
        echo "::error::ASSERT FAILED: ${msg} (expected=${expected}, actual=${actual})"
        exit 1
    fi
}

echo $(grep -E "^VERSION_ID=" /etc/os-release | cut -d'"' -f2)
if [[ "${task_name}" == *ubuntu24* ]]; then
    if [[ "${target_branch}" == "master" ]]; then
        sudo update-alternatives --set gcc /usr/bin/gcc-15
    else
        sudo update-alternatives --set gcc /usr/bin/gcc-14
    fi
else
    if [[ -f "/opt/rh/devtoolset-7/enable" ]]; then
        echo "source devtoolset"
        source /opt/rh/devtoolset-7/enable
    fi
fi

gcc --version
if gcc --version | head -n1 | grep -q "15."; then
    rm -rf /home/jenkins/opensource/lib_cache
    if [ -d /home/jenkins/opensource/gcc15 ]; then
        rm -rf /home/jenkins/opensource/gcc15/lib_cache/abseil-cpp
        rm -rf /home/jenkins/opensource/gcc15/lib_cache/device/abseil-cpp
        ln -s /home/jenkins/opensource/gcc15/lib_cache /home/jenkins/opensource/lib_cache
    elif [ -d /home/jenkins/opensource/gcc15x86 ]; then
        rm -rf /home/jenkins/opensource/gcc15x86/lib_cache/abseil-cpp
        rm -rf /home/jenkins/opensource/gcc15x86/lib_cache/device/abseil-cpp
        ln -s /home/jenkins/opensource/gcc15x86/lib_cache /home/jenkins/opensource/lib_cache
    fi
elif gcc --version | head -n1 | grep -q "14."; then
    :
else
    rm -rf /home/jenkins/opensource/lib_cache
    ln -s /home/jenkins/opensource/ubuntu20/lib_cache /home/jenkins/opensource/lib_cache
fi

if [[ "${task_name}" =~ Compile_Ascend_X86_ubuntu24 ]]; then
    sed -i "1i set(CMAKE_EXPORT_COMPILE_COMMANDS ON)" "CMakeLists.txt"
    echo "api-check=compile" >> "${ATOMGIT_OUTPUT}"
else
    echo "api-check=continue" >> "${ATOMGIT_OUTPUT}"
fi

gcc --version
source /home/jenkins/Ascend/cann/bin/setenv.bash
set +e

echo "exec cmd: [bash build.sh --examples --cann_3rd_lib_path="/home/jenkins/opensource"]"
bash build.sh --examples --cann_3rd_lib_path="/home/jenkins/opensource"

ret=$?

DP_ASSERT_EQUAL "$ret" "0" "build hixl ${task_name}"

cd build
tar -zcf examples.tar.gz examples
cp -rf examples.tar.gz ${WORKSPACE}/build_out

if [[ "${task_name}" == *X86* ]]; then
    if [[ "${task_name}" == *ubuntu24* ]]; then
        mv ${WORKSPACE}/build_out/examples.tar.gz  ${WORKSPACE}/build_out/examples_x86_ubuntu24.tar.gz
    else
        mv ${WORKSPACE}/build_out/examples.tar.gz  ${WORKSPACE}/build_out/examples_x86.tar.gz
    fi
fi

DP_ASSERT_EQUAL "$?" "0" "mv examples_x86 ${task_name}"

if [[ "${task_name}" == *ARM* ]]; then
    if [[ "${task_name}" == *ubuntu24* ]]; then
        mv ${WORKSPACE}/build_out/examples.tar.gz  ${WORKSPACE}/build_out/examples_arm_ubuntu24.tar.gz
    else
        mv ${WORKSPACE}/build_out/examples.tar.gz  ${WORKSPACE}/build_out/examples_arm.tar.gz
    fi
fi
DP_ASSERT_EQUAL "$?" "0" "mv examples_arm ${task_name}"
