# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

import importlib.util
from pathlib import Path
import re
import unittest


SCRIPT_PATH = Path(__file__).parents[2] / "scripts" / "check_log_spec.py"
REPO_ROOT = SCRIPT_PATH.parents[1]
SPEC = importlib.util.spec_from_file_location("check_log_spec", SCRIPT_PATH)
CHECK_LOG_SPEC = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_LOG_SPEC)


class LogSpecCheckerTest(unittest.TestCase):
    def assert_valid(self, source):
        self.assertEqual(CHECK_LOG_SPEC.check_text(source), [])

    def assert_invalid(self, source, expected):
        errors = CHECK_LOG_SPEC.check_text(source)
        self.assertTrue(any(expected in message for _, message in errors), errors)

    def test_accepts_multiline_nested_arguments_and_pri_macro(self):
        self.assert_valid(
            """LLMEVENT("size:%" PRIu64 ", ratio:%%, value:%d", size,
                       Convert(foo, bar));"""
        )

    def test_accepts_pri_least_macro(self):
        self.assert_valid('HIXL_LOGI("value:%" PRIuLEAST8, value);')

    def test_rejects_missing_pri_format_argument(self):
        self.assert_invalid(
            'HIXL_LOGI("value:%" PRIu64);',
            "expects 1 format argument(s), got 0",
        )

    def test_rejects_invalid_conversion_after_pri_macro(self):
        self.assert_invalid(
            'HIXL_LOGI("value:%" PRIu64 ", bad:%:lu", value, bad);',
            "invalid format conversion",
        )

    def test_rejects_non_ascii_text_after_pri_macro(self):
        self.assert_invalid(
            'HIXL_LOGI("value:%" PRIu64 ", bad：%d", value, bad);',
            "non-ASCII",
        )

    def test_rejects_incomplete_pri_macro_names(self):
        self.assertIsNone(CHECK_LOG_SPEC.PRI_RE.fullmatch("PRIu"))
        self.assertIsNone(CHECK_LOG_SPEC.PRI_RE.fullmatch("PRIuFAST"))

    def test_accepts_error_report_macros_with_pri_macro(self):
        self.assert_valid('HIXL_REPORT_ERR_MSG("E19999", "size:%" PRIu64, size);')
        self.assert_valid('REPORT_INNER_ERR_MSG("E19999", "value:%d", value);')

    def test_rejects_missing_error_report_argument(self):
        self.assert_invalid(
            'HIXL_REPORT_ERR_MSG("E19999", "ret:%d, fd:%d", ret);',
            "expects 2 format argument(s), got 1",
        )

    def test_rejects_invalid_error_report_conversion(self):
        self.assert_invalid(
            'REPORT_INNER_ERR_MSG("E19999", "value:%:lu", value);',
            "invalid format conversion",
        )

    def test_rejects_missing_format_argument(self):
        self.assert_invalid(
            'HIXL_LOGE(FAILED, "fd:%d, ret:%d", fd);',
            "expects 2 format argument(s), got 1",
        )

    def test_rejects_invalid_conversion(self):
        self.assert_invalid(
            'LLMLOGI("free times%:lu", value);', "invalid format conversion"
        )

    def test_rejects_non_ascii_log_text(self):
        self.assert_invalid('HIXL_LOGI("value：%d", value);', "non-ASCII")

    def test_checks_record_macro_format_arguments(self):
        self.assert_valid('HIXL_RECORD(module, level, "value:%d", value);')
        self.assert_invalid(
            'LLM_RECORD(module, level, "value:%d");',
            "expects 1 format argument(s), got 0",
        )

    def test_rejects_ignored_nolog_message(self):
        self.assert_invalid(
            'LLM_CHK_BOOL_RET_STATUS_NOLOG(ready, ge::FAILED, "not ready");',
            "ignores 1 trailing log argument",
        )

    def test_ignores_macro_definitions(self):
        self.assert_valid(
            """#define WRAPPER(fmt, ...) \\
  HIXL_LOGI(fmt, ##__VA_ARGS__)
void Run() { HIXL_LOGD("done"); }"""
        )

    def test_accepts_status_check_without_optional_message(self):
        self.assert_valid("HIXL_CHK_STATUS_RET(DoWork());")

    def test_accepts_acl_hccl_checks_without_optional_message(self):
        for macro in (
            "HIXL_CHK_ACL_RET",
            "HIXL_CHK_ACL",
            "HIXL_CHK_HCCL_RET",
            "HIXL_CHK_HCCL",
        ):
            with self.subTest(macro=macro):
                self.assert_valid(f"{macro}(Api());")

    def test_checks_acl_hccl_format_argument_count(self):
        for macro in (
            "HIXL_CHK_ACL_RET",
            "HIXL_CHK_ACL",
            "HIXL_CHK_HCCL_RET",
            "HIXL_CHK_HCCL",
        ):
            with self.subTest(macro=macro):
                self.assert_invalid(
                    f'{macro}(Api(), "ret:%d");',
                    "expects 1 format argument(s), got 0",
                )

    def test_checks_acl_hccl_invalid_conversion_and_pri_macro(self):
        self.assert_invalid(
            'HIXL_CHK_HCCL_RET(Api(), "bad:%q", value);',
            "invalid format conversion",
        )
        self.assert_valid('HIXL_CHK_ACL_RET(Api(), "size:%" PRIu64, size);')

    def test_accepts_remaining_checks_without_optional_message(self):
        for invocation in (
            "HIXL_CHECK_NOTNULL(ptr);",
            "LLM_CHK_BOOL_EXEC(ready, HandleError());",
            "LLM_CHECK_NOTNULL(ptr);",
            "ADXL_CHECK_NOTNULL(ptr);",
        ):
            with self.subTest(invocation=invocation):
                self.assert_valid(invocation)

    def test_checks_remaining_check_macro_format_argument_count(self):
        invocations = (
            'HIXL_CHK_BOOL_RET_SPECIAL_STATUS(ready, FAILED, "value:%d");',
            'HIXL_CHECK_NOTNULL(ptr, "value:%d");',
            'LLM_LOGE_IF(failed, "value:%d");',
            'LLM_CHK_BOOL_RET_SPECIAL_STATUS(ready, FAILED, "value:%d");',
            'LLM_CHK_BOOL_EXEC(ready, HandleError(), "value:%d");',
            'LLM_CHECK_NOTNULL(ptr, "value:%d");',
            'ADXL_CHK_BOOL_RET_SPECIAL_STATUS(ready, FAILED, "value:%d");',
            'ADXL_CHECK_NOTNULL(ptr, "value:%d");',
            'ADXL_CHK_LLM_RET(Api(), "value:%d");',
        )
        for invocation in invocations:
            with self.subTest(invocation=invocation):
                self.assert_invalid(
                    invocation,
                    "expects 1 format argument(s), got 0",
                )

    def test_checks_remaining_check_macro_invalid_conversion_and_pri_macro(self):
        self.assert_invalid(
            'LLM_CHK_BOOL_EXEC(ready, HandleError(), "bad:%q", value);',
            "invalid format conversion",
        )
        self.assert_valid('ADXL_CHK_LLM_RET(Api(), "size:%" PRIu64, size);')

    def test_checks_llm_hixl_and_log_by_type_format_argument_count(self):
        for invocation in (
            'LLM_CHK_HIXL_RET(Api(), "ret:%d");',
            'LOG_BY_TYPE(DLOG_INFO, "ret:%d");',
        ):
            with self.subTest(invocation=invocation):
                self.assert_invalid(
                    invocation,
                    "expects 1 format argument(s), got 0",
                )

    def test_accepts_assert_macros_without_optional_message(self):
        for macro in (
            "LLM_ASSERT",
            "LLM_ASSERT_NOTNULL",
            "LLM_ASSERT_SUCCESS",
            "LLM_ASSERT_RT_OK",
            "LLM_ASSERT_EOK",
            "LLM_ASSERT_TRUE",
        ):
            with self.subTest(macro=macro):
                self.assert_valid(f"{macro}(value);")

    def test_checks_assert_macro_format_and_pri_macro(self):
        self.assert_invalid(
            'LLM_ASSERT_TRUE(ready, "ret:%d");',
            "expects 1 format argument(s), got 0",
        )
        self.assert_invalid(
            'LLM_ASSERT_NOTNULL(ptr, "bad:%q", value);',
            "invalid format conversion",
        )
        self.assert_valid('LLM_ASSERT_SUCCESS(ret, "size:%" PRIu64, size);')

    def test_ignores_macro_names_outside_code(self):
        self.assert_valid(
            r"""// HIXL_LOGI("ret:%d");
/* HIXL_LOGE(FAILED, "ret:%d"); */
const char *text = "HIXL_LOGI(\"ret:%d\")";
const char *raw_text = R"tag(HIXL_LOGI("ret:%d"))tag";"""
        )

    def test_accepts_comments_inside_argument_list(self):
        self.assert_valid('HIXL_LOGI("ret:%d", /* old, value (ignored) */ ret);')

    def test_checks_format_followed_by_comment(self):
        self.assert_invalid(
            'HIXL_LOGI("ret:%d" /* failure */);',
            "expects 1 format argument(s), got 0",
        )

    def test_accepts_comments_between_literal_and_pri_macro(self):
        self.assert_valid('HIXL_LOGI("size:%" /* uint64 */ PRIu64, size);')

    def test_accepts_u8_and_raw_string_formats(self):
        for invocation in (
            'HIXL_LOGI(u8"ret:%d", ret);',
            'HIXL_LOGI(R"(ret:%d)", ret);',
            'HIXL_LOGI(R"tag(ret:%d)tag", ret);',
            'HIXL_LOGI("size:%" R"(lu)", size);',
        ):
            with self.subTest(invocation=invocation):
                self.assert_valid(invocation)

    def test_checks_u8_and_raw_string_format_arguments(self):
        for invocation in (
            'HIXL_LOGI(u8"ret:%d");',
            'HIXL_LOGI(R"(ret:%d)");',
            'HIXL_LOGI(R"tag(ret:%d)tag");',
        ):
            with self.subTest(invocation=invocation):
                self.assert_invalid(
                    invocation,
                    "expects 1 format argument(s), got 0",
                )

    def test_handles_escaped_newlines(self):
        self.assert_valid(
            """// disabled log \\
HIXL_LOGI("ret:%d");
HIXL_LOGI \\
("ret:%d", ret);"""
        )

    def test_handles_cpp_digit_separators(self):
        for literal in ("1'000", "0xFF'EE", "0b1010'0101"):
            with self.subTest(literal=literal):
                self.assert_invalid(
                    f'constexpr auto value = {literal};\nHIXL_LOGI("value:%d");',
                    "expects 1 format argument(s), got 0",
                )
        self.assert_invalid(
            "constexpr char value = 'x';\nHIXL_LOGI(\"value:%d\");",
            "expects 1 format argument(s), got 0",
        )

    def test_handles_explicit_cast_template_commas(self):
        self.assert_valid(
            'HIXL_LOGI("ret:%d", static_cast<std::pair<int, int>>(value).first);'
        )
        self.assert_valid(
            """HIXL_LOGI("ret:%d", static_cast<
                           std::pair<int, int>>(value).first);"""
        )
        self.assert_invalid(
            'HIXL_LOGI("ret:%d, other:%d", '
            "static_cast<std::pair<int, int>>(value).first);",
            "expects 2 format argument(s), got 1",
        )

    def test_skips_argument_count_for_ambiguous_angle_brackets(self):
        for invocation in (
            'HIXL_LOGI("left:%d, right:%d", a < b, c > (d));',
            'HIXL_LOGI("left:%d, right:%d", a<b, c>(d));',
            'HIXL_LOGI("ret:%d", Value<int, int>);',
            'HIXL_LOGI("ret:%d, other:%d", Value<int, int>, other);',
            'HIXL_LOGI("ret:%d", MakeValue<int, int>(value));',
            'HIXL_LOGI("ret:%d", MakeValue<Outer<int, int>, int>(value));',
            'HIXL_LOGI("ret:%d", (Value<int, int>));',
        ):
            with self.subTest(invocation=invocation):
                self.assert_valid(invocation)

    def test_checks_format_text_after_ambiguous_angle_brackets(self):
        self.assert_invalid(
            'HIXL_LOGI("bad:%q", Value<int, int>);',
            "invalid format conversion",
        )
        self.assert_invalid(
            'HIXL_LOGI("bad：%d", a < b, c > (d));',
            "non-ASCII",
        )

    def test_skips_checks_when_ambiguity_precedes_format_argument(self):
        self.assert_valid('HIXL_LOGE(Select<int, int>, "bad:%q", value);')
        self.assert_valid("LLM_CHK_BOOL_RET_STATUS_NOLOG(Value<int, int>, FAILED);")

    def test_handles_comparison_without_parenthesized_right_operand(self):
        self.assert_valid('HIXL_LOGI("left:%d, right:%d", a < b, c > d);')

    def test_checks_required_arguments_for_all_format_macros(self):
        for macro, format_index in CHECK_LOG_SPEC.FORMAT_INDEX.items():
            fixed_args = ["value"] * format_index
            with self.subTest(macro=macro, case="required"):
                if format_index > 0:
                    invocation = f"{macro}({', '.join(fixed_args[:-1])});"
                    self.assert_invalid(invocation, "missing required argument")
            with self.subTest(macro=macro, case="format"):
                invocation = f"{macro}({', '.join(fixed_args)});"
                if macro in CHECK_LOG_SPEC.OPTIONAL_FORMAT_MACROS:
                    self.assert_valid(invocation)
                else:
                    self.assert_invalid(invocation, "missing its format argument")
            with self.subTest(macro=macro, case="format_count"):
                args = fixed_args + ['"value:%d"']
                invocation = f"{macro}({', '.join(args)});"
                self.assert_invalid(invocation, "expects 1 format argument(s), got 0")

    def test_checks_exact_nolog_arguments(self):
        for macro, expected in CHECK_LOG_SPEC.NOLOG_ARG_COUNT.items():
            args = ["value"] * expected
            with self.subTest(macro=macro, case="valid"):
                self.assert_valid(f"{macro}({', '.join(args)});")
            with self.subTest(macro=macro, case="missing"):
                self.assert_invalid(
                    f"{macro}({', '.join(args[:-1])});",
                    "missing required argument",
                )
            with self.subTest(macro=macro, case="extra"):
                self.assert_invalid(
                    f"{macro}({', '.join(args + ['extra'])});",
                    "ignores 1 trailing log argument",
                )

    def test_rejects_empty_required_arguments(self):
        for invocation in (
            "HIXL_CHK_STATUS_RET();",
            "HIXL_CHECK_NOTNULL(,);",
            "LLM_ASSERT_TRUE();",
            "LLM_CHK_BOOL_RET_STATUS_NOLOG(ready,);",
            "HIXL_LOGI(,);",
        ):
            with self.subTest(invocation=invocation):
                self.assert_invalid(invocation, "missing")

    def test_accepts_standard_format_conversion_matrix(self):
        for conversion, lengths in CHECK_LOG_SPEC.ALLOWED_LENGTHS.items():
            for length in lengths:
                with self.subTest(conversion=conversion, length=length):
                    self.assert_valid(
                        f'HIXL_LOGI("value:%{length}{conversion}", value);'
                    )
        self.assert_valid('HIXL_LOGI("value:%*.*s", width, precision, value);')
        self.assert_valid('HIXL_LOGI("ratio:%%");')

    def test_rejects_invalid_format_conversion_matrix(self):
        lengths = {"", "hh", "h", "l", "ll", "j", "z", "t", "L", "hl"}
        for conversion, allowed in CHECK_LOG_SPEC.ALLOWED_LENGTHS.items():
            for length in lengths - allowed:
                with self.subTest(conversion=conversion, length=length):
                    self.assert_invalid(
                        f'HIXL_LOGI("value:%{length}{conversion}", value);',
                        "invalid format conversion",
                    )
        for fmt in ("%n", "%hld", "%lls", "%lp", "%Ld", "%#s", "%.2c"):
            with self.subTest(fmt=fmt):
                self.assert_invalid(
                    f'HIXL_LOGI("value:{fmt}", value);',
                    "invalid format conversion",
                )

    def test_registers_all_variadic_log_macros(self):
        definition_files = (
            "src/hixl/common/hixl_log.h",
            "src/hixl/common/hixl_checker.h",
            "src/llm_datadist/common/llm_log.h",
            "src/llm_datadist/common/llm_checker.h",
            "src/llm_datadist/adxl/adxl_checker.h",
            "src/llm_datadist/memory/allocator/scalable_allocator.h",
        )
        discovered = set()
        for relative_path in definition_files:
            text = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
            discovered.update(
                re.findall(
                    r"^\s*#\s*define\s+([A-Za-z_]\w*)\([^\n]*\.\.\.",
                    text,
                    re.MULTILINE,
                )
            )
        self.assertLessEqual(discovered, set(CHECK_LOG_SPEC.MACRO_NAMES))


if __name__ == "__main__":
    unittest.main()
