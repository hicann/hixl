# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Exercise KV rank validation without an NPU or benchmark binary."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


LAUNCHER = (
    Path(__file__).resolve().parents[2]
    / "benchmarks/kv_benchmark/scripts/run_kv_benchmark.py"
)
SPEC = importlib.util.spec_from_file_location("kv_process_count_launcher", LAUNCHER)
KV = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = KV
SPEC.loader.exec_module(KV)
import platform_detect  # noqa: E402


class KvBenchmarkProcessCountTest(unittest.TestCase):
    def test_unknown_platform_noninteractive_fails_before_output_cleanup(self):
        with tempfile.TemporaryDirectory() as tmp:
            result_file = Path(tmp) / "kv_result_rank0.csv"
            result_file.write_text("existing", encoding="utf-8")
            args = SimpleNamespace(
                num_processes=1,
                key_counts=None,
                platform=None,
                output_dir=tmp,
                transport=None,
                devices="0",
                bench_bin=str(Path(tmp) / "missing"),
            )
            with (
                patch.object(KV, "parse_args", return_value=args),
                patch.object(platform_detect, "find_npu_smi", return_value=None),
                patch.object(sys, "stdin") as stdin,
                patch.object(KV, "_run_kv_processes") as run,
            ):
                stdin.isatty.return_value = False
                with self.assertLogs(KV.log, level="ERROR") as logs:
                    self.assertEqual(KV.main(), 1)
            self.assertTrue(any("--platform" in line for line in logs.output))
            self.assertEqual(result_file.read_text(encoding="utf-8"), "existing")
            run.assert_not_called()

    def test_platform_override_and_detection(self):
        self.assertEqual(KV.detect_platform_for_kv("a5"), "a5")
        with (
            patch.object(platform_detect, "find_npu_smi", return_value="npu-smi"),
            patch.object(
                platform_detect, "detect_platform_from_npu_smi", return_value="a3"
            ),
        ):
            self.assertEqual(KV.detect_platform_for_kv(None), "a3")

    def test_unknown_platform_interactive_allows_selection(self):
        with (
            patch.object(platform_detect, "find_npu_smi", return_value=None),
            patch.object(sys, "stdin") as stdin,
            patch("builtins.input", return_value="a5"),
        ):
            stdin.isatty.return_value = True
            self.assertEqual(KV.detect_platform_for_kv(None), "a5")

    def test_unrecognized_npu_smi_noninteractive_does_not_guess_a2(self):
        with (
            patch.object(platform_detect, "find_npu_smi", return_value="npu-smi"),
            patch.object(
                platform_detect, "detect_platform_from_npu_smi", return_value=None
            ),
            patch.object(
                platform_detect, "collect_npu_smi_info_text", return_value="unknown"
            ),
            patch.object(sys, "stdin") as stdin,
        ):
            stdin.isatty.return_value = False
            with self.assertRaisesRegex(RuntimeError, "--platform"):
                KV.detect_platform_for_kv(None)

    def test_nonpositive_count_preserves_existing_results(self):
        for count, devices in ((0, None), (-1, None), (-1, "0,1,2")):
            with self.subTest(count=count, devices=devices):
                with tempfile.TemporaryDirectory() as tmp:
                    output = Path(tmp)
                    names = (
                        "kv_result_rank0.csv",
                        "kv_result_rank1.json",
                        "kv_result_all.csv",
                        "keep.txt",
                    )
                    for name in names:
                        (output / name).write_text("existing", encoding="utf-8")
                    sync = output / ".kv_sync"
                    sync.mkdir()
                    (sync / "sentinel").write_text("existing", encoding="utf-8")
                    argv = [
                        sys.executable,
                        str(LAUNCHER),
                        "--platform=a2",
                        f"--num_processes={count}",
                        f"--bench_bin={sys.executable}",
                        f"--model_config={__file__}",
                        f"--output_dir={output}",
                        "--skip_plot",
                    ]
                    if devices is not None:
                        argv.append(f"--devices={devices}")
                    result = subprocess.run(
                        argv, capture_output=True, text=True, timeout=20
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(
                        "--num_processes must be positive",
                        result.stdout + result.stderr,
                    )
                    self.assertNotIn("Traceback", result.stdout + result.stderr)
                    for name in names:
                        self.assertEqual(
                            (output / name).read_text(encoding="utf-8"), "existing"
                        )
                    self.assertEqual(
                        (sync / "sentinel").read_text(encoding="utf-8"), "existing"
                    )

    def test_positive_and_default_counts_keep_device_selection(self):
        cases = (
            ([], [str(i) for i in range(8)]),
            (["--num_processes=1", "--devices=4,5"], ["4"]),
            (["--num_processes=2", "--devices=2,3"], ["2", "3"]),
        )
        for extra, expected_devices in cases:
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as tmp:
                argv = [
                    str(LAUNCHER),
                    "--platform=a2",
                    f"--bench_bin={sys.executable}",
                    f"--model_config={__file__}",
                    f"--output_dir={tmp}",
                    "--skip_plot",
                    *extra,
                ]
                with patch.object(sys, "argv", argv):
                    with patch.object(KV, "_run_kv_processes", return_value=0) as run:
                        self.assertEqual(KV.main(), 0)
                args, _binary, devices, *_rest = run.call_args.args
                self.assertEqual(args.num_processes, len(expected_devices))
                self.assertEqual(devices, expected_devices)


if __name__ == "__main__":
    unittest.main()
