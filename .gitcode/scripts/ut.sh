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
set -ex
echo $(grep -E "^VERSION_ID=" /etc/os-release | cut -d'"' -f2)
if [[ "${target_branch}" == "master" ]]; then
    sudo update-alternatives --set gcc /usr/bin/gcc-15
else
    sudo update-alternatives --set gcc /usr/bin/gcc-14
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

source /home/jenkins/Ascend/cann/bin/setenv.bash
set +e
ls -l

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

changed_files_args=()
if [ -f pr_filelist_mod.txt ]; then
    echo "[INFO] Use PR file list: pr_filelist_mod.txt"
    changed_files_args=(-f pr_filelist_mod.txt)
elif [ -f pr_filelist.txt ]; then
    echo "[INFO] Use PR file list: pr_filelist.txt"
    changed_files_args=(-f pr_filelist.txt)
else
    echo "[INFO] No PR file list found, run full UT"
fi

case "${ut_type}" in
    ut_test)
        bash tests/run_test.sh -t=cpp -c --cann_3rd_lib_path=/home/jenkins/opensource "${changed_files_args[@]}"
        ret=$?
        ;;
    ut_test_python)
        bash tests/run_test.sh -t=py -c --cann_3rd_lib_path=/home/jenkins/opensource "${changed_files_args[@]}"
        ret=$?
        ;;
    *)
        echo "Skip UT test execution for ${ut_type} on non-master branch"
        exit 0
        ;;
esac

# run_test.sh exits 200 when the PR only changes documentation.
if [ "${ret}" = "200" ]; then
    echo "[INFO] Doc-only PR change detected, skip UT and coverage."
    echo "ut_process=skip" >> "${ATOMGIT_OUTPUT}"
    exit 0
fi

DP_ASSERT_EQUAL "$ret" "0" "Run UT TESTCASE"

echo "ut_process=coverage" >> "${ATOMGIT_OUTPUT}"
