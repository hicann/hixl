# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
"""Use the complete cluster metadata class and actual socket/Host validators."""

import ast
import importlib.util
import socket
from enum import IntEnum
from pathlib import Path
from typing import List, Tuple, Union

import pytest


def load_cluster_class():
    package = (
        Path(__file__).resolve().parents[2] / "src/python/llm_datadist/llm_datadist"
    )
    spec = importlib.util.spec_from_file_location(
        "cluster_host_validators", package / "utils/utils.py"
    )
    assert spec is not None and spec.loader is not None
    validators = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validators)
    source = package / "configs.py"
    tree = ast.parse(source.read_text(encoding="utf-8"), filename=str(source))
    nodes = [
        node
        for node in tree.body
        if isinstance(node, (ast.FunctionDef, ast.ClassDef))
        and node.name in ["LLMRole", "trans_str_ip", "LLMClusterInfo"]
    ]
    assert len(nodes) == 3
    namespace = {
        "socket": socket,
        "IntEnum": IntEnum,
        "List": List,
        "Tuple": Tuple,
        "Union": Union,
    }
    namespace.update(
        {
            name: getattr(validators, name)
            for name in [
                "check_isinstance",
                "check_uint16",
                "check_uint32",
                "check_uint64",
            ]
        }
    )
    module = ast.fix_missing_locations(ast.Module(body=nodes, type_ignores=[]))
    exec(compile(module, str(source), "exec"), namespace)
    cluster = namespace.get("LLMClusterInfo")
    assert isinstance(cluster, type)
    return cluster


@pytest.mark.parametrize(
    "method,list_name",
    [
        ("append_local_ip_info", "local_ip_info_list"),
        ("append_remote_ip_info", "remote_ip_info_list"),
    ],
)
@pytest.mark.parametrize(
    "invalid,error", [(None, TypeError), (-1, ValueError), (2**32, ValueError)]
)
def test_invalid_ip_cannot_enter_cluster_metadata(method, list_name, invalid, error):
    cluster = load_cluster_class()()
    append = getattr(cluster, method)
    append("127.0.0.1", 12345)
    before = list(getattr(cluster, list_name))
    with pytest.raises(error):
        append(invalid, 12345)
    assert getattr(cluster, list_name) == before


@pytest.mark.parametrize(
    "method,list_name",
    [
        ("append_local_ip_info", "local_ip_info_list"),
        ("append_remote_ip_info", "remote_ip_info_list"),
    ],
)
@pytest.mark.parametrize("ip", [0, 2**32 - 1, 0x0100007F, "127.0.0.1"])
def test_valid_32bit_or_string_representation_is_preserved(method, list_name, ip):
    cluster = load_cluster_class()()
    getattr(cluster, method)(ip, 12345)
    expected = 0x0100007F if isinstance(ip, str) else ip
    assert getattr(cluster, list_name) == [(expected, 12345)]


@pytest.mark.parametrize("method", ["append_local_ip_info", "append_remote_ip_info"])
def test_bool_is_still_rejected(method):
    cluster = load_cluster_class()()
    with pytest.raises(TypeError):
        getattr(cluster, method)(True, 12345)
