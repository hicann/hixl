# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

import ast
from pathlib import Path
import unittest


class LLMException(RuntimeError):
    pass


def _load_parser():
    source = (
        Path(__file__).resolve().parents[2]
        / "src/python/llm_datadist/llm_datadist/v2/llm_utils.py"
    )
    tree = ast.parse(source.read_text(encoding="utf-8"))
    parser = next(
        node
        for node in tree.body
        if isinstance(node, ast.FunctionDef) and node.name == "parse_listen_ip_info"
    )

    def check_isinstance(name, value, types):
        if not isinstance(value, tuple(types)):
            raise TypeError(name)

    def raise_if_false(condition, fmt, *args):
        if not condition:
            raise LLMException(fmt.format(*args))

    namespace = {
        "check_isinstance": check_isinstance,
        "raise_if_false": raise_if_false,
    }
    module = ast.fix_missing_locations(ast.Module(body=[parser], type_ignores=[]))
    exec(compile(module, str(source), "exec"), namespace)
    return namespace["parse_listen_ip_info"]


class ParseListenIpInfoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.parse_listen_ip_info = staticmethod(_load_parser())

    def test_parses_ipv4_address(self):
        self.assertEqual(
            self.parse_listen_ip_info("127.0.0.1:26000"), ("127.0.0.1", 26000)
        )

    def test_parses_bracketed_ipv6_address(self):
        self.assertEqual(
            self.parse_listen_ip_info("[2001:db8::1]:26000"), ("2001:db8::1", 26000)
        )

    def test_rejects_unbracketed_ipv6_address(self):
        with self.assertRaises(LLMException):
            self.parse_listen_ip_info("2001:db8::1:26000")

    def test_rejects_malformed_bracketed_address(self):
        with self.assertRaises(LLMException):
            self.parse_listen_ip_info("[2001:db8::1]26000")

    def test_rejects_non_decimal_port(self):
        with self.assertRaises(LLMException):
            self.parse_listen_ip_info("[2001:db8::1]:1_000")

    def test_braces_in_invalid_address_raise_llm_exception(self):
        for value in ("127.0.0.1:{x}", "[::1]:{}"):
            with self.subTest(value=value):
                with self.assertRaisesRegex(LLMException, "format should be"):
                    self.parse_listen_ip_info(value)


if __name__ == "__main__":
    unittest.main()
