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

"""Exercise real LogRecords from the source logging module without native bindings."""

import importlib.util
import inspect
import logging
from pathlib import Path
import threading

import pytest

LOG_PATH = (
    Path(__file__).resolve().parents[2]
    / "src/python/llm_datadist/llm_datadist/utils/log.py"
)


class Capture(logging.Handler):
    def __init__(self):
        super().__init__()
        self.records = []

    def emit(self, record):
        self.records.append(record)


@pytest.fixture
def logged():
    spec = importlib.util.spec_from_file_location("hixl_log_callers", LOG_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    logger = logging.getLogger("LLM_DATADIST")
    handlers, level = logger.handlers[:], logger.level
    propagate, find_caller = logger.propagate, logger.findCaller
    capture = Capture()
    module.get_logger()
    logger.handlers = [capture]
    logger.setLevel(logging.DEBUG)
    logger.propagate = False
    try:
        yield module, logger, capture.records
    finally:
        logger.handlers = handlers
        logger.setLevel(level)
        logger.propagate = propagate
        logger.findCaller = find_caller
        capture.close()


def _emit(method, **kwargs):
    line = inspect.currentframe().f_lineno + 1
    method("Caller regression", **kwargs)
    return line


def _outer(method):
    line = inspect.currentframe().f_lineno + 1
    _emit(method, stacklevel=2)
    return line


def _assert_location(record, function, line):
    assert Path(record.pathname) == Path(__file__)
    assert record.funcName == function
    assert record.lineno == line
    assert record.getMessage() == "Caller regression"


@pytest.mark.parametrize("name", ["info", "warn", "error"])
def test_helper_default_caller(logged, name):
    module, _, records = logged
    line = _emit(getattr(module, name))
    _assert_location(records[-1], "_emit", line)


@pytest.mark.parametrize("name", ["info", "warn", "error"])
def test_helper_respects_stacklevel(logged, name):
    module, _, records = logged
    line = _outer(getattr(module, name))
    _assert_location(records[-1], "_outer", line)


@pytest.mark.parametrize("name", ["info", "warning", "error"])
def test_direct_logger_caller(logged, name):
    _, logger, records = logged
    line = _emit(getattr(logger, name))
    _assert_location(records[-1], "_emit", line)


def test_logger_adapter_caller(logged):
    _, logger, records = logged
    adapter = logging.LoggerAdapter(logger, {})
    line = _emit(adapter.info)
    _assert_location(records[-1], "_emit", line)


def test_stack_info_ends_at_selected_caller(logged):
    module, _, records = logged
    line = _emit(module.info, stack_info=True)
    _assert_location(records[-1], "_emit", line)
    assert records[-1].stack_info.startswith("Stack (most recent call last):")
    assert f"line {line}, in _emit" in records[-1].stack_info


def test_stacklevel_three_skips_two_user_wrappers(logged):
    module, _, records = logged

    def middle():
        _emit(module.info, stacklevel=3)

    line = inspect.currentframe().f_lineno + 1
    middle()
    _assert_location(records[-1], "test_stacklevel_three_skips_two_user_wrappers", line)


def test_thread_direct_logger_caller(logged):
    _, logger, records = logged
    lines = []

    def target():
        lines.append(_emit(logger.error))

    thread = threading.Thread(target=target)
    thread.start()
    thread.join(timeout=5)
    assert not thread.is_alive()
    assert len(records) == 1
    _assert_location(records[0], "_emit", lines[0])
