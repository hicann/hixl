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
from llm_datadist.utils import log

try:
    from llm_datadist import llm_datadist_wrapper as llm_wrapper
except ImportError:
    from llm_datadist_v1 import llm_wrapper

class LLMStatusCode(Enum):
    LLM_SUCCESS = llm_wrapper.kSuccess
    LLM_FAILED = llm_wrapper.kFailed
    LLM_PARAM_INVALID = llm_wrapper.kParamInvalid
    LLM_WAIT_PROCESS_TIMEOUT = llm_wrapper.kLLMWaitProcessTimeOut
    LLM_KV_CACHE_NOT_EXIST = llm_wrapper.kLLMKVCacheNotExist
    LLM_REPEAT_REQUEST = llm_wrapper.kLLMRepeatRequest
    LLM_REQUEST_ALREADY_COMPLETED = llm_wrapper.kLLMRequestAlreadyCompleted
    LLM_ENGINE_FINALIZED = llm_wrapper.kLLMEngineFinalized
    LLM_NOT_YET_LINK = llm_wrapper.kLLMNotYetLink
    LLM_ALREADY_LINK = llm_wrapper.kAlreadyLink
    LLM_LINK_FAILED = llm_wrapper.kLinKFailed
    LLM_UNLINK_FAILED = llm_wrapper.kUnlinkFailed
    LLM_NOTIFY_PROMPT_UNLINK_FAILED = llm_wrapper.kNotifyPromptUnlinkFailed
    LLM_CLUSTER_NUM_EXCEED_LIMIT = llm_wrapper.kLLMClusterNumExceedLimit
    LLM_PROCESSING_LINK = llm_wrapper.kProcessingLink
    LLM_DEVICE_OUT_OF_MEMORY = llm_wrapper.kDeviceOutOfMemory
    LLM_PREFIX_ALREADY_EXIST = llm_wrapper.kPrefixAlreadyExist
    LLM_PREFIX_NOT_EXIST = llm_wrapper.kPrefixNotExist
    LLM_SEQ_LENOVERLIMIT = llm_wrapper.kLLMSeqLenOverLimit
    LLM_NO_FREE_BLOCK = llm_wrapper.kLLMNoFreeBlock
    LLM_BLOCKS_OUT_OF_MEMORY = llm_wrapper.kLLMBlocksOutOfMemory
    LLM_EXIST_LINK = llm_wrapper.kLLMExistLink
    LLM_FEATURE_NOT_ENABLED = llm_wrapper.kLLMFeatureNotEnabled
    LLM_TIMEOUT = llm_wrapper.kLLMTimeout
    LLM_LINK_BUSY = llm_wrapper.kLLMLinkBusy
    LLM_OUT_OF_MEMORY = llm_wrapper.kLLMOutOfMemory
    LLM_DEVICE_MEM_ERROR = llm_wrapper.kLLMDeviceMemError
    LLM_SUSPECT_REMOTE_ERROR = llm_wrapper.kLLMSuspectRemoteError
    LLM_UNKNOWN_ERROR = -1


class LLMException(RuntimeError):
    def __init__(self, *args, status_code=LLMStatusCode.LLM_SUCCESS):
        super().__init__(*args)
        self._status_code = status_code

    @property
    def status_code(self):
        """
        获取异常的错误码
        Returns:
            异常的错误码
        """
        return self._status_code


_code_2_status = {
    llm_wrapper.kSuccess: LLMStatusCode.LLM_SUCCESS,
    llm_wrapper.kFailed: LLMStatusCode.LLM_FAILED,
    llm_wrapper.kParamInvalid: LLMStatusCode.LLM_PARAM_INVALID,
    llm_wrapper.kLLMWaitProcessTimeOut: LLMStatusCode.LLM_WAIT_PROCESS_TIMEOUT,
    llm_wrapper.kLLMKVCacheNotExist: LLMStatusCode.LLM_KV_CACHE_NOT_EXIST,
    llm_wrapper.kLLMRepeatRequest: LLMStatusCode.LLM_REPEAT_REQUEST,
    llm_wrapper.kLLMRequestAlreadyCompleted: LLMStatusCode.LLM_REQUEST_ALREADY_COMPLETED,
    llm_wrapper.kLLMEngineFinalized: LLMStatusCode.LLM_ENGINE_FINALIZED,
    llm_wrapper.kLLMNotYetLink: LLMStatusCode.LLM_NOT_YET_LINK,
    llm_wrapper.kAlreadyLink: LLMStatusCode.LLM_ALREADY_LINK,
    llm_wrapper.kLinKFailed: LLMStatusCode.LLM_LINK_FAILED,
    llm_wrapper.kUnlinkFailed: LLMStatusCode.LLM_UNLINK_FAILED,
    llm_wrapper.kNotifyPromptUnlinkFailed: LLMStatusCode.LLM_NOTIFY_PROMPT_UNLINK_FAILED,
    llm_wrapper.kLLMClusterNumExceedLimit: LLMStatusCode.LLM_CLUSTER_NUM_EXCEED_LIMIT,
    llm_wrapper.kProcessingLink: LLMStatusCode.LLM_PROCESSING_LINK,
    llm_wrapper.kDeviceOutOfMemory: LLMStatusCode.LLM_DEVICE_OUT_OF_MEMORY,
    llm_wrapper.kPrefixAlreadyExist: LLMStatusCode.LLM_PREFIX_ALREADY_EXIST,
    llm_wrapper.kPrefixNotExist: LLMStatusCode.LLM_PREFIX_NOT_EXIST,
    llm_wrapper.kLLMSeqLenOverLimit: LLMStatusCode.LLM_SEQ_LENOVERLIMIT,
    llm_wrapper.kLLMNoFreeBlock: LLMStatusCode.LLM_NO_FREE_BLOCK,
    llm_wrapper.kLLMBlocksOutOfMemory: LLMStatusCode.LLM_BLOCKS_OUT_OF_MEMORY,
    llm_wrapper.kLLMExistLink: LLMStatusCode.LLM_EXIST_LINK,
    llm_wrapper.kLLMFeatureNotEnabled: LLMStatusCode.LLM_FEATURE_NOT_ENABLED,
    llm_wrapper.kLLMTimeout: LLMStatusCode.LLM_TIMEOUT,
    llm_wrapper.kLLMLinkBusy: LLMStatusCode.LLM_LINK_BUSY,
    llm_wrapper.kLLMOutOfMemory: LLMStatusCode.LLM_OUT_OF_MEMORY,
    llm_wrapper.kLLMDeviceMemError: LLMStatusCode.LLM_DEVICE_MEM_ERROR,
    llm_wrapper.kLLMSuspectRemoteError: LLMStatusCode.LLM_SUSPECT_REMOTE_ERROR
}


class Status():
    def __init__(self, status):
        self._status_code = status

    def is_ok(self):
        """
        是否成功
        Returns:
            是否成功
        """
        return self._status_code == LLMStatusCode.LLM_SUCCESS

    @property
    def status_code(self):
        """
        获取错误码
        Returns:
            错误码
        """
        return self._status_code


def code_2_status(status) -> LLMStatusCode:
    return _code_2_status[status] if status in _code_2_status else LLMStatusCode.LLM_FAILED


def handle_llm_status(status, func_name, other_info):
    if status != int(llm_wrapper.kSuccess):
        raise LLMException(f"{func_name} failed, error code is {code_2_status(status)}, {other_info}.",
                           status_code=code_2_status(status))


def raise_if_false(pred, fmt, *args, status_code=LLMStatusCode.LLM_PARAM_INVALID, **kwargs):
    if not pred:
        error_msg = fmt.format(*args, **kwargs)
        log.error(error_msg)
        raise LLMException(error_msg, status_code=status_code)


def raise_if_true(pred, fmt, *args, status_code=LLMStatusCode.LLM_PARAM_INVALID, **kwargs):
    if pred:
        error_msg = fmt.format(*args, **kwargs)
        log.error(error_msg)
        raise LLMException(error_msg, status_code=status_code)
