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

BASEPATH=$(cd "$(dirname $0)/.."; pwd)
ASCEND_INSTALL_PATH=""

# print usage message
usage() {
  echo "Usage:"
  echo "sh run_test.sh [-c | --cov] [-j<N>] [-h | --help] [-v | --verbose]"
  echo "               [--ascend_install_path=<PATH>] [--ascend_3rd_lib_path=<PATH>]"
  echo ""
  echo "Options:"
  echo "    -h, --help     Print usage"
  echo "    -t, --test     Build all test"
  echo "        =cpp               Build all cpp test"
  echo "        =py                Build all py test"
  echo "    -c, --cov      Build test with coverage tag"
  echo "                   Please ensure that the environment has correctly installed lcov, gcov, and genhtml."
  echo "                   and the version matched gcc/g++."
  echo "    -v, --verbose  Display build command"
  echo "    -j<N>          Set the number of threads used for building Parser, default 8"
  echo "        --ascend_install_path=<PATH>"
  echo "                   Set ascend package install path, default /usr/local/Ascend/latest"
  echo "        --ascend_3rd_lib_path=<PATH>"
  echo "                   Set ascend third_party package install path, default ./build/third_party"
  echo ""
}

mk_dir() {
  local create_dir="$1"  # the target to make
  mkdir -pv "${create_dir}"
  echo "created ${create_dir}"
}

# parse and set options
checkopts() {
  VERBOSE=""
  THREAD_NUM=8
  COVERAGE=""
  CMAKE_BUILD_TYPE="DT"
  ENABLE_CPP_TEST="on"
  ENABLE_PY_TEST="on"

  ASCEND_3RD_LIB_PATH="$BASEPATH/build_out/third_party"

  parsed_args=$(getopt -a -o t::cj:hv -l test::,cov,help,verbose,ascend_install_path:,ascend_3rd_lib_path: -- "$@") || {
    usage
    exit 1
  }

  eval set -- "$parsed_args"

  while true; do
    case "$1" in
      -t | --test)
        case "$2" in
          "")
            ENABLE_CPP_TEST="on"
            ENABLE_PY_TEST="on"
            shift 2
            ;;
          "cpp")
            ENABLE_CPP_TEST="on"
            ENABLE_PY_TEST="off"
            shift 2
            ;;
          "py")
            ENABLE_PY_TEST="on"
            ENABLE_CPP_TEST="off"
            shift 2
            ;;
          *)
            usage
            exit 1
        esac
        ;;
      -c | --cov)
        CMAKE_BUILD_TYPE="GCOV"
        shift
        ;;
      -h | --help)
        usage
        exit 1
        ;;
      -j)
        THREAD_NUM=$2
        shift 2
        ;;
      -v | --verbose)
        VERBOSE="-v"
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

build() {
  cd "${BUILD_PATH}"
  cmake -D ENABLE_TEST="on" \
        ${ASCEND_INSTALL_PATH:+-D ASCEND_INSTALL_PATH=${ASCEND_INSTALL_PATH}} \
        -D ASCEND_3RD_LIB_PATH=${ASCEND_3RD_LIB_PATH} \
        -D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
        -D CMAKE_INSTALL_PREFIX=${OUTPUT_PATH} \
        ..
  if [ $? -ne 0 ]
  then
    echo "execute command: cmake ${CMAKE_ARGS} .. failed."
    return 1
  fi
  make ${VERBOSE} -j${THREAD_NUM}

  if [ $? -ne 0 ]
  then
    echo "execute command: make ${VERBOSE} -j${THREAD_NUM} && make install failed."
    return 1
  fi
  make install
  echo "build success!"
}

run() {
  if [ -z "${OUTPUT_PATH}" ] ; then
    OUTPUT_PATH="${BASEPATH}/build_out"
  fi

  BUILD_RELATIVE_PATH="build_test"
  BUILD_PATH="${BASEPATH}/${BUILD_RELATIVE_PATH}/"
  USE_ASAN=$(gcc -print-file-name=libasan.so)

  g++ -v
  mk_dir ${OUTPUT_PATH}
  mk_dir ${BUILD_PATH}
  report_dir="${OUTPUT_PATH}/report"

  build || { echo "build failed."; exit 1; }
  echo "---------------- build finished ----------------"
  rm -f ${OUTPUT_PATH}/libgmock*.so
  rm -f ${OUTPUT_PATH}/libgtest*.so
  rm -f ${OUTPUT_PATH}/lib*_stub.so

  chmod -R 750 ${OUTPUT_PATH}
  find ${OUTPUT_PATH} -name "*.so*" -print0 | xargs -0 -r chmod 500

  echo "Run tests with leaks check"
  if [[ "X$ENABLE_CPP_TEST" = "Xon" ]]; then
      RUN_TEST_CASE="${BUILD_PATH}/tests/cpp/llm_datadist/llm_datadist_test --gtest_output=xml:${report_dir}/llm_datadist_test.xml" && ${RUN_TEST_CASE}
      if [[ "$?" -ne 0 ]]; then
          echo "!!! CPP TEST FAILED, PLEASE CHECK YOUR CHANGES !!!"
          echo -e "\033[31m${RUN_TEST_CASE}\033[0m"
          exit 1;
      fi
  fi

  if [[ "X$ENABLE_PY_TEST" = "Xon" ]]; then
      unset LD_PRELOAD
      cp ${BUILD_PATH}/tests/depends/python/llm_datadist_wrapper.so ${BASEPATH}/src/python/llm_datadist/llm_datadist/
      cp ${BUILD_PATH}/tests/depends/python/metadef_wrapper.so ${BASEPATH}/src/python/llm_datadist/llm_datadist/
      cp -r ${BASEPATH}/tests/python ./
      PYTHON_ORIGINAL_PATH=$PYTHONPATH
      export PYTHONPATH=${BASEPATH}/src/python/llm_datadist/:$PYTHON_ORIGINAL_PATH
      export LD_PRELOAD=${USE_ASAN}
      echo "----------st start----------"
      ASAN_OPTIONS=detect_leaks=0 coverage run -m unittest discover python
      if [[ "$?" -ne 0 ]]; then
          echo "!!! PY TEST FAILED, PLEASE CHECK YOUR CHANGES !!!"
          rm -f ${BASEPATH}/src/python/llm_datadist/llm_datadist/*.so
          exit 1;
      fi
      rm -f ${BASEPATH}/src/python/llm_datadist/llm_datadist/*.so
      unset LD_PRELOAD
  fi

  if [[ "X$CMAKE_BUILD_TYPE" = "XGCOV" ]]; then
      echo "Generating coverage statistics, please wait..."
      cd ${BASEPATH}
      rm -rf ${BASEPATH}/cov
      mk_dir ${BASEPATH}/cov
      if [[ "X$ENABLE_CPP_TEST" = "Xon" ]]; then
          lcov -c -d ${BUILD_PATH}/tests/cpp/llm_datadist/CMakeFiles/llm_datadist_test.dir \
                  -d ${BUILD_PATH}/tests/depends/python/CMakeFiles/llm_datadist_wrapper_stub.dir \
                  -d ${BUILD_PATH}/tests/depends/python/CMakeFiles/metadef_wrapper_stub.dir \
               -o cov/tmp.info
          lcov -e cov/tmp.info "${BASEPATH}/src/*" -o cov/coverage.info
          cd ${BASEPATH}/cov
          genhtml coverage.info
      fi

      if [[ "X$ENABLE_PY_TEST" = "Xon" ]]; then
          mv ${BUILD_PATH}/.coverage ${BASEPATH}/cov/
          cd ${BASEPATH}/cov
          coverage html -i --include="${BASEPATH}/src/*"
      fi
  fi
}

main() {
  cd "${BASEPATH}"
  checkopts "$@"
  if [ $? -ne 0 ]
  then
    echo "checkopts failed."
    return 1
  fi
  run || { echo "run failed."; return; }
}

main "$@"
