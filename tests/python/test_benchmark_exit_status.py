# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Verify launcher failure propagation without starting NPU processes."""

from contextlib import ExitStack
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch


def load_launcher(name, relative_path):
    source = Path(__file__).resolve().parents[2] / "benchmarks" / relative_path
    spec = importlib.util.spec_from_file_location(name, source)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


COMM = load_launcher(
    "comm_exit_launcher", "comm_benchmark/scripts/run_comm_benchmark.py"
)
KV = load_launcher("kv_exit_launcher", "kv_benchmark/scripts/run_kv_benchmark.py")
EXIT_CASES = ((0, 0), (-9, 0), (0, -15), (-11, -9), (2, 0), (2, 3), (-9, 2))


class BenchmarkExitStatusTest(unittest.TestCase):
    def check_launch(self, launch):
        for codes in EXIT_CASES:
            with self.subTest(exit_codes=codes):
                processes = [Mock() for _ in codes]
                for proc, code in zip(processes, codes):
                    proc.wait.return_value = code
                    proc.poll.return_value = None
                result = launch(processes)
                if any(codes):
                    self.assertNotEqual(result, 0)
                    self.assertIn(result, codes)
                else:
                    self.assertEqual(result, 0)
                if all(code >= 0 for code in codes):
                    self.assertEqual(result, max(codes))
                for proc in processes:
                    proc.wait.assert_called_once_with()

    def test_single_machine_propagates_child_failure(self):
        def launch(processes):
            args = SimpleNamespace(
                pattern="pairwise",
                num_targets=None,
                num_initiators=None,
                host="127.0.0.1",
                base_hixl_port=17000,
            )
            with ExitStack() as stack:
                stack.enter_context(
                    patch.object(COMM, "start_process", side_effect=processes)
                )
                stack.enter_context(
                    patch.object(COMM, "_single_target_cmd", return_value=[])
                )
                stack.enter_context(
                    patch.object(COMM, "_single_initiator_cmd", return_value=[])
                )
                stack.enter_context(patch.object(COMM.time, "sleep"))
                return COMM._run_single_direction(args, "bench", [0, 1], "D2rD", 30)

        self.check_launch(launch)

    def test_dual_target_propagates_child_failure(self):
        def launch(processes):
            args = SimpleNamespace(transport="roce")
            ctx = COMM.DualTargetContext(args, "bench", None, "127.0.0.1", 17000, 30)
            with ExitStack() as stack:
                stack.enter_context(
                    patch.object(COMM, "_dual_target_lanes", return_value=[{}, {}])
                )
                stack.enter_context(
                    patch.object(COMM, "build_target_cmd", return_value=[])
                )
                stack.enter_context(
                    patch.object(COMM, "start_process", side_effect=processes)
                )
                stack.enter_context(patch.object(COMM.time, "sleep"))
                result = COMM._run_dual_target_step(ctx, "hccs", "D2rD")
            self.assertEqual(args.transport, "roce")
            return result

        self.check_launch(launch)

    def test_dual_initiator_propagates_child_failure(self):
        def launch(processes):
            args = SimpleNamespace(
                transport="roce", target_host="127.0.0.1", base_hixl_port=17000
            )
            ctx = COMM.DualInitiatorContext(args, "bench", None, 17000)
            with ExitStack() as stack:
                stack.enter_context(
                    patch.object(COMM, "_dual_initiator_lanes", return_value=[{}, {}])
                )
                stack.enter_context(
                    patch.object(COMM, "build_initiator_cmd", return_value=[])
                )
                stack.enter_context(
                    patch.object(COMM, "start_process", side_effect=processes)
                )
                result = COMM._run_dual_initiator_step(ctx, "hccs", "D2rD")
            self.assertEqual(args.transport, "roce")
            return result

        self.check_launch(launch)

    def test_kv_propagates_child_failure_and_skips_success_plots(self):
        def launch(processes):
            argv = [
                "run_kv_benchmark.py",
                "--platform=a2",
                "--num_processes=2",
                "--devices=0,1",
                "--bench_bin=" + sys.executable,
                "--model_config=" + str(Path(__file__).resolve()),
            ]
            with ExitStack() as stack:
                stack.enter_context(patch.object(sys, "argv", argv))
                stack.enter_context(patch.object(KV, "prepare_output_dir"))
                stack.enter_context(patch.object(KV, "_log_run_config"))
                stack.enter_context(
                    patch.object(KV, "_kv_process_cmd", return_value=["bench"])
                )
                stack.enter_context(
                    patch.object(KV.subprocess, "Popen", side_effect=processes)
                )
                stack.enter_context(
                    patch.object(KV, "combine_results", return_value=None)
                )
                plots = stack.enter_context(patch.object(KV, "generate_plots"))
                result = KV.main()
            if any(proc.wait.return_value for proc in processes):
                plots.assert_not_called()
            else:
                plots.assert_called_once()
            return result

        self.check_launch(launch)


if __name__ == "__main__":
    unittest.main()
