# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
"""Run the complete real tracker with only vLLM config/log and clock boundaries."""

import importlib.util
import logging
import sys
import types
import unittest
from collections import OrderedDict
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


class ClockBoundary:
    def __init__(self):
        self.now = 100.0

    def time(self):
        return self.now


def _create_tracker():
    # Keep the real tracker and locks; only external vLLM config/log and the clock are boundaries.
    external = types.ModuleType("vllm")
    external.envs = types.ModuleType("vllm.envs")
    external.envs.VLLM_MOONCAKE_ABORT_REQUEST_TIMEOUT = 10
    logger = types.ModuleType("vllm.logger")
    logger.logger = logging.getLogger("test_hixl_completed_delay")
    fake_modules = {
        "vllm": external,
        "vllm.envs": external.envs,
        "vllm.logger": logger,
    }
    with patch.dict(sys.modules, fake_modules):
        source = ROOT / "src/experimental/python/vllm_hixl_connector/task_tracker.py"
        spec = importlib.util.spec_from_file_location("hixl_host_task_tracker", source)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module, module.KVCacheTaskTracker()


class TestNonTransferTimeoutState(unittest.TestCase):
    def setUp(self):
        self.module, self.tracker = _create_tracker()
        self.clock = ClockBoundary()
        self.time_patch = patch.object(self.module, "time", self.clock)
        self.time_patch.start()

    def tearDown(self):
        self.time_patch.stop()

    def _register_delayed(self):
        self.tracker.add_req_to_process("request")
        self.tracker.add_delayed_request("request", self.clock.now)

    def test_nontransfer_completion_removes_pending_timeout_entry(self):
        self._register_delayed()
        self.tracker.add_not_transfer_request("request")
        self.assertEqual(self.tracker.delayed_free_requests, OrderedDict())
        self.assertEqual(self.tracker.get_and_clear_finished_requests(), {"request"})

    def test_completed_id_is_not_emitted_again_when_old_deadline_expires(self):
        self._register_delayed()
        self.tracker.add_not_transfer_request("request")
        self.assertEqual(self.tracker.get_and_clear_finished_requests(), {"request"})
        self.clock.now = 120.0
        self.assertEqual(self.tracker.get_and_clear_finished_requests(), set())

    def test_completed_request_is_not_logged_as_force_freed(self):
        self._register_delayed()
        self.clock.now = 120.0
        with self.assertNoLogs("test_hixl_completed_delay", level="ERROR"):
            self.tracker.add_not_transfer_request("request")
            self.assertEqual(
                self.tracker.get_and_clear_finished_requests(), {"request"}
            )

    def test_normal_transfer_completion_already_cleans_timeout_entry(self):
        self._register_delayed()
        self.tracker.update_done_task_count("request")
        self.assertEqual(self.tracker.delayed_free_requests, OrderedDict())
        self.assertEqual(self.tracker.get_and_clear_finished_requests(), {"request"})
        self.clock.now = 120.0
        self.assertEqual(self.tracker.get_and_clear_finished_requests(), set())

    def test_still_pending_request_retains_real_timeout_behavior(self):
        self._register_delayed()
        self.assertEqual(self.tracker.get_and_clear_finished_requests(), set())
        self.clock.now = 130.0
        self.assertEqual(self.tracker.get_and_clear_finished_requests(), {"request"})
        self.assertEqual(self.tracker.get_and_clear_finished_requests(), set())


if __name__ == "__main__":
    unittest.main()
