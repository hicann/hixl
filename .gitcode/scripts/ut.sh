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
sudo update-alternatives --set gcc /usr/bin/gcc-14
gcc --version
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

case "${ut_type}" in
    ut_test)
        bash tests/run_test.sh -t=cpp -c --cann_3rd_lib_path=/home/jenkins/opensource
        ret=$?
        ;;
    ut_test_python)
        bash tests/run_test.sh -t=py -c --cann_3rd_lib_path=/home/jenkins/opensource
        ret=$?
        ;;
    *)
        echo "Skip UT test execution for ${ut_type} on non-master branch"
        exit 0
        ;;
esac


DP_ASSERT_EQUAL "$ret" "0" "Run UT TESTCASE"

echo "ut_process=coverage" >> "${ATOMGIT_OUTPUT}"
