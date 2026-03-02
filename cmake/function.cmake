# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

#[[
  module - the name of export imported target
  name   - find the library name
  path   - find the library path
#]]

set(_FUNC_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(_ROOT_DIR "${_FUNC_CMAKE_DIR}/..")

function(find_module module name)
    if (TARGET ${module})
        return()
    endif ()

    set(options)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(MODULE "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    set(path ${MODULE_UNPARSED_ARGUMENTS})
    unset(${module}_LIBRARY_DIR CACHE)
    find_library(${module}_LIBRARY_DIR NAMES ${name} NAMES_PER_DIR PATHS ${path}
            PATH_SUFFIXES lib
            )

    message(STATUS "find ${name} location ${${module}_LIBRARY_DIR}")
    if ("${${module}_LIBRARY_DIR}" STREQUAL "${module}_LIBRARY_DIR-NOTFOUND")
        message(FATAL_ERROR "${name} not found in ${path}")
    endif ()

    add_library(${module} SHARED IMPORTED)
    set_target_properties(${module} PROPERTIES
            IMPORTED_LOCATION ${${module}_LIBRARY_DIR}
            )
endfunction()

function(protobuf_generate comp c_var h_var)
    if (NOT ARGN)
        message(SEND_ERROR "Error: protobuf_generate() called without any proto files")
        return()
    endif ()
    set(${c_var})
    set(${h_var})
    set(_add_target FALSE)

    set(extra_option "")
    foreach (arg ${ARGN})
        if ("${arg}" MATCHES "--proto_path")
            set(extra_option ${arg})
        endif ()
        if ("${arg}" STREQUAL "TARGET")
            set(_add_target TRUE)
        endif ()
    endforeach ()

    foreach (file ${ARGN})
        if ("${file}" MATCHES "--proto_path")
            continue()
        endif ()
        if ("${file}" STREQUAL "TARGET")
            continue()
        endif ()

        get_filename_component(abs_file ${file} ABSOLUTE)
        get_filename_component(file_name ${file} NAME_WE)
        get_filename_component(file_dir ${abs_file} PATH)
        get_filename_component(parent_subdir ${file_dir} NAME)

        if ("${parent_subdir}" STREQUAL "proto")
            set(proto_output_path ${CMAKE_BINARY_DIR}/proto/${comp}/proto)
        else ()
            set(proto_output_path ${CMAKE_BINARY_DIR}/proto/${comp}/proto/${parent_subdir})
        endif ()
        list(APPEND ${c_var} "${proto_output_path}/${file_name}.pb.cc")
        list(APPEND ${h_var} "${proto_output_path}/${file_name}.pb.h")

        add_custom_command(
                OUTPUT "${proto_output_path}/${file_name}.pb.cc" "${proto_output_path}/${file_name}.pb.h"
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${proto_output_path}"
                COMMAND ${CMAKE_COMMAND} -E echo "generate proto cpp_out ${comp} by ${abs_file}"
                COMMAND ${PROTOC_PROGRAM} -I${file_dir} ${extra_option} --cpp_out=${proto_output_path} ${abs_file}
                DEPENDS ${abs_file}
                COMMENT "Running C++ protocol buffer compiler on ${file}" VERBATIM )
    endforeach ()

    if (_add_target)
        add_custom_target(
            ${comp} DEPENDS ${${c_var}} ${${h_var}}
        )
    endif ()

    set_source_files_properties(${${c_var}} ${${h_var}} PROPERTIES GENERATED TRUE)
    set(${c_var} ${${c_var}} PARENT_SCOPE)
    set(${h_var} ${${h_var}} PARENT_SCOPE)
endfunction()

function(protobuf_generate_grpc comp c_var h_var)
    if (NOT ARGN)
        MESSAGE(SEND_ERROR "Error:protobuf_generate_grpc() called without any proto files")
        return()
    endif ()
    set(${c_var})
    set(${h_var})

    set(extra_option "")
    foreach (arg ${ARGN})
        if ("${arg}" MATCHES "--proto_path")
            set(extra_option ${arg})
        endif ()
    endforeach ()

    foreach (file ${ARGN})
        if ("${file}" MATCHES "--proto_path")
            continue()
        endif ()
        get_filename_component(abs_file ${file} ABSOLUTE)
        get_filename_component(file_name ${file} NAME_WE)
        get_filename_component(file_dir ${abs_file} PATH)
        get_filename_component(parent_subdir ${file_dir} NAME)
        if ("${parent_subdir}" STREQUAL "proto")
            set(proto_output_path ${CMAKE_BINARY_DIR}/proto_grpc/${comp}/proto)
        else ()
            set(proto_output_path ${CMAKE_BINARY_DIR}/proto_grpc/${comp}/proto_grpc/${parent_subdir})
        endif ()
        list(APPEND ${c_var} "${proto_output_path}/${file_name}.pb.cc")
        list(APPEND ${c_var} "${proto_output_path}/${file_name}.grpc.pb.cc")
        list(APPEND ${h_var} "${proto_output_path}/${file_name}.pb.h")
        list(APPEND ${h_var} "${proto_output_path}/${file_name}.grpc.pb.h")

        add_custom_command(
                OUTPUT "${proto_output_path}/${file_name}.pb.cc" "${proto_output_path}/${file_name}.pb.h"
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${proto_output_path}"
                COMMAND ${PROTOC_PROGRAM} -I${file_dir} ${extra_option} --cpp_out=${proto_output_path} ${abs_file}
                COMMAND ${PROTOC_PROGRAM} -I${file_dir} ${extra_option} --grpc_out=${proto_output_path} --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_PROGRAM} ${abs_file}
                DEPENDS ${abs_file}
                COMMENT "Running C++ protocol buffer complier on ${file}" VERBATIM)
    endforeach ()

    set_source_files_properties(${${c_var}} ${${h_var}} PROPERTIES GENERATED TRUE)
    set(${c_var} ${${c_var}} PARENT_SCOPE)
    set(${h_var} ${${h_var}} PARENT_SCOPE)
endfunction()

function(protobuf_generate_py comp py_var)
    if (NOT ARGN)
        message(SEND_ERROR "Error: protobuf_generate_py() called without any proto files")
        return()
    endif ()
    set(${py_var})
    set(_add_target FALSE)

    foreach (file ${ARGN})
        if ("${file}" STREQUAL "TARGET")
            set(_add_target TRUE)
            continue()
        endif ()

        get_filename_component(abs_file ${file} ABSOLUTE)
        get_filename_component(file_name ${file} NAME_WE)
        get_filename_component(file_dir ${abs_file} PATH)
        get_filename_component(parent_subdir ${file_dir} NAME)

        if ("${parent_subdir}" STREQUAL "proto")
            set(proto_output_path ${CMAKE_BINARY_DIR}/proto/${comp}/proto)
        else ()
            set(proto_output_path ${CMAKE_BINARY_DIR}/proto/${comp}/proto/${parent_subdir})
        endif ()
        list(APPEND ${py_var} "${proto_output_path}/${file_name}_pb2.py")

        add_custom_command(
                OUTPUT "${proto_output_path}/${file_name}_pb2.py"
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${proto_output_path}"
                COMMAND ${CMAKE_COMMAND} -E echo "generate proto cpp_out ${comp} by ${abs_file}"
                COMMAND ${PROTOC_PROGRAM} -I${file_dir} --python_out=${proto_output_path} ${abs_file}
                DEPENDS ${abs_file}
                COMMENT "Running PYTHON protocol buffer compiler on ${file}" VERBATIM )
    endforeach ()

    if (_add_target)
        add_custom_target(
            ${comp} DEPENDS ${${py_var}}
        )
    endif ()

    set_source_files_properties(${${py_var}} PROPERTIES GENERATED TRUE)
    set(${py_var} ${${py_var}} PARENT_SCOPE)
endfunction()

macro(find_package_if_target_not_exists pkg)
    if (TARGET ${pkg})
        message(STATUS "SN_DEBUG package ${pkg} target exists")
    else ()
        find_package(${pkg} ${ARGN})
    endif ()
endmacro()

function(stub_module module stub_name)
    if (TARGET ${module})
        return()
    endif( )
    add_library(${module} INTERFACE)
    target_link_libraries(${module} INTERFACE ${stub_name})
endfunction()

macro(replace_cur_major_minor_ver)
    string(REPLACE CUR_MAJOR_MINOR_VER "${CANN_VERSION_${CANN_VERSION_CURRENT_PACKAGE}_VERSION_MAJOR_MINOR}" depend "${depend}")
endmacro()

# 设置包和版本号
function(set_package name)
    cmake_parse_arguments(VERSION "" "VERSION" "" ${ARGN})
    set(VERSION "${VERSION_VERSION}")
    if(NOT name)
        message(FATAL_ERROR "The name parameter is not set in set_package.")
    endif()
    if(NOT VERSION)
        message(FATAL_ERROR "The VERSION parameter is not set in set_package(${name}).")
    endif()
    string(REGEX MATCH "^([0-9]+\\.[0-9]+)" VERSION_MAJOR_MINOR "${VERSION}")
    list(APPEND CANN_VERSION_PACKAGES "${name}")
    set(CANN_VERSION_PACKAGES "${CANN_VERSION_PACKAGES}" PARENT_SCOPE)
    set(CANN_VERSION_CURRENT_PACKAGE "${name}" PARENT_SCOPE)
    set(CANN_VERSION_${name}_VERSION "${VERSION}" PARENT_SCOPE)
    set(CANN_VERSION_${name}_VERSION_MAJOR_MINOR "${VERSION_MAJOR_MINOR}" PARENT_SCOPE)
    set(CANN_VERSION_${name}_BUILD_DEPS PARENT_SCOPE)
    set(CANN_VERSION_${name}_RUN_DEPS PARENT_SCOPE)
endfunction()

# 设置构建依赖
function(set_build_dependencies pkg_name depend)
    if(NOT CANN_VERSION_CURRENT_PACKAGE)
        message(FATAL_ERROR "The set_package must be invoked first.")
    endif()
    if(NOT pkg_name)
        message(FATAL_ERROR "The pkg_name parameter is not set in set_build_dependencies.")
    endif()
    if(NOT depend)
        message(FATAL_ERROR "The depend parameter is not set in set_build_dependencies.")
    endif()
    replace_cur_major_minor_ver()
    list(APPEND CANN_VERSION_${CANN_VERSION_CURRENT_PACKAGE}_BUILD_DEPS "${pkg_name}" "${depend}")
    set(CANN_VERSION_${CANN_VERSION_CURRENT_PACKAGE}_BUILD_DEPS "${CANN_VERSION_${CANN_VERSION_CURRENT_PACKAGE}_BUILD_DEPS}" PARENT_SCOPE)
endfunction()

# 设置运行依赖
function(set_run_dependencies pkg_name depend)
    if(NOT CANN_VERSION_CURRENT_PACKAGE)
        message(FATAL_ERROR "The set_package must be invoked first.")
    endif()
    if(NOT pkg_name)
        message(FATAL_ERROR "The pkg_name parameter is not set in set_run_dependencies.")
    endif()
    if(NOT depend)
        message(FATAL_ERROR "The depend parameter is not set in set_run_dependencies.")
    endif()
    replace_cur_major_minor_ver()
    list(APPEND CANN_VERSION_${CANN_VERSION_CURRENT_PACKAGE}_RUN_DEPS "${pkg_name}" "${depend}")
    set(CANN_VERSION_${CANN_VERSION_CURRENT_PACKAGE}_RUN_DEPS "${CANN_VERSION_${CANN_VERSION_CURRENT_PACKAGE}_RUN_DEPS}" PARENT_SCOPE)
endfunction()

# 检查构建依赖
function(check_pkg_build_deps pkg_name)
    execute_process(
        COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_build_dependencies.py "${CANN_INSTALL_PATH}" ${CANN_VERSION_${pkg_name}_BUILD_DEPS}
        RESULT_VARIABLE result
    )
    if(result)
        message(FATAL_ERROR "Check ${pkg_name} build dependencies failed!")
    endif()
endfunction()

# 添加生成version.info的目标
# 目标名格式为：version_${包名}_info
function(add_version_info_targets)
    foreach(pkg_name ${CANN_VERSION_PACKAGES})
        add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/version.${pkg_name}.info
            COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_version_info.py --output ${CMAKE_BINARY_DIR}/version.${pkg_name}.info
                    "${CANN_VERSION_${pkg_name}_VERSION}" ${CANN_VERSION_${pkg_name}_RUN_DEPS}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_version_info.py
            VERBATIM
        )
        add_custom_target(version_${pkg_name}_info ALL DEPENDS ${CMAKE_BINARY_DIR}/version.${pkg_name}.info)
    endforeach()
endfunction()

function(pack_targets_and_files)
    cmake_parse_arguments(ARG
        ""
        "OUTPUT;MANIFEST;OUTPUT_TARGET"
        "TARGETS;FILES"
        ${ARGN}
    )

    # --- Validation ---
    if(NOT ARG_OUTPUT)
        message(FATAL_ERROR "[pack_targets_and_files] OUTPUT is required")
    endif()

    if(NOT IS_ABSOLUTE "${ARG_OUTPUT}")
        set(ARG_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${ARG_OUTPUT}")
    endif()

    if(NOT ARG_OUTPUT_TARGET)
        message(FATAL_ERROR "[pack_targets_and_files] OUTPUT_TARGET is required")
    endif()

    # Generate safe target name
    get_filename_component(tar_basename "${ARG_OUTPUT}" NAME_WE)
    string(MAKE_C_IDENTIFIER "pack_${tar_basename}" safe_name)
    set(staging_dir "${CMAKE_CURRENT_BINARY_DIR}/_${safe_name}_stage")

    # --- Collect all source items (as generator expressions) ---
    set(src_items "")
    foreach(tgt IN LISTS ARG_TARGETS)
        if(NOT TARGET ${tgt})
            message(FATAL_ERROR "[pack_targets_and_files] Target '${tgt}' does not exist")
        endif()

        get_target_property(type ${tgt} TYPE)
        if(type MATCHES "^(EXECUTABLE|SHARED_LIBRARY|STATIC_LIBRARY)$")
            list(APPEND src_items "$<TARGET_FILE:${tgt}>")
        endif()
    endforeach()
    list(APPEND src_items ${ARG_FILES})

    if(NOT src_items)
        message(FATAL_ERROR "[pack_targets_and_files] No targets or files specified to pack")
    endif()

    set(manifest_arg "")
    if(ARG_MANIFEST)
        if("${ARG_MANIFEST}" STREQUAL "")
            message(FATAL_ERROR "[pack] MANIFEST filename cannot be empty")
        endif()
        if(IS_ABSOLUTE "${ARG_MANIFEST}")
            message(FATAL_ERROR "[pack] MANIFEST must be relative (e.g., 'sha256sums.cfg')")
        endif()
        set(manifest_arg -D_MANIFEST_FILE=${staging_dir}/${ARG_MANIFEST})
    endif()

    add_custom_command(
        OUTPUT ${staging_dir}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${staging_dir}"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${ARG_OUTPUT}"
        COMMAND ${CMAKE_COMMAND}
            -D _STAGING_DIR=${staging_dir}
            ${manifest_arg}
            -D "_ITEMS=$<JOIN:${src_items},;>"
            -P "${_FUNC_CMAKE_DIR}/_pack_stage.cmake"
        COMMAND tar "czf" "${ARG_OUTPUT}" . --transform "s/^/aicpu_kernels_device\\//"
                "--mode=750"
        WORKING_DIRECTORY ${staging_dir}
        DEPENDS ${ARG_TARGETS} ${staging_dir}
        COMMENT "Packing with ${ARG_OUTPUT}"
        VERBATIM
    )

    add_custom_target(${ARG_OUTPUT_TARGET} ALL DEPENDS "${ARG_OUTPUT}")
endfunction()

function(sign_file)
    cmake_parse_arguments(
        ARG
        ""
        "OUTPUT_TARGET;INPUT;CONFIG;RESULT_VAR"
        "SCRIPT_ARGS;DEPENDS"
        ${ARGN}
    )

    # --- Validation ---
    message(STATUS "signing script: ${CUSTOM_SIGN_SCRIPT}")
    if(DEFINED CUSTOM_SIGN_SCRIPT AND NOT CUSTOM_SIGN_SCRIPT STREQUAL "")
        set(SIGN_SCRIPT ${CUSTOM_SIGN_SCRIPT})
    else()
        set(SIGN_SCRIPT)
    endif()

    if(ENABLE_SIGN)
        set(sign_flag "true")
    else()
        set(sign_flag "false")
    endif()

    foreach(var INPUT CONFIG RESULT_VAR)
        if(NOT ARG_${var})
            message(FATAL_ERROR "[sign_file] Missing required: ${var}")
        endif()
    endforeach()

    if(NOT EXISTS "${ARG_CONFIG}")
        message(FATAL_ERROR "[sign_file] Sign config not found: ${ARG_CONFIG}")
    endif()

    # Normalize input
    if(NOT IS_ABSOLUTE "${ARG_INPUT}")
        set(ARG_INPUT "${CMAKE_CURRENT_BINARY_DIR}/${ARG_INPUT}")
    endif()

    # Auto output path: ${CMAKE_CURRENT_BINARY_DIR}/signatures
    set(signatures_dir "${CMAKE_CURRENT_BINARY_DIR}/signatures")
    get_filename_component(input_name "${ARG_INPUT}" NAME)
    set(output_sig "${signatures_dir}/${input_name}")

    if(EXISTS "${SIGN_SCRIPT}")
        get_filename_component(EXT ${SIGN_SCRIPT} EXT) # 获取文件扩展名

        if (${EXT} STREQUAL ".sh")
            set(sign_cmd bash ${SIGN_SCRIPT} ${output_sig} ${ARG_CONFIG} ${sign_flag})
            message(STATUS ${sign_cmd})
        elseif(${EXT} STREQUAL ".py")
            message(STATUS "Detected +++VERSION_INFO:${CANN_VERSION_hixl_VERSION}, _ROOT_DIR:${_ROOT_DIR}")
            set(sign_cmd python3 ${_ROOT_DIR}/scripts/sign/add_header_sign.py ${signatures_dir} ${sign_flag} --bios_check_cfg=${ARG_CONFIG} --sign_script=${SIGN_SCRIPT} --version=${CANN_VERSION_hixl_VERSION})
        endif()
    else()
        set(sign_cmd )
    endif()

    # Ensure dir exists
    file(MAKE_DIRECTORY "${signatures_dir}")

    # Target name
    get_filename_component(sign_basename "${ARG_INPUT}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${sign_basename}" safe_name)

    if(ARG_OUTPUT_TARGET)
        set(sign_target "${ARG_OUTPUT_TARGET}")
    else()
        set(sign_target "sign_${safe_name}")
    endif()

    add_custom_command(
        OUTPUT "${output_sig}"
        COMMAND ${CMAKE_COMMAND} -E make_directory ${signatures_dir}
        COMMAND ${CMAKE_COMMAND} -E copy ${ARG_INPUT} ${output_sig}
        COMMAND ${sign_cmd}
        DEPENDS "${ARG_INPUT}" "${SIGN_SCRIPT}" ${ARG_DEPENDS} ${ARG_CONFIG}
        COMMENT "Signing: ${ARG_INPUT} → ${output_sig}"
        VERBATIM
    )

    add_custom_target(${sign_target} ALL DEPENDS "${output_sig}")

    # Return path via RESULT_VAR
    if(ARG_RESULT_VAR)
        set(${ARG_RESULT_VAR} "${output_sig}" PARENT_SCOPE)
    endif()
endfunction()

function(generate_stub_with_output_name STUB STUB_OUTPUT_NAME)
    if(EXISTS ${DOWNLOAD_LIB_DIR}/lib${STUB_OUTPUT_NAME}.so)
        add_library(${STUB} SHARED IMPORTED GLOBAL)
        set_target_properties(${STUB} PROPERTIES
            IMPORTED_LOCATION "${DOWNLOAD_LIB_DIR}/lib${STUB_OUTPUT_NAME}.so"
            INTERFACE_LINK_OPTIONS "-Wl,-rpath-link=${DOWNLOAD_LIB_DIR}"
        )
        message(STATUS "Imported library lib${STUB_OUTPUT_NAME}.so")
    else()
        string(FIND ${STUB_OUTPUT_NAME} "::" temp)
        if (temp EQUAL "-1")
            set(target_plain_name ${STUB_OUTPUT_NAME})
        else()
            string(REPLACE "::" ";" temp_list ${STUB_OUTPUT_NAME})
            list(GET temp_list 1 target_plain_name)
        endif()

        if (NOT TARGET ${target_plain_name}_stub_tmp)
            add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/stub/${target_plain_name}.c
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/stub
                COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/stub/${target_plain_name}.c)
            add_library(${target_plain_name}_stub_tmp SHARED ${CMAKE_CURRENT_BINARY_DIR}/stub/${target_plain_name}.c)
            set_target_properties(${target_plain_name}_stub_tmp PROPERTIES
                WINDOWS_EXPORT_ALL_SYMBOLS TRUE
                LIBRARY_OUTPUT_NAME ${target_plain_name}
                RUNTIME_OUTPUT_NAME ${target_plain_name}
                LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/stub
                RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/stub)
        endif()

        add_library(${STUB} SHARED IMPORTED GLOBAL)
        if (UNIX)
            set_target_properties(${STUB} PROPERTIES
                IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/stub/lib${target_plain_name}.so")
        endif()
        if (WIN32)
            set_target_properties(${STUB} PROPERTIES
                IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/stub/${target_plain_name}.dll"
                IMPORTED_IMPLIB "${CMAKE_CURRENT_BINARY_DIR}/stub/${target_plain_name}.lib")
        endif()
        add_dependencies(${STUB} ${target_plain_name}_stub_tmp)

        message(STATUS "Stub library lib${STUB_OUTPUT_NAME}.so")
    endif()
endfunction()

function(generate_stub STUB)
    if(DEFINED STUB_OUTPUT_NAME_${STUB})
        set(STUB_OUTPUT_NAME ${STUB_OUTPUT_NAME_${STUB}})
    else()
        set(STUB_OUTPUT_NAME ${STUB})
    endif()

    generate_stub_with_output_name(${STUB} ${STUB_OUTPUT_NAME})

    if(DEFINED STUB_LINK_LIBRARIES_${STUB})
        foreach(LIB ${STUB_LINK_LIBRARIES_${STUB}})
            if(TARGET ${LIB})
                target_link_libraries(${STUB} INTERFACE ${LIB})
            endif()
        endforeach()
    endif()
endfunction(generate_stub)