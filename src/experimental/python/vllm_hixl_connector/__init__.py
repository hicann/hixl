#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""Experimental HIXL python modules, packaged into the hixl wheel.

These modules are experimental: their APIs may change or be removed at any
time. See src/experimental/CMakeLists.txt and docs/zh/build.md for how the
modules are packaged (``build.sh --experimental``).

Submodules may import optional third-party frameworks (e.g. vllm); they are
imported lazily here so that ``import vllm_hixl_connector`` alone never loads
them.
"""

from typing import Any

__all__ = ["HIXLConnector"]


def __getattr__(name: str) -> Any:
    if name == "HIXLConnector":
        from vllm_hixl_connector.connector import HIXLConnector

        return HIXLConnector
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
