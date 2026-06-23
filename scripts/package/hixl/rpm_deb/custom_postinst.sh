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

sourcedir="${INSTALL_PATH}"
WHL_INSTALL_DIR_PATH="${sourcedir}/python/site-packages"
unset PYTHONPATH
export PIP_BREAK_SYSTEM_PACKAGES=1

WHL_LLM_DATADIST="${sourcedir}/hixl/lib/llm_datadist-0.0.1-py3-none-any.whl"

run_pip() { python3 -m pip "$@" || pip3 "$@"; }

input_install_for_all=n
if [ "$(id -u)" -eq 0 ]; then
    input_install_for_all=y
fi

chmod_recur() {
    local file_path="${1}"
    local permission="${2}"
    local type="${3}"
    permission=$(set_file_chmod "$permission")
    if [ "$type" = "dir" ]; then
        find "$file_path" -type d -exec chmod "$permission" {} \; 2> /dev/null || true
    elif [ "$type" = "file" ]; then
        find "$file_path" -type f -exec chmod "$permission" {} \; 2> /dev/null || true
    fi
}

set_file_chmod() {
    local permission="${1}"
    local new_permission=""
    if [ "${input_install_for_all}" = "y" ]; then
        new_permission="$(expr substr $permission 1 2)$(expr substr $permission 2 1)"
        echo "$new_permission"
    else
        echo "$permission"
    fi
}

[ -f "${WHL_LLM_DATADIST}" ] && echo "[hixl] installing ${WHL_LLM_DATADIST}" && run_pip install --disable-pip-version-check --upgrade --no-deps --force-reinstall -t "${WHL_INSTALL_DIR_PATH}" "${WHL_LLM_DATADIST}" || true

if [ -d "${WHL_INSTALL_DIR_PATH}" ]; then
    chmod_recur "${sourcedir}/python" 750 dir
    chmod_recur "${WHL_INSTALL_DIR_PATH}/llm_datadist" 550 dir
    chmod_recur "${WHL_INSTALL_DIR_PATH}/llm_datadist" 550 file
    chmod_recur "${WHL_INSTALL_DIR_PATH}"/llm_datadist-*.dist-info 550 dir
    chmod_recur "${WHL_INSTALL_DIR_PATH}"/llm_datadist-*.dist-info 550 file
    chmod_recur "${WHL_INSTALL_DIR_PATH}/LICENSE" 440 file
fi

 rm -rf "${sourcedir}/hixl" 2>/dev/null || true
