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

"""Detect Ascend platform (A2/A3/A5) via npu-smi."""
from __future__ import annotations

import logging
import os
import shutil
import subprocess
import sys

from benchmark_log import configure_logging

configure_logging()
log = logging.getLogger(__name__)


def find_npu_smi() -> str | None:
    """Return npu-smi path when it exists and is executable."""
    npu_smi = shutil.which('npu-smi')
    if npu_smi is None:
        return None
    if not os.access(npu_smi, os.X_OK):
        return None
    return npu_smi


def _run_npu_smi(npu_smi: str, args: list[str]) -> str:
    """Run npu-smi with args; return combined stdout and stderr."""
    try:
        result = subprocess.run([npu_smi, *args], capture_output=True, text=True, timeout=15)
    except (subprocess.TimeoutExpired, OSError):
        return ''
    return (result.stdout or '') + (result.stderr or '')


def collect_npu_smi_info_text() -> str:
    """Run npu-smi info (and board fallback); return all captured output."""
    npu_smi = find_npu_smi()
    if npu_smi is None:
        return ''
    text = _run_npu_smi(npu_smi, ['info'])
    if classify_platform_from_npu_smi_output(text) is not None:
        return text
    board_text = _run_npu_smi(npu_smi, ['info', '-t', 'board'])
    return text + board_text


def classify_platform_from_npu_smi_output(text: str) -> str | None:
    """Map npu-smi output to a2/a3/a5. Order matters: 950 before 910B before 910."""
    if not text:
        return None
    if 'Ascend950' in text:
        return 'a5'
    if 'Ascend910B' in text:
        return 'a2'
    if 'Ascend910' in text:
        return 'a3'
    return None


def detect_platform_from_npu_smi() -> str | None:
    """Return a2/a3/a5 when npu-smi output is recognized; else None."""
    text = collect_npu_smi_info_text()
    return classify_platform_from_npu_smi_output(text)


def prompt_platform(reason: str | None = None) -> str:
    """Ask the operator to choose A2/A3/A5 when npu-smi detection is unavailable."""
    if reason:
        log.info(reason)
    if not sys.stdin.isatty():
        raise RuntimeError('Cannot detect platform and stdin is not interactive. Pass --platform=a2|a3|a5.')
    while True:
        choice = input("Which platform? Enter 'a2', 'a3', or 'a5': ").strip().lower()
        if choice in ('a2', 'a3', 'a5'):
            return choice
        log.info("Please enter 'a2', 'a3', or 'a5'.")


def detect_platform(platform_override: str | None = None) -> str:
    """Detect Ascend platform from npu-smi info. Falls back to user input."""
    if platform_override is not None:
        platform_id = platform_override.strip().lower()
        if platform_id in ('a2', 'a3', 'a5'):
            log.info(f'[INFO] Using platform from --platform: {platform_id}')
            return platform_id
        raise ValueError(f'Invalid --platform {platform_override!r}; expected a2, a3, or a5')
    if find_npu_smi() is None:
        return prompt_platform('[WARN] npu-smi not found or not executable')
    platform_id = detect_platform_from_npu_smi()
    if platform_id == 'a5':
        log.info('[INFO] Detected A5 (Ascend950) from npu-smi info')
        return platform_id
    if platform_id == 'a2':
        log.info('[INFO] Detected A2 (Ascend910B) from npu-smi info')
        return platform_id
    if platform_id == 'a3':
        log.info('[INFO] Detected A3 (Ascend910) from npu-smi info')
        return platform_id
    snippet = collect_npu_smi_info_text().strip().replace('\n', ' ')[:240]
    detail = f' snippet={snippet!r}' if snippet else ''
    return prompt_platform(f'[WARN] Could not detect platform from npu-smi info.{detail}')
