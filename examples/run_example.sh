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

BASEPATH=$(cd "$(dirname $0)"; pwd)

validate_device_ids() {
    local args=("$@")
    local seen_ids=()

    for id in "${args[@]}"; do
        for seen_id in "${seen_ids[@]}"; do
            if [ "$id" -eq "$seen_id" ]; then
                echo "Error: Device IDs must be different."
                exit 1
            fi
        done

        seen_ids+=("$id")
    done
}

run_pair() {
    local -a cmds=("$@")
    local num_cmds=${#cmds[@]}
    local has_error=0
    local -a tmp_files=()
    local -a pids=()

    for((i=0; i<num_cmds; i++)); do
        cmd="${cmds[i]}"
        # 去掉环境变量
        clean_cmd=$(echo "$cmd" | sed 's/[^ ]*=[^ ]* *//g')
        first_word=$(echo "$clean_cmd" | awk '{print $1; exit}')
        first_word=$(echo "$first_word" | sed 's|^\./||')
        if [[ "$first_word" == "python3" || "$first_word" == "python3.9" ]]; then
            # 是否为python文件
            binary_name=$(echo "$clean_cmd" | awk '
            {
                for(i=1; i<=NF; i++) {
                    if($i ~ /\.py$/) {
                        print $i
                        exit
                    }
                }
            }')
        else
            binary_name="$first_word"
        fi
        if [ ! -f "$binary_name" ]; then
        echo "Binary does not exist!"
        has_error=1
        flag=1
        exit 1
    fi
    done

    echo "Running smoke test: "
    for((i=0; i<num_cmds; i++)); do
        tmp_file=$(mktemp)
        tmp_files+=("${tmp_file}")
        echo "${cmds[i]}"
    done
    set +e
    for((i=0; i<num_cmds; i++)); do
        cmd="${cmds[i]}"
        tmp="${tmp_files[i]}"
        eval "$cmd" > "$tmp" 2>&1 &
        pids+=($!)
    done
    wait "${pids[@]}"
    set -e

    for tmp in "${tmp_files[@]}"; do
        cat "$tmp"
    done

    for tmp in "${tmp_files[@]}"; do
        if grep -qi "ERROR" "$tmp"; then
            has_error=1
            break
        fi
    done

    if [ "$flag" -eq "0" ] && [ "$has_error" -eq "1" ]; then
        flag=1
        echo -e "Execution failed.\n"
        rm -rf "${tmp_files[@]}"
        exit 1
    fi

    if [ "$has_error" -eq "0" ]; then
        echo -e "Execution success.\n"
    fi

    rm -rf "${tmp_files[@]}"
}

all_samples() {
    # 若设置了 SOCKET_IFNAME 环境变量则使用环境变量中的网络接口名，否则默认使用 eth 或 enp 开头的网络接口名
    if [ -n "$SOCKET_IFNAME" ]; then
        NETWORK_INTERFACE_NAME="$SOCKET_IFNAME"
    else
        NETWORK_INTERFACE_NAME=$(ifconfig -a | awk '/^((eth|enp)[0-9a-zA-Z]+)[[:space:]:]/ {gsub(/:/,"",$1); print $1; exit}')
    fi
    if [ -z "$NETWORK_INTERFACE_NAME" ]; then
        echo "ERROR: Failed to get network interface name."
        echo "Please specify a valid interface using SOCKET_IFNAME environment variable"
        exit 1
    fi
    echo "NETWORK_INTERFACE_NAME: ${NETWORK_INTERFACE_NAME}"

    # 获取网络接口的 IP 地址
    IP_ADDRESS=$(ifconfig "$NETWORK_INTERFACE_NAME" | awk '/inet / {gsub(/addr:/,"",$2); print $2}')
    if [ -z "$IP_ADDRESS" ]; then
        echo "ERROR: Failed to get IP address for network interface '${NETWORK_INTERFACE_NAME}'"
        echo "Please check if the network interface exists or specify a valid interface using SOCKET_IFNAME environment variable"
        exit 1
    fi
    echo "IP_ADDRESS: ${IP_ADDRESS}"
    
    if [ $# -lt 2 ]; then
        echo "ERROR: At least 2 device IDs are required."
        exit 1
    fi
    validate_device_ids "$@"
    local device_id_1="$1"
    local device_id_2="$2"
    local flag=0
    cd "${BASEPATH}/../build/examples/cpp"
    # examples/cpp
    run_pair "./prompt_pull_cache_and_blocks ${device_id_1} ${IP_ADDRESS}" "./decoder_pull_cache_and_blocks ${device_id_2} ${IP_ADDRESS} ${IP_ADDRESS}"
    run_pair "./prompt_push_cache_and_blocks ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}" "./decoder_push_cache_and_blocks ${device_id_2} ${IP_ADDRESS}"
    run_pair "./prompt_switch_roles ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}" "./decoder_switch_roles ${device_id_2} ${IP_ADDRESS} ${IP_ADDRESS}"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d ${device_id_2} ${IP_ADDRESS}:16000"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d ${device_id_1} ${IP_ADDRESS}:16000 ${IP_ADDRESS}:16001" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d ${device_id_2} ${IP_ADDRESS}:16001 ${IP_ADDRESS}:16000"

    cd "${BASEPATH}/python"
    # examples/python 单机用例
    run_pair "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 push_blocks_sample.py \
    --device_id ${device_id_1} --role p --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}" \
    "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 push_blocks_sample.py \
    --device_id ${device_id_2} --role d --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}"
    run_pair "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 push_cache_sample.py \
    --device_id ${device_id_1} --role p --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}" \
    "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 push_cache_sample.py \
    --device_id ${device_id_2} --role d --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}"
    run_pair "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 switch_role_sample.py \
    --device_id ${device_id_1} --role p --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}" \
    "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 switch_role_sample.py \
    --device_id ${device_id_2} --role d --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}"
    run_pair "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 transfer_cache_async_sample.py \
    --device_id ${device_id_1} --role p --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}" \
    "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 transfer_cache_async_sample.py \
    --device_id ${device_id_2} --role d --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}"
    run_pair "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_xpyd_sample.py \
    --device_id ${device_id_1} --role p --local_ip_port ${IP_ADDRESS}:16000" \
    "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_xpyd_sample.py \
    --device_id ${device_id_2} --role d --local_ip_port ${IP_ADDRESS}:16001 --remote_ip_port '${IP_ADDRESS}:16000'"

    cd "${BASEPATH}/../build/benchmarks"
    # benchmarks
    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2d write false" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2d write false"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2d write false" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2d write false"

    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2d write false" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2d write false"
    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2d write true" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2d write true"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2d write false" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2d write false"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2d write true" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2d write true"

    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2h write true" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2h write true"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2h write false" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2h write false"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2h write true" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2h write true"

    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2h write true" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2h write true"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2h write false" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2h write false"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2h write true" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2h write true"

    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2d read false" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2d read false"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2d read false" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2d read false"

    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2d read false" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2d read false"
    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2d read true" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2d read true"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2d read false" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2d read false"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2d read true" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2d read true"

    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2h read true" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2h read true"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2h read false" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2h read false"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 d2h read true" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 d2h read true"

    run_pair "./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2h read true" \
    "./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2h read true"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2h read false" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2h read false"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_1} ${IP_ADDRESS} ${IP_ADDRESS}:16000 20000 h2h read true" \
    "HCCL_INTRA_ROCE_ENABLE=1 ./benchmark ${device_id_2} ${IP_ADDRESS}:16000 ${IP_ADDRESS} 20000 h2h read true"

    if [ "$flag" -eq "0" ]; then
        echo "execute samples success"
    fi
    echo "---------------- Finished ----------------"
}

smoke_test_samples() {
    if [ $# -lt 2 ]; then
        echo "ERROR: At least 2 device IDs are required."
        exit 1
    fi
    validate_device_ids "$@"
    local device_id_1="$1"
    local device_id_2="$2"
    local flag=0
    # C++ examples
    cd "${BASEPATH}/../build/examples/cpp"
    run_pair "./prompt_pull_cache_and_blocks ${device_id_1} 127.0.0.1" "./decoder_pull_cache_and_blocks ${device_id_2} 127.0.0.1 127.0.0.1"
    run_pair "./prompt_push_cache_and_blocks ${device_id_1} 127.0.0.1 127.0.0.1" "./decoder_push_cache_and_blocks ${device_id_2} 127.0.0.1"
    run_pair "./prompt_switch_roles ${device_id_1} 127.0.0.1 127.0.0.1" "./decoder_switch_roles ${device_id_2} 127.0.0.1 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d ${device_id_1} 127.0.0.1 127.0.0.1:16000" "HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d ${device_id_2} 127.0.0.1:16000"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d ${device_id_1} 127.0.0.1:16000 127.0.0.1:16001" "HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d ${device_id_2} 127.0.0.1:16001 127.0.0.1:16000"

    # Python examples
    cd "${BASEPATH}/python"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 push_blocks_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 push_blocks_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 hixl_tranfer_backend_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 hixl_tranfer_backend_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 push_cache_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 push_cache_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 switch_role_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 switch_role_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_xpyd_sample.py --device_id ${device_id_1} --role p --local_ip_port 127.0.0.1:16000" "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_xpyd_sample.py --device_id ${device_id_2} --role d --local_ip_port 10.170.10.0:16001 --remote_ip_port 127.0.0.1:16000"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 transfer_cache_async_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 transfer_cache_async_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3.9 pull_cache_sample.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3.9 pull_cache_sample.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3.9 pull_blocks_sample.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3.9 pull_blocks_sample.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3.9 pull_from_cache_to_blocks.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3.9 pull_from_cache_to_blocks.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip 127.0.0.1"




    if [ "$flag" -eq "0" ]; then
        echo "execute samples success"
    fi
}

main() {
    case "$1" in
        -a | --all)
            shift
            all_samples "$@"
            ;;
        *)
            smoke_test_samples "$@"
            ;;
    esac
}

main "$@"