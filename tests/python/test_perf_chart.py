# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Check plotted bandwidth coordinates without requiring matplotlib or CANN."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import MagicMock


SOURCE_PATH = (
    Path(__file__).resolve().parents[2] / "benchmarks/performance/render_perf_md.py"
)
SPEC = importlib.util.spec_from_file_location("perf_chart_renderer", SOURCE_PATH)
RENDERER = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = RENDERER
SPEC.loader.exec_module(RENDERER)


class PerformanceChartTest(unittest.TestCase):
    def render(self, blocks, other_transport=None):
        lookup = {
            "D2rD": {"roce": {"16K": 1.0, "32K": 2.0, "64K": 3.0}},
            "rD2D": {"roce": blocks},
        }
        if other_transport is not None:
            lookup["D2rH"] = {"fabric_mem": other_transport}
        ordered_blocks, _ = RENDERER._chart_block_axes(lookup)
        mpl = MagicMock()
        fig, ax = MagicMock(), MagicMock()
        mpl.subplots.return_value = (fig, ax)
        with tempfile.TemporaryDirectory() as output:
            spec = RENDERER.TransportChartSpec(
                mpl, lookup, "a3", Path(output), "roce", ordered_blocks, ["blue"]
            )
            RENDERER._render_transport_chart(spec)
            fig.savefig.assert_called_once()
            mpl.close.assert_called_once_with(fig)
        ax.set_xticks.assert_called_once_with(range(len(ordered_blocks)))
        self.assertEqual(ax.set_xticklabels.call_args.args[0], ordered_blocks)
        return {call.kwargs["label"]: call.args for call in ax.plot.call_args_list}

    def test_missing_first_block_keeps_original_coordinates(self):
        lines = self.render({"32K": 20.0, "64K": 30.0})
        self.assertEqual(lines["rD2D"], ([1, 2], [20.0, 30.0]))

    def test_missing_middle_block_keeps_original_coordinates(self):
        lines = self.render({"16K": 10.0, "64K": 30.0})
        self.assertEqual(lines["rD2D"], ([0, 2], [10.0, 30.0]))

    def test_single_last_block_keeps_original_coordinate(self):
        lines = self.render({"64K": 30.0})
        self.assertEqual(lines["rD2D"], ([2], [30.0]))

    def test_complete_series_preserves_order_and_zero_bandwidth(self):
        lines = self.render({"64K": 30.0, "16K": 0.0, "32K": 20.0})
        self.assertEqual(lines["rD2D"], ([0, 1, 2], [0.0, 20.0, 30.0]))

    def test_empty_series_is_not_plotted(self):
        lines = self.render({})
        self.assertEqual(lines, {"D2rD": ([0, 1, 2], [1.0, 2.0, 3.0])})

    def test_axis_also_includes_blocks_from_other_transports(self):
        lines = self.render({"256K": 40.0}, {"128K": 50.0})
        self.assertEqual(lines["rD2D"], ([4], [40.0]))
        self.assertNotIn("D2rH", lines)


if __name__ == "__main__":
    unittest.main()
