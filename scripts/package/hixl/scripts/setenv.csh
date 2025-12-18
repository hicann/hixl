#!/usr/bin/env csh
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set CURFILE=`readlink -f ${1}`
set CURPATH=`dirname ${CURFILE}`

set install_info="$CURPATH/../ascend_install.info"
set install_path=`grep -i "HIXL_Install_Path_Param=" "$install_info" | cut --only-delimited -d"=" -f2`
set INSTALL_DIR="`realpath ${install_path}/cann`"

set lib_path="${INSTALL_DIR}/python/site-packages/"
if ( -d "${lib_path}" ) then
    set python_path=""
    if ( $?PYTHONPATH == 1 ) then
        set python_path="$PYTHONPATH"
    endif
    set num=`echo ":${python_path}:" | grep ":${lib_path}:" | wc -l`
    if ( "${num}" == 0 ) then
        if ( "-${python_path}" == "-" ) then
            setenv PYTHONPATH ${lib_path}
        else
            setenv PYTHONPATH ${lib_path}:${python_path}
        endif
    endif
endif

set library_path="${INSTALL_DIR}/lib64"
if ( -d "${library_path}" ) then
    set ld_library_path=""
    if ( $?LD_LIBRARY_PATH == 1 ) then
        set ld_library_path="$LD_LIBRARY_PATH"
    endif
    set num=`echo ":${ld_library_path}:" | grep ":${library_path}:" | wc -l`
    if ( "$num" == 0 ) then
        if ( "-${ld_library_path}" == "-" ) then
            setenv LD_LIBRARY_PATH "${library_path}"
        else
            setenv LD_LIBRARY_PATH "${ld_library_path}:${library_path}"
        endif
    endif
endif
