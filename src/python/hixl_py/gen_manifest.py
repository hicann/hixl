#!/usr/bin/env python3
# coding: utf-8
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------
"""为在线编译源码目录生成 MANIFEST.sha256 完整性清单 (构建期工具, 不随包发布).

附带职责:
- 从仓内 src/python/hixl_py/CMakeLists.txt 按 [hixl:online-begin/end]
  标记提取在线编译段, 生成随包 _src/CMakeLists.txt (去缩进, 含 license 头).
  随包文件不含仓内构建分支与 HIXL_ONLINE_STANDALONE 开关脚手架, 独立配置即在线编译.
- (可选, 传入 --prebuilt-so 时) 为 wheel 预编译绑定产物生成 .build_meta.cpXXX:
  预编译与在线编译产物同名同格式 (hixl.cpython-3XX-*.so), 运行时靠随包 meta
  区分身份, 无 meta 的预编译产物会被误判为陈旧在线产物而拒绝加载.
"""

import hashlib
import json
import re
import sys
from pathlib import Path

MANIFEST_NAME = "MANIFEST.sha256"
ONLINE_BEGIN = "# ---- [hixl:online-begin] ----"
ONLINE_END = "# ---- [hixl:online-end] ----"
BODY_DEDENT = "    "  # if 块内 4 空格缩进, 提取后去除还原为独立文件顶层缩进
META_PREFIX = ".build_meta"


def extract_online_cmake(repo_cmakelists: Path) -> str:
    """提取在线段: license 头 (首个 '# ---' 注释块之前的内容) + marker 之间的正文."""
    text = repo_cmakelists.read_text(encoding="utf-8")
    begin = text.find(ONLINE_BEGIN)
    end = text.find(ONLINE_END)
    if begin < 0 or end < 0 or end < begin:
        raise RuntimeError(
            f"online markers not found or inverted in {repo_cmakelists}: "
            f"begin={begin}, end={end}"
        )
    sep_idx = text.find("# ------", 1)  # 第二条分隔线 = license 块结束
    if sep_idx <= 0:
        raise RuntimeError(f"license header not found in {repo_cmakelists}")
    line_end = text.find("\n", sep_idx)
    # 含结尾分隔线整行, 保证随包文件 license 头完整
    header = text[: len(text) if line_end < 0 else line_end + 1]
    body = text[begin + len(ONLINE_BEGIN) : end]
    dedented = "\n".join(
        line[len(BODY_DEDENT) :] if line.startswith(BODY_DEDENT) else line
        for line in body.split("\n")
    )
    comment = (
        "# 本文件由仓内 src/python/hixl_py/CMakeLists.txt 在出包时自动提取生成\n"
        "# (标记 [hixl:online-begin/end] 之间的在线编译段), 请勿手工修改.\n"
    )
    return header + comment + dedented.rstrip() + "\n"


def gen_manifest(src_dir: Path, repo_cmakelists: Path):
    online_cmake = extract_online_cmake(repo_cmakelists)
    (src_dir / "CMakeLists.txt").write_text(online_cmake, encoding="utf-8")
    lines = []
    for file in sorted(src_dir.rglob("*")):
        if not file.is_file() or file.name == MANIFEST_NAME:
            continue
        digest = hashlib.sha256(file.read_bytes()).hexdigest()
        lines.append(f"{digest}  {file.relative_to(src_dir).as_posix()}")
    manifest = src_dir / MANIFEST_NAME
    manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Generated {manifest} ({len(lines)} files)")


def gen_prebuilt_meta(prebuilt_so: Path, py_ext_suffix: str, build_timestamp: str):
    """为 wheel 预编译产物生成随包 .build_meta.cpXXX (轻量 schema).

    运行时对 pkg_dir 产物只做轻校验 (构建时间戳/Python 版本), 故只写这两个
    字段; 在线编译产物由运行时写完整 schema (另含 manifest_hash/so_sha256),
    同名文件混合 schema, 轻校验只读公共字段, 互不影响。
    """
    tag_match = re.search(r"\.cpython-(\d+)", py_ext_suffix)
    if tag_match is None:
        raise RuntimeError(f"cannot derive python tag from ext suffix: {py_ext_suffix}")
    python_tag = f"cp{tag_match.group(1)}"
    meta = {"python_tag": python_tag, "build_timestamp": build_timestamp}
    meta_path = prebuilt_so.parent / f"{META_PREFIX}.{python_tag}"
    meta_path.write_text(json.dumps(meta) + "\n", encoding="utf-8")
    print(f"Generated {meta_path} for prebuilt {prebuilt_so.name}")


def main():
    args = sys.argv[1:]
    usage = (
        f"Usage: {sys.argv[0]} <src_dir> <repo_cmakelists>"
        " --prebuilt-so <so> --py-ext-suffix <suffix> --build-timestamp <ts>"
    )
    has_prebuilt = "--prebuilt-so" in args
    if (not has_prebuilt and len(args) != 2) or (
        has_prebuilt
        and (
            len(args) != 8
            or args[2] != "--prebuilt-so"
            or args[4] != "--py-ext-suffix"
            or args[6] != "--build-timestamp"
        )
    ):
        print(usage, file=sys.stderr)
        sys.exit(1)
    src_dir = Path(args[0])
    repo_cmakelists = Path(args[1])
    if not src_dir.is_dir():
        print(f"ERROR: src dir not found: {src_dir}", file=sys.stderr)
        sys.exit(1)
    if not repo_cmakelists.is_file():
        print(f"ERROR: repo CMakeLists not found: {repo_cmakelists}", file=sys.stderr)
        sys.exit(1)
    gen_manifest(src_dir, repo_cmakelists)
    if has_prebuilt:
        prebuilt_so = Path(args[3])
        if not prebuilt_so.is_file():
            print(f"ERROR: prebuilt so not found: {prebuilt_so}", file=sys.stderr)
            sys.exit(1)
        gen_prebuilt_meta(prebuilt_so, args[5], args[7])


if __name__ == "__main__":
    main()
