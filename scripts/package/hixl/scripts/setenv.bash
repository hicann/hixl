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

curpath="$(dirname ${BASH_SOURCE:-$0})"
curfile="$(realpath ${BASH_SOURCE:-$0})"
param_mult_ver=$1

get_install_param() {
    local _key="$1"
    local _file="$2"
    local _param=""

    if [ ! -f "${_file}" ]; then
        exit 1
    fi
    local install_info_key_array="HIXL_Install_Type HIXL_Feature_Type HIXL_UserName HIXL_UserGroup HIXL_Install_Path_Param HIXL_Arch_Linux_Path HIXL_Hetero_Arch_Flag"
    for key_param in ${install_info_key_array}; do
        if [ "${key_param}" = "${_key}" ]; then
            _param=$(grep -i "${_key}=" "${_file}" | cut --only-delimited -d"=" -f2-)
            break
        fi
    done
    echo "${_param}"
}

get_install_dir() {
    local install_info="$curpath/../ascend_install.info"
    get_install_param "HIXL_Install_Path_Param" "${install_info}"
}

INSTALL_DIR="$(get_install_dir)/cann"
lib_path="${INSTALL_DIR}/python/site-packages/"
if [ -d "${lib_path}" ]; then
    python_path="${PYTHONPATH}"
    num=$(echo ":${python_path}:" | grep ":${lib_path}:" | wc -l)
    if [ "${num}" -eq 0 ]; then
        if [ "-${python_path}" = "-" ]; then
            export PYTHONPATH="${lib_path}"
        else
            export PYTHONPATH="${lib_path}:${python_path}"
        fi
    fi
fi

library_path="${INSTALL_DIR}/lib64"
if [ -d "${library_path}" ]; then
    ld_library_path="${LD_LIBRARY_PATH}"
    num=$(echo ":${ld_library_path}:" | grep ":${library_path}:" | wc -l)
    if [ "${num}" -eq 0 ]; then
        if [ "-${ld_library_path}" = "-" ]; then
            export LD_LIBRARY_PATH="${library_path}"
        else
            export LD_LIBRARY_PATH="${ld_library_path}:${library_path}"
        fi
    fi
fi

