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
export PYTHONPATH="${WHL_INSTALL_DIR_PATH}"
export PIP_BREAK_SYSTEM_PACKAGES=1

pip3 uninstall -y "llm_datadist" >/dev/null 2>&1 || true

rm -fr "${WHL_INSTALL_DIR_PATH}/llm_datadist" "${WHL_INSTALL_DIR_PATH}/llm_datadist-"*.dist-info 2>/dev/null

remove_empty_dir() {
    local dir="$1"
    local parent
    parent=$(dirname "${dir}")
    [ -d "${dir}" ] || return 0
    rmdir "${dir}" 2>/dev/null && chmod +w "${parent}" 2>/dev/null
}

remove_empty_dir "${WHL_INSTALL_DIR_PATH}"
remove_empty_dir "${sourcedir}/python"

