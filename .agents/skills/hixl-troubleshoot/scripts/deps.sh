#!/usr/bin/env bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

set -euo pipefail

base_dir="${TMPDIR:-/tmp}/hixl-troubleshoot-deps"

mkdir -p "${base_dir}"

clone_if_missing() {
    local repo="$1"
    local dst="${base_dir}/${repo}"

    if [ -d "${dst}/.git" ]; then
        echo "${repo} already exists at ${dst}"
        return 0
    fi

    git clone "https://github.com/cann/${repo}.git" "${dst}"
}

clone_if_missing runtime
clone_if_missing hcomm
clone_if_missing driver

echo "Dependencies are available under ${base_dir}"
