#!/bin/bash
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set -e

BASEPATH=$(cd "$(dirname $0)"; pwd)
device_id_1="$1"
device_id_2="$2"
flag=0

run_pair() {
    local cmd1="$1"
    local cmd2="$2"
    local has_error=0

    local binary_name1=$(echo "${cmd1#./}" | cut -d' ' -f1)
    local binary_name2=$(echo "${cmd2#./}" | cut -d' ' -f1)

    if [ ! -f "$binary_name1" ] || [ ! -f "$binary_name1" ]; then
        echo "Binary does not exist!"
        has_error=1
        flag=1
        exit 1
    fi

    tmp1=$(mktemp)
    tmp2=$(mktemp)

    echo "running smoke test: $cmd1 | $cmd2"

    eval "$cmd1" > "$tmp1" 2>&1 & 
    pid1=$!
    eval "$cmd2" > "$tmp2" 2>&1 & 
    pid2=$!

    wait "$pid1" "$pid2" || true

    cat "$tmp1"
    cat "$tmp2"

    if grep -qi "ERROR" "$tmp1" || grep -qi "ERROR" "$tmp2"; then
        has_error=1
    fi

    if [ "$flag" -eq "0" ] && [ "$has_error" -eq "1" ]; then
        flag=1
        rm -rf "$tmp1" "$tmp2"
        exit 1
    fi

    if [ "$has_error" -eq "0" ]; then
        echo "Execution finished"
    fi

    rm -rf "$tmp1" "$tmp2"
}

main() {
    # C++ examples
    cd "${BASEPATH}/../build/examples/cpp"
    run_pair "./prompt_pull_cache_and_blocks ${device_id_1} 127.0.0.1" "./decoder_pull_cache_and_blocks ${device_id_2} 127.0.0.1 127.0.0.1"
    run_pair "./prompt_push_cache_and_blocks ${device_id_1} 127.0.0.1 127.0.0.1" "./decoder_push_cache_and_blocks ${device_id_2} 127.0.0.1"
    run_pair "./prompt_switch_roles ${device_id_1} 127.0.0.1 127.0.0.1" "./decoder_switch_roles ${device_id_2} 127.0.0.1 127.0.0.1"
    run_pair "./client_server_h2d ${device_id_1} 127.0.0.1 127.0.0.1:16000" "./client_server_h2d ${device_id_2} 127.0.0.1:16000"
    run_pair "./server_server_d2d ${device_id_1} 127.0.0.1:16000 127.0.0.1:16001" "./server_server_d2d ${device_id_2} 127.0.0.1:16001 127.0.0.1:16000"

    
    if [ "$flag" -eq "0" ]; then
        echo "execute samples success"
    fi

    rm -rf 127.0.0.1:16000 127.0.0.1:16001 tmp
}

main "$@"