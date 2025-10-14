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

import os
from .status import LLMStatusCode, LLMException, Status
from .configs import LLMClusterInfo, LLMRole, LlmConfig, LlmConfig as LLMConfig
from .data_type import DataType
from .v2.llm_types import KvCache, CacheDesc, CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey, Placement, \
    RegisterMemStatus, Cache, LayerSynchronizer, TransferConfig, CacheTask, TransferWithCacheKeyConfig, \
    Memtype, MemInfo
from .v2.llm_datadist import LLMDataDist

__all__ = ["LLMClusterInfo", "LLMStatusCode", "LLMException",
           "LLMRole", "LlmConfig", "LLMConfig", "DataType", "Status",
           "CacheDesc", "CacheKey", "CacheKeyByIdAndIndex", "KvCache", "Cache",
           "BlocksCacheKey", "LLMDataDist", "Placement", "RegisterMemStatus", "LayerSynchronizer",
           "TransferConfig", "CacheTask", "TransferWithCacheKeyConfig", "Memtype", "MemInfo"]

try:
    from llm_datadist_v1.tensor import TensorDesc, Tensor
    from llm_datadist_v1.kv_cache_manager import KvCacheManager
    __all__ += ['TensorDesc', 'Tensor', 'KvCacheManager']
except ModuleNotFoundError:
    pass

_ENV_VAR_NAME_AUTO_USE_UC_MEMORY = 'AUTO_USE_UC_MEMORY'

if _ENV_VAR_NAME_AUTO_USE_UC_MEMORY not in os.environ:
    os.environ[_ENV_VAR_NAME_AUTO_USE_UC_MEMORY] = '0'

