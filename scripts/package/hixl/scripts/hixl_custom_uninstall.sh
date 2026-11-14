#!/bin/bash
# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

curpath=$(dirname "$(readlink -f "$0")")
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
    cd "$stub_dir" && chmod u+w . && \
        find -L "$ref_dir" -mindepth 1 -maxdepth 1 ! -name '.*' -printf '%f\0' | xargs -0 --no-run-if-empty rm -rf --
    cd "$pwdbak"
}

recreate_common_stub_softlink() {
    local arch_name="$1"
    local stub_dir="$2"
    if [ ! -d "$stub_dir" ]; then
        return
    fi
    local pwdbak="$(pwd)"
    cd "$stub_dir" && [ -f "$arch_name/libgraph.so" ] && chmod u+w . && ln -s "$arch_name/libgraph.so" libgraph.so
    cd "$pwdbak"
}

remove_whl_residuals() {
    local _module_path="$1"
    local _whl_name="$2"

    if [ -z "${_module_path}" ] || [ -z "${_whl_name}" ]; then
        log "ERROR" "remove whl residuals failed, invalid path or package name."
        exit 1
    fi

    chmod +w -R "${_module_path}/${_whl_name}" 2> /dev/null
    chmod +w -R "${_module_path}/${_whl_name}-"*.dist-info 2> /dev/null
    rm -rf "${_module_path:?}/${_whl_name}" 2> /dev/null
    rm -rf "${_module_path:?}/${_whl_name}-"*.dist-info 2> /dev/null
}

whl_uninstall_package() {
    local _module="$1"
    local _module_path="$2"
    local _whl_name="$3"
    local ret=0

    if [ ! -d "${_module_path}/${_whl_name}" ]; then
        pip3 show "${_whl_name}" > /dev/null 2>&1
        if [ $? -ne 0 ]; then
            log "WARNING" "${_module} does not exist."
        else
            pip3 uninstall -y "${_whl_name}" > /dev/null 2>&1
            ret=$?
            if [ $ret -ne 0 ]; then
                log "WARNING" "pip uninstall ${_module} failed, error code: $ret."
            else
                log "INFO" "${_module} uninstalled by pip successfully!"
            fi
        fi
        remove_whl_residuals "${_module_path}" "${_whl_name}"
    else
        export PYTHONPATH="${_module_path}"
        pip3 uninstall -y "${_whl_name}" > /dev/null 2>&1
        ret=$?
        if [ $ret -ne 0 ]; then
            log "WARNING" "pip uninstall ${_module} failed, error code: $ret, clean install path residuals instead."
        else
            log "INFO" "${_module} uninstalled by pip successfully!"
        fi
        remove_whl_residuals "${_module_path}" "${_whl_name}"
    fi

    if [ -d "${_module_path}/${_whl_name}" ] || ls "${_module_path}/${_whl_name}-"*.dist-info > /dev/null 2>&1; then
        log "ERROR" "remove ${_module} residual files failed."
        exit 1
    fi
    log "INFO" "${_module} uninstalled successfully!"
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
        if [ -n "$(find "${_path}" -mindepth 1 -print -quit 2>/dev/null)" ]; then
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

WHL_INSTALL_DIR_PATH="${common_parse_dir}/python/site-packages"
HIXL_NAME="hixl"

clean_hixl_online_cache() {
    # 清理 ~/.cache 在线编译缓存目录
    local cache_dir="${HOME:-}/.cache/cann/hixl"
    if [ -n "${HOME:-}" ] && [ "$HOME" != "/" ] && [ -d "$cache_dir" ]; then
        rm -rf -- "$cache_dir" > /dev/null 2>&1
        log "INFO" "cleaned hixl online-compile cache: $cache_dir"
    fi
    # 清理 pkg_dir 可写时写回的在线编译产物与常驻锁文件 (不在 pip RECORD 里, 升级不会删)
    local hixl_pkg_dir="${WHL_INSTALL_DIR_PATH}/hixl"
    if [ -n "${WHL_INSTALL_DIR_PATH:-}" ] && [ -d "$hixl_pkg_dir" ]; then
        find "$hixl_pkg_dir" -maxdepth 1 -name "hixl.cpython-*.so" -type f -delete 2>/dev/null
        find "$hixl_pkg_dir" -maxdepth 1 -name "hixl.so" -type f -delete 2>/dev/null
        find "$hixl_pkg_dir" -maxdepth 1 -name "hixl.abi3.so" -type f -delete 2>/dev/null
        find "$hixl_pkg_dir" -maxdepth 1 -name ".build_meta.*" -type f -delete 2>/dev/null
        find "$hixl_pkg_dir" -maxdepth 1 -name ".hixl_build.*" -type f -delete 2>/dev/null
        log "INFO" "cleaned online-compile artifacts in $hixl_pkg_dir"
    fi
}

custom_uninstall() {
    if [ ! -d "$common_parse_dir/share/info/hixl" ]; then
        log "ERROR" "ERR_NO:0x0001;ERR_DES:hixl directory is empty"
        exit 1
    fi

    if [ "$hetero_arch" != "y" ]; then
        local arch_name="$(get_arch_name $common_parse_dir/share/info/hixl)"
    else
        local arch_name="$(get_arch_name $common_parse_dir/share/info/hixl)"
    fi

    if [ "$hetero_arch" != "y" ]; then
        chmod +w -R "$curpath" 2> /dev/null
        chmod +w -R "${WHL_INSTALL_DIR_PATH}/llm_datadist" 2> /dev/null
        chmod +w -R "${WHL_INSTALL_DIR_PATH}/llm_datadist-0.0.1.dist-info" 2> /dev/null
        chmod +w -R "${WHL_INSTALL_DIR_PATH}/hixl" 2> /dev/null
        chmod +w -R "${WHL_INSTALL_DIR_PATH}/hixl-"*.dist-info 2> /dev/null

        log "INFO" "uninstall llm_datadist begin..."
        whl_uninstall_package "llm_datadist" "${WHL_INSTALL_DIR_PATH}" "llm_datadist"
        log "INFO" "llm_datadist uninstalled successfully!"

        log "INFO" "uninstall hixl begin..."
        whl_uninstall_package "${HIXL_NAME}" "${WHL_INSTALL_DIR_PATH}" "hixl"
        log "INFO" "hixl extension module uninstalled successfully!"

        clean_hixl_online_cache
    fi

    remove_empty_dir "${common_parse_dir}/hixl/python"

    if [ "$hetero_arch" != "y" ]; then
        if [ -d "${WHL_INSTALL_DIR_PATH}" ]; then
            local python_path=$(dirname "$WHL_INSTALL_DIR_PATH")
            chmod +w "${python_path}"
        fi

        remove_empty_dir "${WHL_INSTALL_DIR_PATH}"
        remove_empty_dir "${common_parse_dir}/python"
    fi

    return 0
}

custom_uninstall
if [ $? -ne 0 ]; then
    exit 1
fi
exit 0
