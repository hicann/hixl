#!/usr/bin/env python3
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Check deterministic HIXL C/C++ logging violations."""

import argparse
import ast
import logging
from pathlib import Path
import re
import sys


FORMAT_INDEX = {
    "HIXL_LOGE": 1,
    "HIXL_LOGW": 0,
    "HIXL_LOGI": 0,
    "HIXL_LOGD": 0,
    "HIXL_EVENT": 0,
    "HIXL_REPORT_ERR_MSG": 1,
    "REPORT_INNER_ERR_MSG": 1,
    "LLMLOGE": 1,
    "LLMLOGW": 0,
    "LLMLOGI": 0,
    "LLMLOGD": 0,
    "LLMEVENT": 0,
    "HIXL_CHK_STATUS_RET": 1,
    "HIXL_CHK_STATUS": 1,
    "HIXL_CHK_BOOL_RET_STATUS": 2,
    "HIXL_CHK_BOOL_RET_SPECIAL_STATUS": 2,
    "HIXL_CHECK_NOTNULL": 1,
    "HIXL_CHK_ACL_RET": 1,
    "HIXL_CHK_ACL": 1,
    "HIXL_CHK_HCCL_RET": 1,
    "HIXL_CHK_HCCL": 1,
    "LLM_LOGE_IF": 1,
    "LLM_CHK_STATUS_RET": 1,
    "LLM_CHK_STATUS": 1,
    "LLM_CHK_BOOL_RET_STATUS": 2,
    "LLM_CHK_BOOL_RET_SPECIAL_STATUS": 2,
    "LLM_CHK_BOOL_EXEC": 2,
    "LLM_CHECK_NOTNULL": 1,
    "LLM_CHK_HIXL_RET": 1,
    "LLM_ASSERT": 1,
    "LLM_ASSERT_NOTNULL": 1,
    "LLM_ASSERT_SUCCESS": 1,
    "LLM_ASSERT_RT_OK": 1,
    "LLM_ASSERT_EOK": 1,
    "LLM_ASSERT_TRUE": 1,
    "LOG_BY_TYPE": 1,
    "ADXL_CHK_STATUS_RET": 1,
    "ADXL_CHK_STATUS": 1,
    "ADXL_CHK_BOOL_RET_STATUS": 2,
    "ADXL_CHK_BOOL_RET_SPECIAL_STATUS": 2,
    "ADXL_CHECK_NOTNULL": 1,
    "ADXL_CHK_LLM_RET": 1,
}
NOLOG_ARG_COUNT = {"LLM_CHK_STATUS_RET_NOLOG": 1, "LLM_CHK_BOOL_RET_STATUS_NOLOG": 2}
OPTIONAL_FORMAT_MACROS = {
    "HIXL_CHK_STATUS_RET",
    "HIXL_CHK_STATUS",
    "HIXL_CHECK_NOTNULL",
    "HIXL_CHK_ACL_RET",
    "HIXL_CHK_ACL",
    "HIXL_CHK_HCCL_RET",
    "HIXL_CHK_HCCL",
    "LLM_CHK_STATUS_RET",
    "LLM_CHK_STATUS",
    "LLM_CHK_BOOL_EXEC",
    "LLM_CHECK_NOTNULL",
    "LLM_ASSERT",
    "LLM_ASSERT_NOTNULL",
    "LLM_ASSERT_SUCCESS",
    "LLM_ASSERT_RT_OK",
    "LLM_ASSERT_EOK",
    "LLM_ASSERT_TRUE",
    "ADXL_CHK_STATUS_RET",
    "ADXL_CHK_STATUS",
    "ADXL_CHECK_NOTNULL",
}
MACRO_NAMES = tuple(FORMAT_INDEX) + tuple(NOLOG_ARG_COUNT)
MACRO_RE = re.compile(r"\b(" + "|".join(map(re.escape, MACRO_NAMES)) + r")\s*\(")
STRING_RE = re.compile(r'(?P<prefix>u8)?(?P<literal>"(?:\\.|[^"\\])*")')
PRI_RE = re.compile(r"PRI[diouxX](?:(?:LEAST|FAST)?(?:8|16|32|64)|MAX|PTR)")
RAW_STRING_RE = re.compile(
    r'(?P<prefix>u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\v\f\r\n]{0,16})\('
)
PP_NUMBER_RE = re.compile(r"(?<![\w.])(?:\d|\.\d)(?:[\w.]|'(?=[\w]))*")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}
CAST_NAMES = {"const_cast", "dynamic_cast", "reinterpret_cast", "static_cast"}
INTEGER_CONVERSIONS = set("diuoxX")
FLOAT_CONVERSIONS = set("fFeEgGaA")
ALLOWED_LENGTHS = {
    **{
        conversion: {"", "hh", "h", "l", "ll", "j", "z", "t"}
        for conversion in INTEGER_CONVERSIONS
    },
    **{conversion: {"", "l", "L"} for conversion in FLOAT_CONVERSIONS},
    "c": {"", "l"},
    "s": {"", "l"},
    "p": {""},
}
ALLOWED_FLAGS = {
    **{conversion: set("-+ 0'") for conversion in "di"},
    "u": set("-0'"),
    **{conversion: set("-#0'") for conversion in "oxX"},
    **{conversion: set("-+ #0'") for conversion in FLOAT_CONVERSIONS},
    "c": {"-"},
    "s": {"-"},
    "p": {"-"},
}
PRECISION_CONVERSIONS = INTEGER_CONVERSIONS | FLOAT_CONVERSIONS | {"s"}


def _preprocessor_offsets(text):
    offsets = set()
    continued = False
    offset = 0
    for line in text.splitlines(keepends=True):
        stripped = line.lstrip()
        current = continued or stripped.startswith("#")
        if current:
            offsets.update(range(offset, offset + len(line)))
        continued = current and line.rstrip().endswith("\\")
        offset += len(line)
    return offsets


def _mask_range(chars, start, end):
    for index in range(start, end):
        if chars[index] not in {"\n", "\r"}:
            chars[index] = " "


def _splice_escaped_newlines(text):
    chars = list(text)
    index = 0
    while index < len(chars) - 1:
        if chars[index] != "\\":
            index += 1
            continue
        end = index + 1
        if chars[end] == "\r" and end + 1 < len(chars) and chars[end + 1] == "\n":
            end += 1
        if chars[end] != "\n":
            index += 1
            continue
        for position in range(index, end + 1):
            chars[position] = " "
        index = end + 1
    return "".join(chars)


def _numeric_separator_offsets(text):
    offsets = set()
    for match in PP_NUMBER_RE.finditer(text):
        for index in range(match.start(), match.end()):
            if text[index] == "'":
                offsets.add(index)
    return offsets


def _comment_end(text, index):
    if text.startswith("//", index):
        end = text.find("\n", index + 2)
        return len(text) if end < 0 else end
    if text.startswith("/*", index):
        end = text.find("*/", index + 2)
        return len(text) if end < 0 else end + 2
    return None


def _raw_string_end(text, index):
    raw_match = RAW_STRING_RE.match(text, index)
    if raw_match is None:
        return None
    terminator = ")" + raw_match.group("delimiter") + '"'
    end = text.find(terminator, raw_match.end())
    return len(text) if end < 0 else end + len(terminator)


def _quoted_literal_end(text, index):
    if text[index] not in {'"', "'"}:
        return None
    quote = text[index]
    end = index + 1
    escaped = False
    while end < len(text):
        if escaped:
            escaped = False
        elif text[end] == "\\":
            escaped = True
        elif text[end] == quote:
            return end + 1
        end += 1
    return end


def _cpp_mask(text, mask_literals):
    logical_text = _splice_escaped_newlines(text)
    chars = list(logical_text)
    numeric_separators = _numeric_separator_offsets(logical_text)
    index = 0
    while index < len(logical_text):
        comment_end = _comment_end(logical_text, index)
        if comment_end is not None:
            _mask_range(chars, index, comment_end)
            index = comment_end
            continue
        raw_end = _raw_string_end(logical_text, index)
        if raw_end is not None:
            if mask_literals:
                _mask_range(chars, index, raw_end)
            index = raw_end
            continue
        if logical_text[index] == "'" and index in numeric_separators:
            index += 1
            continue
        literal_end = _quoted_literal_end(logical_text, index)
        if literal_end is not None:
            if mask_literals:
                _mask_range(chars, index, literal_end)
            index = literal_end
            continue
        index += 1
    return "".join(chars)


def _find_closing_paren(masked_text, opening):
    depth = 0
    index = opening
    while index < len(masked_text):
        char = masked_text[index]
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index
        index += 1
    return None


def _find_template_close(text, opening):
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "<":
            depth += 1
        elif text[index] == ">":
            depth -= 1
            if depth == 0:
                return index
        elif text[index] == ";" and depth == 1:
            return None
    return None


def _mask_template_commas(masked_content):
    chars = list(masked_content)
    ambiguous_openings = []
    index = 0
    while index < len(masked_content):
        if masked_content[index] != "<":
            index += 1
            continue
        prefix = masked_content[:index].rstrip()
        name_match = re.search(r"([A-Za-z_]\w*)$", prefix)
        if name_match is None:
            index += 1
            continue
        closing = _find_template_close(masked_content, index)
        if closing is None:
            index += 1
            continue
        template_start = index + 1
        if "," not in masked_content[template_start:closing]:
            index = closing + 1
            continue
        name = name_match.group(1)
        if name not in CAST_NAMES:
            ambiguous_openings.append(index)
        for position in range(index + 1, closing):
            if chars[position] == ",":
                chars[position] = " "
        index = closing + 1
    return "".join(chars), ambiguous_openings


def _ambiguous_argument_indexes(ambiguous_openings, boundaries):
    ambiguous_args = set()
    for opening in ambiguous_openings:
        for arg_index, (begin, end) in enumerate(boundaries):
            if begin <= opening < end:
                ambiguous_args.add(arg_index)
    return ambiguous_args


def _split_arguments(content):
    args = []
    boundaries = []
    start = 0
    levels = {"(": 0, "[": 0, "{": 0}
    closing = {")": "(", "]": "[", "}": "{"}
    masked_content, ambiguous_openings = _mask_template_commas(
        _cpp_mask(content, mask_literals=True)
    )
    if not _cpp_mask(content, mask_literals=False).strip():
        return [], set()
    for index, char in enumerate(masked_content):
        if char in levels:
            levels[char] += 1
        elif char in closing:
            levels[closing[char]] -= 1
        elif char == "," and not any(levels.values()):
            args.append(content[start:index].strip())
            boundaries.append((start, index))
            start = index + 1
    args.append(content[start:].strip())
    boundaries.append((start, len(content)))
    ambiguous_args = _ambiguous_argument_indexes(ambiguous_openings, boundaries)
    return args, ambiguous_args


def _literal_format(expression):
    expression = _cpp_mask(expression, mask_literals=False)
    parts = []
    position = 0
    while position < len(expression):
        while position < len(expression) and expression[position].isspace():
            position += 1
        if position >= len(expression):
            break
        pri_match = PRI_RE.match(expression, position)
        if pri_match is not None:
            parts.append(pri_match.group()[3])
            position = pri_match.end()
            continue
        raw_match = RAW_STRING_RE.match(expression, position)
        if raw_match is not None:
            if raw_match.group("prefix") not in {None, "u8"}:
                return None
            terminator = ")" + raw_match.group("delimiter") + '"'
            end = expression.find(terminator, raw_match.end())
            if end < 0:
                return None
            content_start = raw_match.end()
            parts.append(expression[content_start:end])
            position = end + len(terminator)
            continue
        string_match = STRING_RE.match(expression, position)
        if string_match is None:
            return None
        try:
            parts.append(ast.literal_eval(string_match.group("literal")))
        except (SyntaxError, ValueError):
            return None
        position = string_match.end()
    return "".join(parts) if parts else None


def _consume_characters(text, index, characters):
    while index < len(text) and text[index] in characters:
        index += 1
    return index


def _consume_digits(text, index):
    while index < len(text) and text[index].isdigit():
        index += 1
    return index


def _consume_length(fmt, index):
    for candidate in ("hh", "ll", "h", "l", "j", "z", "t", "L"):
        if fmt.startswith(candidate, index):
            return candidate, index + len(candidate)
    return "", index


def _parse_format_spec(fmt, index):
    flags_start = index
    index = _consume_characters(fmt, index, "#0- +'")
    flags = fmt[flags_start:index]
    argument_count = 0
    if index < len(fmt) and fmt[index] == "*":
        argument_count += 1
        index += 1
    else:
        index = _consume_digits(fmt, index)
    has_precision = index < len(fmt) and fmt[index] == "."
    if has_precision:
        index += 1
        if index < len(fmt) and fmt[index] == "*":
            argument_count += 1
            index += 1
        else:
            index = _consume_digits(fmt, index)
    length, index = _consume_length(fmt, index)
    if index >= len(fmt):
        return None
    conversion = fmt[index]
    if conversion == "n" or conversion not in ALLOWED_LENGTHS:
        return None
    if length not in ALLOWED_LENGTHS[conversion]:
        return None
    if any(flag not in ALLOWED_FLAGS[conversion] for flag in flags):
        return None
    if has_precision and conversion not in PRECISION_CONVERSIONS:
        return None
    return index + 1, argument_count + 1


def _format_argument_count(fmt):
    count = 0
    index = 0
    while index < len(fmt):
        percent = fmt.find("%", index)
        if percent < 0:
            break
        if percent + 1 < len(fmt) and fmt[percent + 1] == "%":
            index = percent + 2
            continue
        parsed = _parse_format_spec(fmt, percent + 1)
        if parsed is None:
            return None
        index, spec_count = parsed
        count += spec_count
    return count


def _check_nolog_arguments(macro, args, ambiguous_args, offset):
    if ambiguous_args:
        return []
    expected = NOLOG_ARG_COUNT[macro]
    if len(args) < expected or any(not argument for argument in args[:expected]):
        return [(offset, f"{macro} is missing required argument(s)")]
    if len(args) > expected:
        return [
            (
                offset,
                f"{macro} ignores {len(args) - expected} trailing log argument(s)",
            )
        ]
    return []


def _check_format_arguments(macro, args, ambiguous_args, offset):
    format_index = FORMAT_INDEX[macro]
    if any(index <= format_index for index in ambiguous_args):
        return []
    if len(args) < format_index or any(
        not argument for argument in args[:format_index]
    ):
        return [(offset, f"{macro} is missing required argument(s)")]
    if len(args) == format_index:
        if macro in OPTIONAL_FORMAT_MACROS:
            return []
        return [(offset, f"{macro} is missing its format argument")]
    if not args[format_index]:
        return [(offset, f"{macro} is missing its format argument")]
    fmt = _literal_format(args[format_index])
    if fmt is None:
        return []
    errors = []
    if any(ord(char) > 127 for char in fmt):
        errors.append(
            (offset, f"{macro} format contains non-ASCII text or punctuation")
        )
    expected = _format_argument_count(fmt)
    if expected is None:
        errors.append((offset, f"{macro} contains an invalid format conversion"))
        return errors
    if ambiguous_args:
        return errors
    actual = len(args) - format_index - 1
    if actual != expected:
        errors.append(
            (offset, f"{macro} expects {expected} format argument(s), got {actual}")
        )
    return errors


def _check_macro_invocation(text, masked_text, match):
    macro = match.group(1)
    opening = masked_text.find("(", match.start())
    closing = _find_closing_paren(masked_text, opening)
    if closing is None:
        return [(match.start(), f"{macro} has an unterminated argument list")]
    argument_start = opening + 1
    args, ambiguous_args = _split_arguments(text[argument_start:closing])
    if macro in NOLOG_ARG_COUNT:
        return _check_nolog_arguments(macro, args, ambiguous_args, match.start())
    return _check_format_arguments(macro, args, ambiguous_args, match.start())


def check_text(text):
    errors = []
    preprocessor = _preprocessor_offsets(text)
    masked_text = _cpp_mask(text, mask_literals=True)
    for match in MACRO_RE.finditer(masked_text):
        if match.start() in preprocessor:
            continue
        errors.extend(_check_macro_invocation(text, masked_text, match))
    return errors


def _directory_source_files(path):
    for candidate in path.rglob("*"):
        if candidate.suffix in SOURCE_SUFFIXES:
            yield candidate


def _source_files(paths):
    for path in paths:
        if path.is_dir():
            yield from _directory_source_files(path)
            continue
        if path.suffix in SOURCE_SUFFIXES and path.exists():
            yield path


def _diagnostic_logger():
    logger = logging.getLogger(__name__)
    logger.handlers.clear()
    handler = logging.StreamHandler(sys.stdout)
    handler.setFormatter(logging.Formatter("%(message)s"))
    logger.addHandler(handler)
    logger.setLevel(logging.ERROR)
    logger.propagate = False
    return logger


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="*", type=Path)
    args = parser.parse_args(argv)
    roots = args.files or [Path("src"), Path("include")]
    logger = _diagnostic_logger()
    failed = False
    for path in sorted(set(_source_files(roots))):
        text = path.read_text(encoding="utf-8")
        for offset, message in check_text(text):
            line = text.count("\n", 0, offset) + 1
            logger.error("%s:%d: %s", path, line, message)
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
