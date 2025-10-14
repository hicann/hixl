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

set -e

BASEPATH=$(cd "$(dirname $0)"; pwd)
OUTPUT_PATH="${BASEPATH}/build_out"
BUILD_RELATIVE_PATH="build"
BUILD_PATH="${BASEPATH}/${BUILD_RELATIVE_PATH}/"
ASCEND_INSTALL_PATH=""

# print usage message
usage() {
  echo "Usage:"
  echo "  sh build.sh [-h | --help] [-v | --verbose] [-j<N>]"
  echo "              [--build_type=<Release|Debug>]"
  echo "              [--ascend_install_path=<PATH>] [--ascend_3rd_lib_path=<PATH>] [--output_path=<PATH>]"
  echo ""
  echo "Options:"
  echo "    -h, --help        Print usage"
  echo "    -v, --verbose     Display build command"
  echo "    -j<N>             Set the number of threads used for building HIXL, default is 8"
  echo "    --build_type=<Release|Debug>"
  echo "                      Set build type, default Release"
  echo "    --ascend_install_path=<PATH>"
  echo "                      Set ascend package install path, default /usr/local/Ascend/latest"
  echo "    --ascend_3rd_lib_path=<PATH>"
  echo "                      Set ascend third_party package install path, default ./build/third_party"
  echo "    --output_path=<PATH>"
  echo "                      Set output path, default ./build_out"
  echo ""
}

# check value of build_type option
# usage: check_build_type build_type
check_build_type() {
  arg_value="$1"
  if [ "X$arg_value" != "XRelease" ] && [ "X$arg_value" != "XDebug" ]; then
    echo "Invalid value $arg_value for option --$2"
    usage
    exit 1
  fi
}

# parse and set options
checkopts() {
  VERBOSE=""
  THREAD_NUM=8

  OUTPUT_PATH="${BASEPATH}/build_out"
  ASCEND_3RD_LIB_PATH="$BASEPATH/build/third_party"
  CMAKE_BUILD_TYPE="Release"

  # Process the options
  parsed_args=$(getopt -a -o j:hv -l help,verbose,ascend_install_path:,ascend_3rd_lib_path:,output_path:,build_type: -- "$@") || {
    usage
    exit 1
  }

  eval set -- "$parsed_args"

  while true; do
    case "$1" in
      -h | --help)
        usage
        exit 0
        ;;
      -j)
        THREAD_NUM="$2"
        shift 2
        ;;
      -v | --verbose)
        VERBOSE="VERBOSE=1"
        shift
        ;;
      --ascend_install_path)
        ASCEND_INSTALL_PATH="$(realpath $2)"
        shift 2
        ;;
      --ascend_3rd_lib_path)
        ASCEND_3RD_LIB_PATH="$(realpath $2)"
        shift 2
        ;;
      --output_path)
        OUTPUT_PATH="$(realpath $2)"
        shift 2
        ;;
      --build_type)
        check_build_type "$2" build_type
        CMAKE_BUILD_TYPE="$2"
        shift 2
        ;;
      --)
        shift
        break
        ;;
      *)
        echo "Undefined option: $1"
        usage
        exit 1
        ;;
    esac
  done
}

mk_dir() {
  local create_dir="$1"  # the target to make
  mkdir -pv "${create_dir}"
  echo "created ${create_dir}"
}

build() {
  echo "create build directory and build hixl";
  mk_dir "${BUILD_PATH}"
  cd "${BUILD_PATH}"
  cmake -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -D CMAKE_INSTALL_PREFIX=${OUTPUT_PATH} \
        ${ASCEND_INSTALL_PATH:+-D ASCEND_INSTALL_PATH=${ASCEND_INSTALL_PATH}} \
        ${ASCEND_3RD_LIB_PATH:+-D ASCEND_3RD_LIB_PATH=${ASCEND_3RD_LIB_PATH}} \
        ..

  make ${VERBOSE} -j${THREAD_NUM} && make package
  if [ $? -ne 0 ]
  then
    echo "execute command: make ${VERBOSE} -j${THREAD_NUM} && make package failed."
    return 1
  fi
  echo "Build success!"

  if [ -f _CPack_Packages/makeself_staging/cann*.run ];then
    mv _CPack_Packages/makeself_staging/cann*.run ${OUTPUT_PATH}
  else
    echo "package hixl run failed"
    return 1
  fi

  echo "hixl package success!"
}

main() {
  cd "${BASEPATH}"
  checkopts "$@"
  g++ -v

  mk_dir ${OUTPUT_PATH}
  build || { echo "Build failed."; exit 1; }
  echo "---------------- Build finished ----------------"
}

main "$@"
