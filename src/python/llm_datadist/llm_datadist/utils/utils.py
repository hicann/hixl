#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import ctypes

_MAX_UINT8 = ctypes.c_uint8(2**8 - 1).value
_MAX_UINT16 = ctypes.c_uint16(2**16 - 1).value
_MAX_INT32 = ctypes.c_int32(2**31 - 1).value
_MIN_INT32 = ctypes.c_int32(0 - 2**31).value
_MAX_UINT32 = ctypes.c_uint32(2**32 - 1).value
_MAX_INT64 = ctypes.c_int64(2**63 - 1).value
_MIN_INT64 = ctypes.c_int64(0 - 2**63).value
_MAX_UINT64 = ctypes.c_uint64(2**64 - 1).value


def check_inner(arg_name, arg_value, inner_class):
    for val in arg_value:
        if not isinstance(val, inner_class):
            raise TypeError(f"{arg_name} inner type only support {[inner_class]}, but got {format(type(val))}.")


def check_dict(arg_name, arg_value: dict, k_class, v_class, v_inner_class=None):
    for k, v in arg_value.items():
        if not isinstance(k, k_class):
            raise TypeError(
                f"{arg_name} dict key only support {k_class}, but got {format(type(k))}.")
        if not isinstance(v, v_class):
            raise TypeError(
                f"{arg_name} dict value only support {v_class}, but got {format(type(v))}.")
        elif v_inner_class is not None:
            check_inner("dict value", v, v_inner_class)


def check_isinstance(arg_name, arg_value, classes, inner_class=None, extra_fmt="", allow_none: bool = True):
    if allow_none and arg_value is None:
        return arg_value
    if not isinstance(classes, list):
        classes = [classes]
    check = False
    for clazz in classes:
        if isinstance(arg_value, clazz):
            if inner_class:
                check_inner(arg_name, arg_value, inner_class)
            check = True
            break
    if not check:
        raise TypeError(f"{extra_fmt}{arg_name} only support {[clazz.__name__ for clazz in classes]}, "
                        f"but got {format(type(arg_value))}.")
    return arg_value


def check_uint64(arg_name, arg_value):
    check_isinstance(arg_name, arg_value, [int], allow_none=False)
    if arg_value < 0 or arg_value > _MAX_UINT64:
        raise ValueError(f"{arg_name}'s value is out of range,{arg_value} is not a uint64 value.")


def check_int64(arg_name, arg_value):
    check_isinstance(arg_name, arg_value, [int], allow_none=False)
    if arg_value < _MIN_INT64 or arg_value > _MAX_INT64:
        raise ValueError(f"{arg_name}'s value is out of range,{arg_value} is not a int64 value.")


def check_positive_or_set_default(arg_name, arg_value, default=-1):
    if arg_value is not None:
        check_int64(arg_name, arg_value)
        if arg_value < 0:
            raise ValueError(f"{arg_name}'s value:{arg_value} is invalid, should be >= 0")
    else:
        arg_value = default
    return arg_value


def check_int32(arg_name, arg_value):
    check_isinstance(arg_name, arg_value, [int], allow_none=False)
    if arg_value < _MIN_INT32 or arg_value > _MAX_INT32:
        raise ValueError(f"{arg_name}'s value is out of range,{arg_value} is not a int32 value.")


def check_uint32(arg_name, arg_value):
    check_isinstance(arg_name, arg_value, [int], allow_none=False)
    if arg_value < 0 or arg_value > _MAX_UINT32:
        raise ValueError(f"{arg_name}'s value is out of range,{arg_value} is not a uint32 value.")


def check_list_int32(arg_name, arg_value):
    for value in arg_value:
        check_int32(arg_name, value)


def check_list_int64(arg_name, arg_value):
    for value in arg_value:
        check_int64(arg_name, value)


def check_list_uint64(arg_name, arg_value):
    for value in arg_value:
        check_uint64(arg_name, value)


def check_uint16(arg_name, arg_value):
    check_isinstance(arg_name, arg_value, [int], allow_none=False)
    if arg_value < 0 or arg_value > _MAX_UINT16:
        raise ValueError(f"{arg_name}'s value is out of range,{arg_value} is not a uint16 value.")


def check_uint8(arg_name, arg_value):
    check_isinstance(arg_name, arg_value, [int], allow_none=False)
    if arg_value < 0 or arg_value > _MAX_UINT8:
        raise ValueError(f"{arg_name}'s value is out of range,{arg_value} is not a uint8 value.")
