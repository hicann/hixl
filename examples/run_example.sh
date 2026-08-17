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

# Detect smoke env type from npu-smi NPU Name into ENV_TYPE.
# 910B* -> A2; Ascend910* (and not 910B) -> A3. Extensible for future types.
detect_npu_env_type() {
    ENV_TYPE=""
    if ! command -v npu-smi >/dev/null 2>&1; then
        echo "ERROR: npu-smi command not found."
        exit 1
    fi

    local npu_info
    if ! npu_info=$(npu-smi info 2>&1); then
        echo "ERROR: failed to run npu-smi info."
        echo "${npu_info}"
        exit 1
    fi
    echo "${npu_info}"

    local npu_name
    npu_name=$(echo "${npu_info}" | awk -F'|' '
        $0 ~ /NPU[[:space:]]+Name/ { in_table=1; next }
        in_table && $2 ~ /^[[:space:]]*[0-9]+[[:space:]]+/ {
            split($2, fields, " ")
            print fields[2]
            exit
        }
    ')
    if [ -z "${npu_name}" ]; then
        echo "ERROR: failed to parse NPU Name from npu-smi info."
        exit 1
    fi
    echo "NPU Name: ${npu_name}"

    # Check 910B before Ascend910: names like 910B3 must map to A2.
    if [[ "${npu_name}" == *910B* ]]; then
        ENV_TYPE="A2"
    elif [[ "${npu_name}" == *Ascend910* ]]; then
        ENV_TYPE="A3"
    else
        echo "ERROR: unsupported NPU Name '${npu_name}'. Supported: *910B* (A2), *Ascend910* (A3)."
        exit 1
    fi
    echo "Detected environment type: ${ENV_TYPE}"
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
        for tmp in "${tmp_files[@]}"; do
            abs_path=$(readlink -f "$tmp" 2>/dev/null)
            [[ -z "$abs_path" ]] && continue
            if [[ "$abs_path" =~ ^/tmp/tmp\.[0-9a-zA-Z_]+$ && -f "$abs_path" ]]; then
                rm -f "$abs_path"
                echo "Deleted safe temp file: $abs_path"
            fi
        done
        exit 1
    fi

    if [ "$has_error" -eq "0" ]; then
        echo -e "Execution success.\n"
    fi

    for tmp in "${tmp_files[@]}"; do
        abs_path=$(readlink -f "$tmp" 2>/dev/null)
        [[ -z "$abs_path" ]] && continue
        if [[ "$abs_path" =~ ^/tmp/tmp\.[0-9a-zA-Z_]+$ && -f "$abs_path" ]]; then
            rm -f "$abs_path"
            echo "Deleted safe temp file: $abs_path"
        fi
    done
}

run_comm_bench_pair() {
    local bench_bin="$1"
    local transport="$2"
    local initiator_memory="$3"
    local target_memory="$4"
    local op_type="$5"
    local client_device="$6"
    local server_device="$7"
    local ip_address="$8"
    local hixl_port="$9"
    local common_args="--transport=${transport}"
    local initiator_args="${common_args} --memory=${initiator_memory} --remote_memory=${target_memory} --op=${op_type}"
    local target_args="${common_args} --memory=${target_memory} --peer_count=1"

    run_pair "${bench_bin} --role=target --device_id=${server_device} --local_engine=${ip_address}:${hixl_port} --remote_engine=${ip_address} ${target_args}" \
    "${bench_bin} --role=initiator --device_id=${client_device} --local_engine=${ip_address}:$((hixl_port + 1)) --remote_engine=${ip_address}:${hixl_port} ${initiator_args}"
}

run_hixl_cpp_examples() {
    local device_id_1="$1"
    local device_id_2="$2"
    local env_type="$3"
    local ip_address="${4:-127.0.0.1}"

    # version=0: A2 and A3
    run_pair "./hixl_example_d2rd --protocol=roce:device --device=${device_id_1},${device_id_2} --version=0"
    run_pair "./hixl_example_d2rh --protocol=roce:device --device=${device_id_1},${device_id_2} --version=0"
    run_pair "./hixl_example_d2rd_multiproc --role=server --protocol=roce:device --device=${device_id_2} --version=0" \
    "./hixl_example_d2rd_multiproc --role=client --protocol=roce:device --device=${device_id_1} --version=0"

    # version=1: A2 and A3
    run_pair "./hixl_example_d2rd --protocol=roce:device --device=${device_id_1},${device_id_2} --version=1"
    run_pair "./hixl_example_d2rh --protocol=roce:device --device=${device_id_1},${device_id_2} --version=1"
    run_pair "./hixl_example_d2rd_multiproc --role=server --protocol=hccs:device --device=${device_id_2} --version=1" \
    "./hixl_example_d2rd_multiproc --role=client --protocol=hccs:device --device=${device_id_1} --version=1"
    run_pair "./hixl_example_d2rd_multiproc --role=server --protocol=roce:device --device=${device_id_2} --version=1" \
    "./hixl_example_d2rd_multiproc --role=client --protocol=roce:device --device=${device_id_1} --version=1"

}

all_samples() {
    detect_npu_env_type
    local env_type="${ENV_TYPE}"

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
    run_hixl_cpp_examples "${device_id_1}" "${device_id_2}" "${env_type}" "${IP_ADDRESS}"

    cd "${BASEPATH}/python/llm_datadist"
    # examples/python/llm_datadist
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
    run_pair "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 hixl_transfer_backend_sample.py \
    --device_id ${device_id_1} --role p --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}" \
    "GLOO_SOCKET_IFNAME=${NETWORK_INTERFACE_NAME} HCCL_INTRA_ROCE_ENABLE=1 python3 hixl_transfer_backend_sample.py \
    --device_id ${device_id_2} --role d --local_host_ip ${IP_ADDRESS} --remote_host_ip ${IP_ADDRESS}"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_cache_sample.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip ${IP_ADDRESS}" \
    "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_cache_sample.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip ${IP_ADDRESS}"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_sample.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip ${IP_ADDRESS}" \
    "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_sample.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip ${IP_ADDRESS}"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_from_cache_to_blocks.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip ${IP_ADDRESS}" \
    "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_from_cache_to_blocks.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip ${IP_ADDRESS}"

    cd "${BASEPATH}/python/hixl"
    # examples/python/hixl
    run_pair "python3 hixl_d2rd_multiproc_sample.py --role server --device ${device_id_2} --local-engine ${IP_ADDRESS}:16101 --protocol roce:device" \
    "python3 hixl_d2rd_multiproc_sample.py --role client --device ${device_id_1} --local-engine ${IP_ADDRESS}:16100 --remote-engine ${IP_ADDRESS}:16101 --protocol roce:device"

    cd "${BASEPATH}/../build/benchmarks"
    BENCH_BIN="./comm_benchmark/hixl_comm_bench"
    # benchmarks (key=value CLI; peer TCP coordination port is derived from target HIXL port +10000 or -10000)
    # HCCS: D2D smoke cases.
    run_comm_bench_pair "${BENCH_BIN}" "hccs" "device" "device" "write" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"
    run_comm_bench_pair "${BENCH_BIN}" "hccs" "device" "device" "read" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"

    # RoCE: all memory direction smoke cases.
    run_comm_bench_pair "${BENCH_BIN}" "roce" "device" "device" "write" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"
    run_comm_bench_pair "${BENCH_BIN}" "roce" "host" "device" "write" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"
    run_comm_bench_pair "${BENCH_BIN}" "roce" "device" "host" "write" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"
    run_comm_bench_pair "${BENCH_BIN}" "roce" "host" "host" "write" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"
    run_comm_bench_pair "${BENCH_BIN}" "roce" "device" "device" "read" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"
    run_comm_bench_pair "${BENCH_BIN}" "roce" "host" "device" "read" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"
    run_comm_bench_pair "${BENCH_BIN}" "roce" "device" "host" "read" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"
    run_comm_bench_pair "${BENCH_BIN}" "roce" "host" "host" "read" "${device_id_1}" "${device_id_2}" "${IP_ADDRESS}" "16000"

    if [ "$flag" -eq "0" ]; then
        echo "execute samples success"
    fi
    echo "---------------- Finished ----------------"
}

smoke_test_samples() {
    detect_npu_env_type
    local env_type="${ENV_TYPE}"

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
    run_hixl_cpp_examples "${device_id_1}" "${device_id_2}" "${env_type}" "127.0.0.1"

    # Python llm_datadist examples
    cd "${BASEPATH}/python/llm_datadist"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 push_blocks_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 push_blocks_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 hixl_transfer_backend_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 hixl_transfer_backend_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 push_cache_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 push_cache_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 switch_role_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 switch_role_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_xpyd_sample.py --device_id ${device_id_1} --role p --local_ip_port 127.0.0.1:16000" "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_xpyd_sample.py --device_id ${device_id_2} --role d --local_ip_port 127.0.0.1:16001 --remote_ip_port 127.0.0.1:16000"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 transfer_cache_async_sample.py --device_id ${device_id_1} --role p --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 transfer_cache_async_sample.py --device_id ${device_id_2} --role d --local_host_ip 127.0.0.1 --remote_host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_cache_sample.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_cache_sample.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_sample.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_blocks_sample.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip 127.0.0.1"
    run_pair "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_from_cache_to_blocks.py --device_id ${device_id_1} --cluster_id 1 --is_single true --host_ip 127.0.0.1" "HCCL_INTRA_ROCE_ENABLE=1 python3 pull_from_cache_to_blocks.py --device_id ${device_id_2} --cluster_id 2 --is_single true --host_ip 127.0.0.1"

    # Python hixl examples
    cd "${BASEPATH}/python/hixl"
    run_pair "python3 hixl_d2rd_multiproc_sample.py --role server --device ${device_id_2} --local-engine 127.0.0.1:16101 --protocol roce:device" \
    "python3 hixl_d2rd_multiproc_sample.py --role client --device ${device_id_1} --local-engine 127.0.0.1:16100 --remote-engine 127.0.0.1:16101 --protocol roce:device"


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
