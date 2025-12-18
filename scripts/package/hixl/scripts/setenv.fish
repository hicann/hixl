#!/usr/bin/env fish
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set -g curpath (realpath (dirname (status --current-filename)))
set -g curfile (realpath (status --current-filename))
set -g param_mult_ver $argv[1]

function get_install_param
    set -l _key $argv[1]
    set -l _file $argv[2]
    if not test -f "$_file"
        exit 1
    end
    set -l install_info_key_array HIXL_Install_Type HIXL_Feature_Type HIXL_UserName HIXL_UserGroup HIXL_Install_Path_Param HIXL_Arch_Linux_Path HIXL_Hetero_Arch_Flag
    for key_param in $install_info_key_array
        if test "$key_param" = "$_key"
            grep -i "$_key=" "$_file" | cut --only-delimited -d"=" -f2-
            break
        end
    end
end

function get_install_dir
    local install_info="$curpath/../ascend_install.info"
    get_install_param "HIXL_Install_Path_Param" "${install_info}"
end

set -l INSTALL_DIR (get_install_dir)/cann
set -l lib_path "$INSTALL_DIR/python/site-packages/"
if test -d "$lib_path"
    set -l python_path "$PYTHONPATH"
    set -l num (echo ":$python_path:" | grep ":$lib_path:" | wc -l)
    if test "$num" -eq 0
        if test "-$python_path" = "-"
            set -gx PYTHONPATH "$lib_path"
        else
            set -gx PYTHONPATH "$lib_path:$python_path"
        end
    end
end

set -l library_path "$INSTALL_DIR/lib64"
if test -d "$library_path"
    set -l ld_library_path "$LD_LIBRARY_PATH"
    set -l num (echo ":$ld_library_path:" | grep ":$library_path:" | wc -l)
    if test "$num" -eq 0
        if test "-$ld_library_path" = "-"
            set -gx LD_LIBRARY_PATH "$library_path"
        else
            set -gx LD_LIBRARY_PATH "$ld_library_path:$library_path"
        end
    end
end
