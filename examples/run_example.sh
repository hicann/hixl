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

    if [ "$#" -eq "3" ]; then
        local binary_name1=$(echo "$cmd1" | grep -oP '(?<=\/)[^\/\s]+(?=\s|$)')
        local binary_name2=$(echo "$cmd2" | grep -oP '(?<=\/)[^\/\s]+(?=\s|$)')

        if [ ! -f "$binary_name1" ] || [ ! -f "$binary_name1" ]; then
            echo "Binary does not exist!"
            has_error=1
            flag=1
            exit 1
        fi
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
        if [ -e "$tmp1" ]; then
            rm -rf "$tmp1"
        fi
        if [ -e "$tmp2" ]; then
            rm -rf "$tmp2"
        fi
        exit 1
    fi

    if [ "$has_error" -eq "0" ]; then
        echo "Execution finished"
    fi

    if [ -e "$tmp1" ]; then
        rm -rf "$tmp1"
    fi
    if [ -e "$tmp2" ]; then
        rm -rf "$tmp2"
    fi
}

main() {
    # C++ examples
    cd "${BASEPATH}/../build/examples/cpp"
    run_pair "./prompt_pull_cache_and_blocks ${device_id_1} 127.0.0.1" "./decoder_pull_cache_and_blocks ${device_id_2} 127.0.0.1 127.0.0.1" "check"
    run_pair "./prompt_push_cache_and_blocks ${device_id_1} 127.0.0.1 127.0.0.1" "./decoder_push_cache_and_blocks ${device_id_2} 127.0.0.1" "check"
    run_pair "./prompt_switch_roles ${device_id_1} 127.0.0.1 127.0.0.1" "./decoder_switch_roles ${device_id_2} 127.0.0.1 127.0.0.1" "check"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d ${device_id_1} 127.0.0.1 127.0.0.1:16000" "HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d ${device_id_2} 127.0.0.1:16000" "check"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d ${device_id_1} 127.0.0.1:16000 127.0.0.1:16001" "HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d ${device_id_2} 127.0.0.1:16001 127.0.0.1:16000" "check"

    # Python examples
    cd "${BASEPATH}/../examples/python"
    run_pair "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 push_blocks_sample.py --device_id 0 --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 push_blocks_sample.py --device_id 1 --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 push_cache_sample.py --device_id 0 --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 push_cache_sample.py --device_id 1 --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 switch_role_sample.py --device_id 0 --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 switch_role_sample.py --device_id 1 --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port 127.0.0.1:16000" "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_xpyd_sample.py --device_id 2 --role d --local_ip_port 10.170.10.0:16001 --remote_ip_port 127.0.0.1:16000"
    run_pair "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python3 transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    
    if [ "$flag" -eq "0" ]; then
        echo "execute samples success"
    fi

    rm -rf 127.0.0.1:16000 127.0.0.1:16001 tmp
}

main "$@"