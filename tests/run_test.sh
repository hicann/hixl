#!/bin/bash
# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

set -e

BASEPATH=$(cd "$(dirname $0)/.."; pwd)

unset LD_LIBRARY_PATH
unset LD_PRELOAD

# print usage message
usage() {
  echo "Usage:"
  echo "sh run_test.sh [-c | --cov] [-j<N>] [-h | --help] [-v | --verbose]"
  echo "               [--cann_3rd_lib_path=<PATH> | --cann-3rd-lib-path=<PATH>] [--asan]"
  echo ""
  echo "Options:"
  echo "    -h, --help     Print usage"
  echo "    -t, --test     Test target (omit -t for cpp+py; bare -t same as all)"
  echo "        cpp                C++ tests only (llm_datadist + hixl + fabric_mem)"
  echo "        fabric_mem         fabric_mem_test only"
  echo "        py                 Python tests only"
  echo "        TYPE may be glued (-tfabric_mem) or spaced (-t fabric_mem)."
  echo "    -c, --cov      Build test with coverage tag"
  echo "                   Please ensure that the environment has correctly installed lcov, gcov, and genhtml."
  echo "                   and the version matched gcc/g++, default is OFF."
  echo "    -v, --verbose  Display build command"
  echo "    -j<N>          Set the number of threads used for building Parser, default 8"
  echo "        --cann_3rd_lib_path=<PATH> | --cann-3rd-lib-path=<PATH>"
  echo "                   Set ascend third_party package install path, default ./third_party"
  echo "    --asan         Enable AddressSanitizer, default is OFF. when cov is set, asan is set too."
  echo ""
}

mk_dir() {
  local create_dir="$1"  # the target to make
  mkdir -pv "${create_dir}"
  echo "created ${create_dir}"
}

set_test_type() {
  case "$1" in
    cpp)
      ENABLE_CPP_TEST=ON
      ENABLE_PY_TEST="off"
      ENABLE_FABRIC_MEM_ONLY=OFF
      ;;
    fabric_mem)
      ENABLE_CPP_TEST=ON
      ENABLE_PY_TEST="off"
      ENABLE_FABRIC_MEM_ONLY=ON
      ;;
    py)
      ENABLE_PY_TEST=ON
      ENABLE_CPP_TEST="off"
      ENABLE_FABRIC_MEM_ONLY=OFF
      ;;
    *)
      usage
      exit 1
      ;;
  esac
}

# parse and set options
checkopts() {
  VERBOSE=""
  THREAD_NUM=8
  COVERAGE=""
  CMAKE_BUILD_TYPE="DT"
  ENABLE_CPP_TEST=ON
  ENABLE_PY_TEST=ON
  ENABLE_FABRIC_MEM_ONLY=OFF
  ENABLE_ASAN=OFF
  ENABLE_GCOV=OFF
  TEST_TYPE_DEFERRED=0

  CANN_3RD_LIB_PATH="$BASEPATH/third_party"

  parsed_args=$(getopt -a -o t::cj:hv -l test::,cov,help,verbose,cann_3rd_lib_path:,cann-3rd-lib-path:,asan -- "$@") || {
    usage
    exit 1
  }

  eval set -- "$parsed_args"

  while true; do
    case "$1" in
      -t | --test)
        case "$2" in
          "")
            ENABLE_CPP_TEST=ON
            ENABLE_PY_TEST=ON
            ENABLE_FABRIC_MEM_ONLY=OFF
            TEST_TYPE_DEFERRED=1
            shift 2
            ;;
          *)
            set_test_type "$2"
            shift 2
            ;;
        esac
        ;;
      -c | --cov)
        ENABLE_GCOV=ON
        # keep set asan for legacy
        ENABLE_ASAN=ON
        shift
        ;;
      --asan)
        ENABLE_ASAN=ON
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
      --cann_3rd_lib_path | --cann-3rd-lib-path)
        CANN_3RD_LIB_PATH="$(realpath $2)"
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

  # GNU getopt optional-arg quirk: "-t fabric_mem" leaves TYPE as a positional arg.
  if [[ $# -gt 0 ]]; then
    if [[ "${TEST_TYPE_DEFERRED}" -eq 1 ]]; then
      set_test_type "$1"
      shift
    else
      echo "Unexpected argument(s): $*"
      usage
      exit 1
    fi
  fi
  if [[ $# -gt 0 ]]; then
    echo "Unexpected argument(s): $*"
    usage
    exit 1
  fi
}

build() {
  cd "${BUILD_PATH}"
  cmake -D ENABLE_TEST=ON \
        -D ENABLE_ASAN=${ENABLE_ASAN} \
        -D ENABLE_GCOV=${ENABLE_GCOV} \
        -D CANN_3RD_LIB_PATH=${CANN_3RD_LIB_PATH} \
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
  if [[ "X$ENABLE_CPP_TEST" = "XON" ]]; then
      run_cpp_test() {
          local test_bin="$1"
          local report_name="$2"
          local run_cmd="${test_bin} --gtest_output=xml:${report_dir}/${report_name}"
          echo "Run: ${run_cmd}"
          ${run_cmd}
      }

      if [[ "X$ENABLE_FABRIC_MEM_ONLY" = "XON" ]]; then
          run_cpp_test "${BUILD_PATH}/tests/cpp/hixl/fabric_mem/fabric_mem_test" "fabric_mem_test.xml" || {
              echo "!!! CPP TEST FAILED, PLEASE CHECK YOUR CHANGES !!!"
              echo -e "\033[31m${BUILD_PATH}/tests/cpp/hixl/fabric_mem/fabric_mem_test\033[0m"
              exit 1;
          }
      else
          run_cpp_test "${BUILD_PATH}/tests/cpp/llm_datadist/llm_datadist_test" "llm_datadist_test.xml" || {
              echo "!!! CPP TEST FAILED, PLEASE CHECK YOUR CHANGES !!!"
              echo -e "\033[31m${BUILD_PATH}/tests/cpp/llm_datadist/llm_datadist_test\033[0m"
              exit 1;
          }
          HIXL_PARALLEL_TEST_PIDS=()
          HIXL_PARALLEL_TEST_CMDS=()
          HIXL_PARALLEL_TEST_LOGS=()

          run_cpp_test_parallel() {
              local test_bin="$1"
              local report_name="$2"
              local run_cmd="${test_bin} --gtest_output=xml:${report_dir}/${report_name}"
              local log_file="${report_dir}/${report_name%.xml}.log"
              echo "Run (parallel): ${run_cmd} (log: ${log_file})"
              ${run_cmd} > "${log_file}" 2>&1 &
              HIXL_PARALLEL_TEST_PIDS+=("$!")
              HIXL_PARALLEL_TEST_CMDS+=("${run_cmd}")
              HIXL_PARALLEL_TEST_LOGS+=("${log_file}")
          }

          run_cpp_test_parallel "${BUILD_PATH}/tests/cpp/hixl/hixl_test" "hixl_test.xml"
          run_cpp_test_parallel "${BUILD_PATH}/tests/cpp/hixl/fabric_mem/fabric_mem_test" "fabric_mem_test.xml"

          HIXL_PARALLEL_FAILED=0
          for idx in "${!HIXL_PARALLEL_TEST_PIDS[@]}"; do
              # Each run is redirected to its own log to avoid interleaved parallel output; replay it here.
              wait "${HIXL_PARALLEL_TEST_PIDS[$idx]}" && wait_ret=0 || wait_ret=1
              echo "===== Output: ${HIXL_PARALLEL_TEST_CMDS[$idx]} ====="
              cat "${HIXL_PARALLEL_TEST_LOGS[$idx]}"
              if [[ "${wait_ret}" -ne 0 ]]; then
                  HIXL_PARALLEL_FAILED=1
                  echo "!!! CPP TEST FAILED, PLEASE CHECK YOUR CHANGES !!!"
                  echo -e "\033[31m${HIXL_PARALLEL_TEST_CMDS[$idx]} (log: ${HIXL_PARALLEL_TEST_LOGS[$idx]})\033[0m"
              fi
          done
          if [[ "${HIXL_PARALLEL_FAILED}" -ne 0 ]]; then
              exit 1;
          fi
      fi
  fi

  if [[ "X$ENABLE_PY_TEST" = "XON" ]]; then
      unset LD_PRELOAD
      cp ${BUILD_PATH}/tests/depends/python/llm_datadist_wrapper.so ${BASEPATH}/src/python/llm_datadist/llm_datadist/
      cp ${BUILD_PATH}/tests/depends/python/metadef_wrapper.so ${BASEPATH}/src/python/llm_datadist/llm_datadist/
      cp -r ${BASEPATH}/tests/python ./
      PYTHON_ORIGINAL_PATH=$PYTHONPATH
      export PYTHONPATH=${BASEPATH}/src/python/llm_datadist/

      echo "----------st start----------"
      if [[ "X$ENABLE_ASAN" = "XON" ]]; then
        export LD_PRELOAD=${USE_ASAN}
        ASAN_OPTIONS=detect_leaks=0 coverage run -m unittest discover python
      else
        coverage run -m unittest discover python
      fi
      if [[ "$?" -ne 0 ]]; then
          echo "!!! PY TEST FAILED, PLEASE CHECK YOUR CHANGES !!!"
          rm -f ${BASEPATH}/src/python/llm_datadist/llm_datadist/*.so
          exit 1;
      fi
      rm -f ${BASEPATH}/src/python/llm_datadist/llm_datadist/*.so

      if [[ "X$ENABLE_ASAN" = "XON" ]]; then
        unset LD_PRELOAD
      fi
      export PYTHONPATH=${PYTHON_ORIGINAL_PATH}
  fi

  if [[ "X$ENABLE_GCOV" = "XON" ]]; then
      echo "Generating coverage statistics, please wait..."
      cd ${BASEPATH}
      rm -rf ${BASEPATH}/cov
      mk_dir ${BASEPATH}/cov

      # Detect lcov version and set appropriate ignore errors flags
      detect_lcov_flags() {
          LCOV_VERSION=$(lcov --version 2>/dev/null | head -n1 | sed 's/.*LCOV version //' | cut -d. -f1)
          if [[ "${LCOV_VERSION}" -ge 2 ]]; then
              LCOV_IGNORE_FLAGS="--ignore-errors empty,negative,mismatch,corrupt"
          else
              LCOV_IGNORE_FLAGS=""
          fi
      }

      if [[ "X$ENABLE_CPP_TEST" = "XON" ]]; then
          detect_lcov_flags
          if [[ "X$ENABLE_FABRIC_MEM_ONLY" = "XON" ]]; then
              lcov -c ${LCOV_IGNORE_FLAGS} \
                      -d ${BUILD_PATH}/tests/cpp/hixl/fabric_mem/CMakeFiles/fabric_mem_test.dir \
                   -o cov/tmp.info
          else
              lcov -c ${LCOV_IGNORE_FLAGS} -d ${BUILD_PATH}/tests/cpp/llm_datadist/CMakeFiles/llm_datadist_test.dir \
                      -d ${BUILD_PATH}/tests/cpp/hixl/CMakeFiles/hixl_test.dir \
                      -d ${BUILD_PATH}/tests/cpp/hixl/fabric_mem/CMakeFiles/fabric_mem_test.dir \
                      -d ${BUILD_PATH}/tests/depends/python/CMakeFiles/llm_datadist_wrapper_stub.dir \
                      -d ${BUILD_PATH}/tests/depends/python/CMakeFiles/metadef_wrapper_stub.dir \
                   -o cov/tmp.info
          fi
          lcov -e cov/tmp.info "${BASEPATH}/src/*" -o cov/coverage.info
          cd ${BASEPATH}/cov
          genhtml coverage.info
      fi

      if [[ "X$ENABLE_PY_TEST" = "XON" ]]; then
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
