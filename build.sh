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

# print usage message
usage() {
  echo "Usage:"
  echo "  sh build.sh [-h | --help] [-v | --verbose] [-j<N>]"
  echo "              [--pkg] [--examples]"
  echo "              [--build_type=<Release|Debug> | --build-type=<Release|Debug]"
  echo "              [--cann_3rd_lib_path=<PATH> | --cann-3rd-lib-path=<PATH>]"
  echo "              [--output_path=<PATH> | --output-path=<PATH>]"
  echo "              [--asan] [--cov]"
  echo ""
  echo "Options:"
  echo "    -h, --help        Print usage"
  echo "    -v, --verbose     Display build command"
  echo "    -j<N>             Set the number of threads used for building HIXL, default is 8"
  echo "    --build_type=<Release|Debug> |--build-type=<Release|Debug>"
  echo "                      Set build type, default Release"
  echo "    --cann_3rd_lib_path=<PATH> | --cann_3rd_lib_path=<PATH>"
  echo "                      Set ascend third_party package install path, default ./third_party"
  echo "    --output_path=<PATH> | --output-path=<PATH>"
  echo "                      Set output path, default ./build_out"
  echo "    --pkg             Build run package, reserved parameter"
  echo "    --examples        Build with examples and benchmarks, default is OFF"
  echo "    --asan            Enable AddressSanitizer, default is OFF"
  echo "    --cov             Enable Coverage, default is OFF"
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
  CANN_3RD_LIB_PATH="$BASEPATH/third_party"
  CMAKE_BUILD_TYPE="Release"
  ENABLE_EXAMPLES=OFF
  ENABLE_BENCHMARKS=OFF
  ENABLE_ASAN=OFF
  ENABLE_GCOV=OFF

  # Process the options
  parsed_args=$(getopt -a -o j:hv -l help,verbose,pkg,examples,cann_3rd_lib_path:,cann-3rd-lib-path:,output_path:,output-path:,build_type:,build-type:,asan,cov -- "$@") || {
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
      --cann_3rd_lib_path | --cann-3rd-lib-path)
        CANN_3RD_LIB_PATH="$(realpath $2)"
        shift 2
        ;;
      --output_path | --output-path)
        OUTPUT_PATH="$(realpath $2)"
        shift 2
        ;;
      --build_type | --build-type)
        check_build_type "$2" build_type
        CMAKE_BUILD_TYPE="$2"
        shift 2
        ;;
      --pkg)
        shift
        ;;
      --examples)
        shift
        ENABLE_EXAMPLES=ON
        ENABLE_BENCHMARKS=ON
        ;;
      --)
        shift
        break
        ;;
      --cov)
        ENABLE_GCOV=ON
        CMAKE_BUILD_TYPE="Debug"
        shift
        ;;
      --asan)
        ENABLE_ASAN=ON
        CMAKE_BUILD_TYPE="Debug"
        shift
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
        -D ENABLE_EXAMPLES=${ENABLE_EXAMPLES} \
        -D ENABLE_BENCHMARKS=${ENABLE_BENCHMARKS} \
        -D ENABLE_ASAN=${ENABLE_ASAN} \
        -D ENABLE_GCOV=${ENABLE_GCOV} \
        ${CANN_3RD_LIB_PATH:+-D CANN_3RD_LIB_PATH=${CANN_3RD_LIB_PATH}} \
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
