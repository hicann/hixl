# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

#[[
  module - the name of export imported target
  name   - find the library name
  path   - find the library path
#]]

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
                COMMENT "Running C++ protocol buffer compiler on ${file}" VERBATIM)
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
        find_cann_package(${pkg} ${ARGN})
    endif ()
endmacro()

function(stub_module module stub_name)
    if (TARGET ${module})
        return()
    endif( )
    add_library(${module} INTERFACE)
    target_link_libraries(${module} INTERFACE ${stub_name})
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
