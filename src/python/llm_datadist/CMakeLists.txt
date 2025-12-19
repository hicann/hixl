# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

add_custom_target(llm_datadist_python ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/llm_datadist-0.0.1-py3-none-any.whl)
add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/llm_datadist-0.0.1-py3-none-any.whl
        COMMAND echo "package llm datadist whl start"
        && mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/wheel1
        && cp -r ${CMAKE_CURRENT_SOURCE_DIR}/setup.py ${CMAKE_CURRENT_BINARY_DIR}/wheel1/setup.py
        && cp -r ${CMAKE_CURRENT_SOURCE_DIR}/MANIFEST.in ${CMAKE_CURRENT_BINARY_DIR}/wheel1/
        && cp -r ${CMAKE_CURRENT_SOURCE_DIR}/llm_datadist ${CMAKE_CURRENT_BINARY_DIR}/wheel1/
        && cp -r ${CMAKE_CURRENT_BINARY_DIR}/../llm_wrapper/llm_datadist_wrapper.so ${CMAKE_CURRENT_BINARY_DIR}/wheel1/llm_datadist/
        && cp -r ${CMAKE_CURRENT_BINARY_DIR}/../metadef_wrapper/metadef_wrapper.so ${CMAKE_CURRENT_BINARY_DIR}/wheel1/llm_datadist/
        && cd ${CMAKE_CURRENT_BINARY_DIR}/wheel1
        && ${HI_PYTHON} setup.py bdist_wheel >/dev/null
        && cp -f dist/llm_datadist-0.0.1-py3-none-any.whl ${CMAKE_CURRENT_BINARY_DIR}/
        && echo "package llm datadist whl end"
        DEPENDS generate_hixl_version_info llm_datadist_wrapper metadef_wrapper
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/llm_datadist-0.0.1-py3-none-any.whl OPTIONAL
        DESTINATION ${INSTALL_LIBRARY_DIR}
)