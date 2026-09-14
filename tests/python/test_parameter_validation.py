# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.

"""Boundary and type contracts for the pure Python parameter validators."""

import importlib.util
from pathlib import Path
import unittest


# Load the real module without importing llm_datadist's native extension.
SOURCE_PATH = (
    Path(__file__).resolve().parents[2]
    / "src/python/llm_datadist/llm_datadist/utils/utils.py"
)
SPEC = importlib.util.spec_from_file_location("parameter_validation", SOURCE_PATH)
VALIDATORS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATORS)

INTEGER_CASES = (
    ("check_uint8", 0, 2**8 - 1),
    ("check_uint16", 0, 2**16 - 1),
    ("check_uint32", 0, 2**32 - 1),
    ("check_uint64", 0, 2**64 - 1),
    ("check_int32", -(2**31), 2**31 - 1),
    ("check_int64", -(2**63), 2**63 - 1),
)
LIST_CASES = (
    ("check_list_int32", -(2**31), 2**31 - 1),
    ("check_list_int64", -(2**63), 2**63 - 1),
    ("check_list_uint64", 0, 2**64 - 1),
)


class IntegerValidationTest(unittest.TestCase):
    def test_accepts_exact_lower_bound(self):
        for name, minimum, _ in INTEGER_CASES:
            with self.subTest(validator=name):
                self.assertIsNone(getattr(VALIDATORS, name)("value", minimum))

    def test_accepts_exact_upper_bound(self):
        for name, _, maximum in INTEGER_CASES:
            with self.subTest(validator=name):
                self.assertIsNone(getattr(VALIDATORS, name)("value", maximum))

    def test_accepts_zero_and_interior_values(self):
        for name, minimum, maximum in INTEGER_CASES:
            for value in (0, minimum + 1, maximum - 1):
                with self.subTest(validator=name, value=value):
                    self.assertIsNone(getattr(VALIDATORS, name)("value", value))

    def test_rejects_one_below_lower_bound(self):
        for name, minimum, _ in INTEGER_CASES:
            with self.subTest(validator=name):
                with self.assertRaisesRegex(ValueError, "value"):
                    getattr(VALIDATORS, name)("value", minimum - 1)

    def test_rejects_one_above_upper_bound(self):
        for name, _, maximum in INTEGER_CASES:
            with self.subTest(validator=name):
                with self.assertRaisesRegex(ValueError, "value"):
                    getattr(VALIDATORS, name)("value", maximum + 1)

    def test_rejects_arbitrary_precision_overflow(self):
        for name, _, _ in INTEGER_CASES:
            for value in (-(2**128), 2**128):
                with self.subTest(validator=name, value=value):
                    with self.assertRaises(ValueError):
                        getattr(VALIDATORS, name)("value", value)

    def test_rejects_non_integer_inputs(self):
        for name, _, _ in INTEGER_CASES:
            for value in (None, "1", 1.0, [], {}, object()):
                with self.subTest(validator=name, input_type=type(value)):
                    with self.assertRaisesRegex(TypeError, "device_id"):
                        getattr(VALIDATORS, name)("device_id", value)


class ListValidationTest(unittest.TestCase):
    def test_accepts_boundaries_without_mutating_list(self):
        for name, minimum, maximum in LIST_CASES:
            values = [minimum, 0, maximum]
            original = values.copy()
            with self.subTest(validator=name):
                self.assertIsNone(getattr(VALIDATORS, name)("ids", values))
                self.assertEqual(values, original)

    def test_accepts_tuple_and_empty_inputs(self):
        for name, minimum, maximum in LIST_CASES:
            for values in ([], (), (minimum, maximum)):
                with self.subTest(validator=name, values=values):
                    self.assertIsNone(getattr(VALIDATORS, name)("ids", values))

    def test_rejects_underflow_after_valid_element(self):
        for name, minimum, _ in LIST_CASES:
            with self.subTest(validator=name):
                with self.assertRaisesRegex(ValueError, "ids"):
                    getattr(VALIDATORS, name)("ids", [0, minimum - 1])

    def test_rejects_overflow_after_valid_element(self):
        for name, _, maximum in LIST_CASES:
            with self.subTest(validator=name):
                with self.assertRaisesRegex(ValueError, "ids"):
                    getattr(VALIDATORS, name)("ids", [0, maximum + 1])

    def test_rejects_wrong_element_type_at_every_position(self):
        for name, _, _ in LIST_CASES:
            for invalid in (None, "1", 1.0, []):
                for index in range(3):
                    values = [0, 1, 2]
                    values[index] = invalid
                    with self.subTest(validator=name, value=invalid, index=index):
                        with self.assertRaisesRegex(TypeError, "ids"):
                            getattr(VALIDATORS, name)("ids", values)


class DefaultValueValidationTest(unittest.TestCase):
    def test_none_uses_default_sentinel(self):
        self.assertEqual(VALIDATORS.check_positive_or_set_default("size", None), -1)

    def test_none_uses_explicit_default(self):
        self.assertEqual(
            VALIDATORS.check_positive_or_set_default("size", None, default=128),
            128,
        )

    def test_zero_is_preserved_instead_of_using_default(self):
        self.assertEqual(
            VALIDATORS.check_positive_or_set_default("size", 0, default=128), 0
        )

    def test_accepts_positive_int64_values(self):
        for value in (1, 2**63 - 1):
            with self.subTest(value=value):
                self.assertEqual(
                    VALIDATORS.check_positive_or_set_default("size", value), value
                )

    def test_rejects_explicit_negative_values(self):
        for value in (-1, -(2**63)):
            with self.subTest(value=value):
                with self.assertRaisesRegex(ValueError, "size"):
                    VALIDATORS.check_positive_or_set_default("size", value)

    def test_rejects_values_outside_int64(self):
        for value in (-(2**63) - 1, 2**63):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    VALIDATORS.check_positive_or_set_default("size", value)

    def test_rejects_wrong_type_without_using_default(self):
        for value in ("1", 1.0, []):
            with self.subTest(value=value):
                with self.assertRaisesRegex(TypeError, "size"):
                    VALIDATORS.check_positive_or_set_default("size", value)


class InstanceValidationTest(unittest.TestCase):
    def test_preserves_valid_object_identity(self):
        value = [1, 2]
        self.assertIs(VALIDATORS.check_isinstance("values", value, list), value)

    def test_accepts_each_alternative_class(self):
        for value in ([1], (1,)):
            with self.subTest(value=value):
                self.assertIs(
                    VALIDATORS.check_isinstance("values", value, [list, tuple]),
                    value,
                )

    def test_accepts_subclasses(self):
        class Values(list):
            pass

        value = Values([1])
        self.assertIs(VALIDATORS.check_isinstance("values", value, list), value)

    def test_allows_none_by_default(self):
        self.assertIsNone(VALIDATORS.check_isinstance("values", None, list))

    def test_rejects_none_when_required(self):
        with self.assertRaisesRegex(TypeError, "values"):
            VALIDATORS.check_isinstance("values", None, list, allow_none=False)

    def test_error_retains_caller_context(self):
        with self.assertRaises(TypeError) as context:
            VALIDATORS.check_isinstance(
                "values", 1.5, [list, tuple], extra_fmt="RegisterMem: "
            )
        message = str(context.exception)
        for fragment in ("RegisterMem: ", "values", "list", "tuple", "float"):
            self.assertIn(fragment, message)

    def test_checks_inner_type_after_matching_outer_type(self):
        value = [0, 1]
        self.assertIs(
            VALIDATORS.check_isinstance("values", value, list, inner_class=int),
            value,
        )
        with self.assertRaisesRegex(TypeError, "values"):
            VALIDATORS.check_isinstance(
                "values", [0, "1"], [list, tuple], inner_class=int
            )

    def test_outer_type_is_checked_before_iteration(self):
        with self.assertRaisesRegex(TypeError, "values"):
            VALIDATORS.check_isinstance("values", 1, list, inner_class=int)


class ContainerValidationTest(unittest.TestCase):
    def test_inner_accepts_empty_and_homogeneous_sequences(self):
        for values in ([], (), [0, 1], (0, 1)):
            with self.subTest(values=values):
                self.assertIsNone(VALIDATORS.check_inner("values", values, int))

    def test_inner_rejects_later_invalid_element(self):
        with self.assertRaisesRegex(TypeError, "values"):
            VALIDATORS.check_inner("values", [0, "1"], int)

    def test_dictionary_accepts_empty_and_valid_entries(self):
        for value in ({}, {"first": [1], "second": [2, 3]}):
            original = {key: item.copy() for key, item in value.items()}
            with self.subTest(value=value):
                self.assertIsNone(
                    VALIDATORS.check_dict("options", value, str, list, int)
                )
                self.assertEqual(value, original)

    def test_dictionary_rejects_later_invalid_key(self):
        with self.assertRaisesRegex(TypeError, "options.*key"):
            VALIDATORS.check_dict("options", {"valid": [1], 2: [2]}, str, list)

    def test_dictionary_rejects_later_invalid_value(self):
        with self.assertRaisesRegex(TypeError, "options.*value"):
            VALIDATORS.check_dict("options", {"valid": [1], "bad": 2}, str, list)

    def test_dictionary_checks_inner_elements(self):
        with self.assertRaisesRegex(TypeError, "dict value"):
            VALIDATORS.check_dict(
                "options", {"valid": [1], "bad": [2, "3"]}, str, list, int
            )

    def test_dictionary_inner_check_is_optional(self):
        self.assertIsNone(
            VALIDATORS.check_dict("options", {"values": [1, "two"]}, str, list)
        )

    def test_dictionary_validates_scalar_values_without_iteration(self):
        self.assertIsNone(
            VALIDATORS.check_dict("options", {"first": 1, "second": 2}, str, int)
        )


if __name__ == "__main__":
    unittest.main()
