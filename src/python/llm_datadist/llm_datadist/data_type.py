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

from enum import Enum
import numpy as np
from . import metadef_wrapper


class DataType(Enum):
    DT_FLOAT = int(metadef_wrapper.DT_FLOAT)
    DT_FLOAT16 = int(metadef_wrapper.DT_FLOAT16)
    DT_BF16 = int(metadef_wrapper.DT_BF16)
    DT_INT8 = int(metadef_wrapper.DT_INT8)
    DT_INT16 = int(metadef_wrapper.DT_INT16)
    DT_UINT16 = int(metadef_wrapper.DT_UINT16)
    DT_UINT8 = int(metadef_wrapper.DT_UINT8)
    DT_INT32 = int(metadef_wrapper.DT_INT32)
    DT_INT64 = int(metadef_wrapper.DT_INT64)
    DT_UINT32 = int(metadef_wrapper.DT_UINT32)
    DT_UINT64 = int(metadef_wrapper.DT_UINT64)
    DT_BOOL = int(metadef_wrapper.DT_BOOL)
    DT_DOUBLE = int(metadef_wrapper.DT_DOUBLE)
    DT_STRING = int(metadef_wrapper.DT_STRING)


_dwrapper_dtype_to_python_dtype = {
    metadef_wrapper.DT_FLOAT: DataType.DT_FLOAT,
    metadef_wrapper.DT_FLOAT16: DataType.DT_FLOAT16,
    metadef_wrapper.DT_BF16: DataType.DT_BF16,
    metadef_wrapper.DT_INT8: DataType.DT_INT8,
    metadef_wrapper.DT_INT16: DataType.DT_INT16,
    metadef_wrapper.DT_UINT16: DataType.DT_UINT16,
    metadef_wrapper.DT_UINT8: DataType.DT_UINT8,
    metadef_wrapper.DT_INT32: DataType.DT_INT32,
    metadef_wrapper.DT_INT64: DataType.DT_INT64,
    metadef_wrapper.DT_UINT32: DataType.DT_UINT32,
    metadef_wrapper.DT_UINT64: DataType.DT_UINT64,
    metadef_wrapper.DT_BOOL: DataType.DT_BOOL,
    metadef_wrapper.DT_DOUBLE: DataType.DT_DOUBLE,
    metadef_wrapper.DT_STRING: DataType.DT_STRING
}

python_dtype_2_dwrapper_dtype = {
    DataType.DT_FLOAT: metadef_wrapper.DT_FLOAT,
    DataType.DT_FLOAT16: metadef_wrapper.DT_FLOAT16,
    DataType.DT_BF16: metadef_wrapper.DT_BF16,
    DataType.DT_INT8: metadef_wrapper.DT_INT8,
    DataType.DT_INT16: metadef_wrapper.DT_INT16,
    DataType.DT_UINT16: metadef_wrapper.DT_UINT16,
    DataType.DT_UINT8: metadef_wrapper.DT_UINT8,
    DataType.DT_INT32: metadef_wrapper.DT_INT32,
    DataType.DT_INT64: metadef_wrapper.DT_INT64,
    DataType.DT_UINT32: metadef_wrapper.DT_UINT32,
    DataType.DT_UINT64: metadef_wrapper.DT_UINT64,
    DataType.DT_BOOL: metadef_wrapper.DT_BOOL,
    DataType.DT_DOUBLE: metadef_wrapper.DT_DOUBLE,
    DataType.DT_STRING: metadef_wrapper.DT_STRING
}


def get_python_dtype_from_wrapper_dtype(wrapper_dtype):
    dtype = _dwrapper_dtype_to_python_dtype.get(wrapper_dtype, None)
    if not dtype:
        raise ValueError(f"The data type {wrapper_dtype} is not support.")
    return dtype


dtype_to_np_dtype = {
    DataType.DT_FLOAT: np.float32,
    DataType.DT_FLOAT16: np.float16,
    DataType.DT_BF16: np.float16,
    DataType.DT_INT8: np.int8,
    DataType.DT_INT16: np.int16,
    DataType.DT_UINT16: np.uint16,
    DataType.DT_UINT8: np.uint8,
    DataType.DT_INT32: np.int32,
    DataType.DT_INT64: np.int64,
    DataType.DT_UINT32: np.uint32,
    DataType.DT_UINT64: np.uint64,
    DataType.DT_BOOL: np.bool_,
    DataType.DT_DOUBLE: np.double,
    DataType.DT_STRING: np.bytes_
}

valid_np_dtypes = list(dtype_to_np_dtype.values())

np_dtype_to_dtype = {
    np.dtype(np.float32): DataType.DT_FLOAT,
    np.dtype(np.float16): DataType.DT_FLOAT16,
    np.dtype(np.int8): DataType.DT_INT8,
    np.dtype(np.int16): DataType.DT_INT16,
    np.dtype(np.uint16): DataType.DT_UINT16,
    np.dtype(np.uint8): DataType.DT_UINT8,
    np.dtype(np.int32): DataType.DT_INT32,
    np.dtype(np.int64): DataType.DT_INT64,
    np.dtype(np.uint32): DataType.DT_UINT32,
    np.dtype(np.uint64): DataType.DT_UINT64,
    np.dtype(np.bool_): DataType.DT_BOOL,
    np.dtype(np.double): DataType.DT_DOUBLE,
    np.dtype(np.bytes_): DataType.DT_STRING
}
