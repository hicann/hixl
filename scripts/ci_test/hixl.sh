#!/bin/bash
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set -e

if [ -z "$ASCEND_HOME_PATH" ];then
    echo "Error:CANN environment variables are not set."
    exit 1
fi
CANN_INSTALL_PATH=$(dirname "$ASCEND_HOME_PATH")
BASEPATH=$(cd "$(dirname $0)"; pwd)
HIXLPATH=$(dirname $(dirname "$BASEPATH"))

# 显示使用帮助
usage() {
    echo "Usage:"
    echo "  sh hixl.sh [<device_id_1> <device_id_2>]            Build, test, install hixl and run samples with default or specified devices"
    echo "  sh hixl.sh hixl_build [--Build options]             Build code"
    echo "  sh hixl.sh hixl_test [--Test options]               Test"
    echo "  sh hixl.sh hixl_install                             Install hixl"
    echo "  sh hixl.sh hixl_samples [<device_id_1> <device_id_2>]"
    echo "                                                      Run samples"
    echo "  sh hixl.sh hixl_build [--Build options]  hixl_samples [<device_id_1> <device_id_2>]"
    echo "                                                      Run multiple stages together"
    echo "  sh hixl.sh [-h | --help]                            Print usage"
    echo ""
}

# 执行构建
build_hixl() {
    local build_args="$@"
    if [ -d "${HIXLPATH}/build" ] || [ -d "${HIXLPATH}/build_out" ]; then
        echo "Found existing build directories. Cleaning..."
        rm -rf "${HIXLPATH}/build" "${HIXLPATH}/build_out"
        echo "Cleaned: build and build_out"
    fi

    if ! bash "${HIXLPATH}/build.sh" $build_args; then
        exit 1
    fi
}

# 执行UTST
test_hixl() {
    local test_args="$@"
    local has_help=false
    for arg in "$@"; do
        if [ "$arg" == "-h" ] || [ "$arg" == "--help" ]; then
            has_help=true
            break
        fi
    done
    if [ "$has_help" = false ]; then
        if ! pip3 install -r "${HIXLPATH}/requirements.txt"; then
            echo "pip install failed."
            exit 1
        fi
    fi

    if [ -d "${HIXLPATH}/build_test" ]; then
        echo "Found existing build_test directories. Cleaning..."
        rm -rf "${HIXLPATH}/build_test"
        echo "Cleaned: build_test"
    fi

    if ! bash "${HIXLPATH}/tests/run_test.sh" $test_args; then
        exit 1
    fi
}

# 安装hixl
install_hixl() {
    local run_file="${HIXLPATH}/build_out/cann*.run"
    local files=($run_file)
    run_file="${files[0]}"

    "${run_file}" \
    --full \
    --quiet \
    --pylocal \
    --install-path="${CANN_INSTALL_PATH}"
    if [[ $? -eq 0 ]]; then
        echo "Install success."
    else
        echo "Intall failed."
        exit 1
    fi
}

# 执行samples脚本
run_samples() {
    local samples_args="$@"
    # 执行samples.sh脚本并传递参数
    if ! bash "${HIXLPATH}/examples/run_example.sh" -a $samples_args; then
        exit 1
    fi
}

main() {
    local current_phase=""
    local build_args=()
    local test_args=()
    local install_args=()
    local samples_args=()
    local all_args=()
    local build_executed=false
    local test_executed=false
    local install_executed=false
    local samples_executed=false
    
    if [ $# -eq 0 ]; then
        build_hixl --examples
        test_hixl
        install_hixl
        echo "Running samples.sh with default parameters"
        run_samples 0 2
        exit 0
    fi

    if [ $# -eq 1 ] && [[ "$1" == "-h" || "$1" == "--help" ]]; then
        usage
        echo "Build options:"
        bash "${HIXLPATH}/build.sh" -h | awk '/Options:/{flag=1; next} flag'
        echo "Test options:"
        bash "${HIXLPATH}/tests/run_test.sh" -h | awk '/Options:/{flag=1; next} flag'
        exit 0
    fi
    
    while [ $# -gt 0 ]; do
        case "$1" in
            hixl_build)
                # 标记hixl_build阶段将被执行
                build_executed=true
                current_phase="hixl_build"
                ;;
            hixl_test)
                # 标记hixl_test阶段将被执行
                test_executed=true
                current_phase="hixl_test"
                ;;
            hixl_install)
                # 标记hixl_install阶段将被执行
                install_executed=true
                current_phase="hixl_install"
                ;;
            hixl_samples)
                # 标记hixl_samples阶段将被执行
                samples_executed=true
                current_phase="hixl_samples"
                ;;
            *)
                # 根据当前阶段将参数添加到对应集合
                case "$current_phase" in
                    hixl_build)
                        # 为build.sh添加参数
                        build_args+=("$1")
                        ;;
                    hixl_test)
                        # 为run_test.sh添加参数
                        test_args+=("$1")
                        ;;
                    hixl_install)
                        # 为build.sh添加参数
                        install_args+=("$1")
                        ;;
                    hixl_samples)
                        # 为samples.sh添加参数
                        samples_args+=("$1")
                        ;;
                    *)
                        all_args+=("$1")
                        ;;
                esac
                ;;
        esac
        shift
    done
    
    # 按顺序执行各阶段命令
    if $build_executed; then
        build_hixl "${build_args[@]}"
    fi

    if $test_executed; then
        test_hixl "${test_args[@]}"
    fi

    if $install_executed; then
        install_hixl "${install_args[@]}"
    fi
    
    if $samples_executed; then
        if [ ${#samples_args[@]} -gt 0 ]; then
            echo "Running run_example.sh with parameters: ${samples_args[*]}"
            run_samples "${samples_args[@]}"
        else
            echo "Running run_example.sh with default parameters"
            run_samples 0 2
        fi
    fi
    
    if ! $build_executed && ! $test_executed && ! $install_executed && ! $samples_executed; then
        build_hixl --examples
        test_hixl
        install_hixl
        if [ ${#all_args[@]} -gt 0 ]; then
            echo "Running run_example.sh with parameters: ${all_args[*]}"
            run_samples "${all_args[@]}"
        fi
    fi
}

main "$@"