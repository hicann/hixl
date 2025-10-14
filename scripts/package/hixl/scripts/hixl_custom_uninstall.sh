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

curpath=$(dirname $(readlink -f "$0"))
unset PYTHONPATH

common_parse_dir=""
logfile=""
stage=""
is_quiet="n"
hetero_arch="n"

while true; do
    case "$1" in
    --install-path=*)
        pkg_install_path=$(echo "$1" | cut -d"=" -f2-)
        shift
        ;;
    --common-parse-dir=*)
        common_parse_dir=$(echo "$1" | cut -d"=" -f2-)
        shift
        ;;
    --version-dir=*)
        pkg_version_dir=$(echo "$1" | cut -d"=" -f2-)
        shift
        ;;
    --logfile=*)
        logfile=$(echo "$1" | cut -d"=" -f2)
        shift
        ;;
    --stage=*)
        stage=$(echo "$1" | cut -d"=" -f2)
        shift
        ;;
    --quiet=*)
        is_quiet=$(echo "$1" | cut -d"=" -f2)
        shift
        ;;
    --hetero-arch=*)
        hetero_arch=$(echo "$1" | cut -d"=" -f2)
        shift
        ;;
    -*)
        shift
        ;;
    *)
        break
        ;;
    esac
done

# 写日志
log() {
    local cur_date="$(date +'%Y-%m-%d %H:%M:%S')"
    local log_type="$1"
    local log_msg="$2"
    local log_format="[hixl] [$cur_date] [$log_type]: $log_msg"
    if [ "$log_type" = "INFO" ]; then
        echo "$log_format"
    elif [ "$log_type" = "WARNING" ]; then
        echo "$log_format"
    elif [ "$log_type" = "ERROR" ]; then
        echo "$log_format"
    elif [ "$log_type" = "DEBUG" ]; then
        echo "$log_format" 1> /dev/null
    fi
    echo "$log_format" >> "$logfile"
}

get_arch_name() {
    local pkg_dir="$1"
    local scene_file="$pkg_dir/scene.info"
    grep '^arch=' $scene_file | cut -d"=" -f2
}

remove_stub_softlink() {
    local ref_dir="$1"
    if [ ! -d "$ref_dir" ]; then
        return
    fi
    local stub_dir="$2"
    if [ ! -d "$stub_dir" ]; then
        return
    fi
    local pwdbak="$(pwd)"
    cd $stub_dir && chmod u+w . && ls -1 "$ref_dir" | xargs --no-run-if-empty rm -rf
    cd $pwdbak
}

recreate_common_stub_softlink() {
    local arch_name="$1"
    local stub_dir="$2"
    if [ ! -d "$stub_dir" ]; then
        return
    fi
    local pwdbak="$(pwd)"
    cd $stub_dir && [ -f "$arch_name/libgraph.so" ] && chmod u+w . && ln -s "$arch_name/libgraph.so" libgraph.so
    cd $pwdbak
}

whl_uninstall_package() {
    local _module="$1"
    local _module_apth="$2"
    if [ ! -d "${WHL_INSTALL_DIR_PATH}/${_module}" ]; then
        pip3 show "${_module}" > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            log "WARNING" "${_module} is not exist."
        else
            pip3 uninstall -y "${_module}" 1> /dev/null
            local ret=$?
            if [ $ret -ne 0 ]; then
                log "WARNING" "uninstall ${_module} failed, error code: $ret."
                exit 1
            else
                log "INFO" "${_module} uninstalled successfully!"
            fi
        fi
    else
        export PYTHONPATH="${_module_apth}"
        pip3 uninstall -y "${_module}" > /dev/null 2>&1
        local ret=$?
        if [ $ret -ne 0 ]; then
            log "WARNING" "uninstall ${_module} failed, error code: $ret."
            exit 1
        else
            log "INFO" "${_module} uninstalled successfully!"
        fi
    fi
}

remove_rl_soft_link() {
    local _path="${1}"
    if [ -d "${_path}" ]; then
        rm -rf "${_path}"
    fi
}

remove_empty_dir() {
    local _path="${1}"
    if [ -d "${_path}" ]; then
        local is_empty=$(ls "${_path}" | wc -l)
        if [ "$is_empty" -ne 0 ]; then
            log "INFO" "${_path} dir is not empty."
        else
            prev_path=$(dirname "${_path}")
            chmod +w "${prev_path}" > /dev/null 2>&1
            rm -rf "${_path}" > /dev/null 2>&1
        fi
    fi
}

remove_dir() {
    local _path="${1}"
    if [ -d "${_path}" ]; then
        prev_path=$(dirname "${_path}")
        chmod +w "${prev_path}" > /dev/null 2>&1
        rm -rf "${_path}" > /dev/null 2>&1
    fi
}

remove_last_license() {
    if [ -d "$WHL_INSTALL_DIR_PATH" ]; then
        if [ -f "$WHL_INSTALL_DIR_PATH/LICENSE" ]; then
            rm -rf "$WHL_INSTALL_DIR_PATH/LICENSE"
        fi
    fi
}

WHL_SOFTLINK_INSTALL_DIR_PATH="${common_parse_dir}/hixl/python/site-packages"
WHL_INSTALL_DIR_PATH="${common_parse_dir}/python/site-packages"
HIXL_NAME="hixl"

custom_uninstall() {
    if [ -z "$common_parse_dir/hixl" ]; then
        log "ERROR" "ERR_NO:0x0001;ERR_DES:hixl directory is empty"
        exit 1
    fi

    if [ "$hetero_arch" != "y" ]; then
        local arch_name="$(get_arch_name $common_parse_dir/hixl)"
        local ref_dir="$common_parse_dir/hixl/lib64/stub/linux/$arch_name"
        remove_stub_softlink "$ref_dir" "$common_parse_dir/hixl/lib64/stub"
    else
        local arch_name="$(get_arch_name $common_parse_dir/hixl)"
        local ref_dir="$common_parse_dir/hixl/lib64/stub/linux/$arch_name"
        remove_stub_softlink "$ref_dir" "$common_parse_dir/hixl/lib64/stub"
    fi

    if [ "$hetero_arch" != "y" ]; then
        chmod +w -R "$curpath" 2> /dev/null
        chmod +w -R "${WHL_INSTALL_DIR_PATH}/llm_datadist" 2> /dev/null
        chmod +w -R "${WHL_INSTALL_DIR_PATH}/llm_datadist-0.0.1.dist-info" 2> /dev/null

        log "INFO" "uninstall hixl tool begin..."
        whl_uninstall_package "${HIXL_NAME}" "${WHL_INSTALL_DIR_PATH}"
        log "INFO" "hixl tool uninstalled successfully!"
    fi

    test -d "$WHL_SOFTLINK_INSTALL_DIR_PATH" && rm -rf "$WHL_SOFTLINK_INSTALL_DIR_PATH" > /dev/null 2>&1
    remove_dir "${common_parse_dir}/hixl/python"

    if [ "$hetero_arch" != "y" ]; then
        if [ -d "${WHL_INSTALL_DIR_PATH}" ]; then
            local python_path=$(dirname "$WHL_INSTALL_DIR_PATH")
            chmod +w "${python_path}"
            remove_last_license
        fi

        remove_dir "${WHL_INSTALL_DIR_PATH}"
        remove_dir "${common_parse_dir}/python"
    fi

    return 0
}

custom_uninstall
if [ $? -ne 0 ]; then
    exit 1
fi
exit 0
