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

import logging
import os
import sys
import threading
import traceback

_global_logger = None
_global_logger_lock = threading.Lock()
_log_level_map = {
    "0": logging.DEBUG,
    "1": logging.INFO,
    "2": logging.WARNING,
    "3": logging.ERROR,
}
LOG_FORMAT = "[%(levelname)s] %(name)s(%(process)d,%(processName)s):%(asctime)s.%(msecs)03d " \
             "[%(filename)s:%(lineno)d]%(thread)d %(funcName)s: %(message)s"


def get_logger():
    global _global_logger
    if _global_logger:
        return _global_logger
    with _global_logger_lock:
        if _global_logger:
            return _global_logger
        logger = logging.getLogger("LLM_DATADIST")
        logger.findCaller = _find_caller
        log_level = os.getenv("ASCEND_GLOBAL_LOG_LEVEL")
        logger.setLevel(_log_level_map.get(log_level, logging.ERROR))
        handler = logging.StreamHandler(sys.stdout)
        handler.setFormatter(logging.Formatter(LOG_FORMAT, datefmt="%Y-%m-%d %H:%M:%S"))
        logger.addHandler(handler)
        _global_logger = logger
        return _global_logger


def info(msg, *args, **kwargs):
    get_logger().info(msg, *args, **kwargs)


def warn(msg, *args, **kwargs):
    get_logger().warn(msg, *args, **kwargs)


def error(msg, *args, **kwargs):
    get_logger().error(msg, *args, **kwargs)


def _get_stack_info(frame):
    return 'Stack (most recent call last):\n' + ''.join(traceback.format_stack(frame))


def _find_caller(stack_info=False, stack_level=1):
    f = sys._getframe(3)
    log_file = f.f_code.co_filename
    rv = '(unknown file)', 0, '(unknown function)', None
    while f:
        f = f.f_back
        code = f.f_code
        if code.co_filename != log_file:
            sinfo = _get_stack_info(f) if stack_info else None
            rv = (code.co_filename, f.f_lineno, code.co_name, sinfo)
            break
    return rv
