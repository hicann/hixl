# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(TARGET_LINUX_DISTRIBUTOR_ID euleros)
set(TARGET_LINUX_DISTRIBUTOR_RELEASE 2.8)

set(CPU_TYPE aarch64)
set(CMAKE_C_COMPILER     "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-gcc"     CACHE PATH "C Compiler")
set(CMAKE_CXX_COMPILER   "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-g++"    CACHE PATH "C++ Compiler")
set(CMAKE_LINKER         "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-g++"     CACHE PATH "LINKER")
set(CMAKE_AR             "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-ar"      CACHE PATH "AR")
set(CMAKE_RANLIB         "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-ranlib"  CACHE PATH "RANLIB")
set(CMAKE_STRIP          "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-strip"   CACHE PATH "STRIP")
set(CMAKE_LD             "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-ld"      CACHE PATH "LD")
set(CMAKE_NM             "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-nm"      CACHE PATH "NM")
set(CMAKE_OBJCOPY        "${TOOLCHAIN_DIR}/bin/aarch64-target-linux-gnu-objcopy" CACHE PATH "OBJCOPY")

set(CMAKE_C_COMPILE_OBJECT "<CMAKE_C_COMPILER> <DEFINES> -D__FILE__='\"$(notdir $(abspath <SOURCE>))\"' -Wno-builtin-macro-redefined <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
set(CMAKE_CXX_COMPILE_OBJECT "<CMAKE_CXX_COMPILER> <DEFINES> -D__FILE__='\"$(notdir $(abspath <SOURCE>))\"' -Wno-builtin-macro-redefined <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")

#删除静态库中的时间戳
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qcD <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> qD <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "<CMAKE_RANLIB> -D <TARGET>")

set(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> qcD <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_APPEND "<CMAKE_AR> qD <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -D <TARGET>")

set(CMAKE_SKIP_RPATH TRUE)

set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY TRUE)
