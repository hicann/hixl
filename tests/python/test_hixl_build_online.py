#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------
"""hixl 绑定层在线编译管理器单元测试.

通过 spec_from_file_location 直接加载 _build_online.py, 绕过 hixl/__init__.py,
使测试不依赖真实绑定 .so 与 CANN 环境.
"""

import collections
import contextlib
import fcntl
import hashlib
import importlib.util
import io
import json
import os
import shutil
import sys
import sysconfig
import tempfile
import unittest
from pathlib import Path
from unittest import mock

_DOTTED_NAME = "hixl.hixl"


def _load_build_online_module():
    pkg_spec = importlib.util.find_spec("hixl")
    if pkg_spec is None or not pkg_spec.submodule_search_locations:
        raise unittest.SkipTest("hixl package not found in PYTHONPATH")
    pkg_dir = Path(list(pkg_spec.submodule_search_locations)[0])
    mod_path = pkg_dir / "_build_online.py"
    if not mod_path.is_file():
        raise unittest.SkipTest("_build_online.py not found in hixl package")
    spec = importlib.util.spec_from_file_location(
        "hixl_build_online_under_test", mod_path
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TestHixlBuildOnline(unittest.TestCase):
    mod = None

    @classmethod
    def setUpClass(cls):
        cls.mod = _load_build_online_module()

    def setUp(self):
        print("Begin ", self._testMethodName)
        self._tmp = tempfile.TemporaryDirectory()
        self.tmpdir = Path(self._tmp.name)

    def tearDown(self):
        print("End ", self._testMethodName)
        self._tmp.cleanup()
        sys.modules.pop(_DOTTED_NAME, None)
        manager_cls = self.mod.BuildOnlineHixlManager
        manager_cls._instances.pop(manager_cls, None)

    def _new_manager(self, pkg_dir: Path):
        manager_cls = self.mod.BuildOnlineHixlManager
        manager_cls._instances.pop(manager_cls, None)
        with mock.patch.object(manager_cls, "__init__", lambda self: None):
            manager = manager_cls()
        manager.pkg_dir = pkg_dir
        manager.src_dir = pkg_dir / "_src"
        manager.version = "0.0.1"
        manager.build_timestamp = "test"
        manager._target_compiled = False
        manager._cache_dir_value = None
        manager.python_tag = "cptest"
        manager._lock_name = ".hixl_build.cptest.lock"
        manager._meta_name = ".build_meta.cptest"
        manager._initialized = True
        return manager

    def _write_manifest(self, src_dir: Path):
        lines = []
        for file in sorted(src_dir.rglob("*")):
            if file.is_file() and file.name != "MANIFEST.sha256":
                digest = hashlib.sha256(file.read_bytes()).hexdigest()
                lines.append(f"{digest}  {file.relative_to(src_dir).as_posix()}")
        (src_dir / "MANIFEST.sha256").write_text(
            "\n".join(lines) + "\n", encoding="utf-8"
        )

    def _make_stub_src(self, src_dir: Path):
        src_dir.mkdir(parents=True, exist_ok=True)
        (src_dir / "hixl_py.cc").write_text("// stub", encoding="utf-8")
        (src_dir / "hixl_py.h").write_text("// stub", encoding="utf-8")
        (src_dir / "CMakeLists.txt").write_text("# stub", encoding="utf-8")
        self._write_manifest(src_dir)

    def _make_fake_cann(self, root: Path):
        for header in (
            "hixl/hixl.h",
            "hixl/hixl_types.h",
            "external/ge_common/ge_api_error_codes.h",
        ):
            f = root / "include" / header
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_text("// stub", encoding="utf-8")
        for lib in ("libcann_hixl.so", "libmetadef.so"):
            f = root / "lib64" / lib
            f.parent.mkdir(parents=True, exist_ok=True)
            f.write_bytes(b"\x7fELF")
        opp = root / "opp"
        opp.mkdir(exist_ok=True)
        (opp / "version.info").write_text("Version=9.2.0\n", encoding="utf-8")

    def _ext_suffix(self) -> str:
        return sysconfig.get_config_var("EXT_SUFFIX") or ".so"

    def test_fast_path_skips_compile(self):
        manager = self._new_manager(self.tmpdir)
        manager._target_compiled = True
        with mock.patch.object(manager, "_ensure_locked") as ensure_locked:
            manager.ensure_hixl_binding()
            ensure_locked.assert_not_called()

    def test_missing_pybind11_hint(self):
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.dict(sys.modules, {"pybind11": None}))
            # 密封化: 宿主未装 python3-dev 时不让头文件检查先失败
            stack.enter_context(
                mock.patch.object(
                    self.mod._PythonContext,
                    "_init_development_component",
                    lambda s: None,
                )
            )
            with self.assertRaisesRegex(RuntimeError, "pybind11"):
                self.mod._PythonContext()

    def test_missing_python_header_hint(self):
        with mock.patch.object(
            self.mod.sysconfig, "get_path", return_value=str(self.tmpdir)
        ):
            with self.assertRaisesRegex(RuntimeError, "python3-dev"):
                self.mod._PythonContext()

    def test_missing_cmake_hint(self):
        with mock.patch.object(self.mod.shutil, "which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "cmake"):
                self.mod._CMakeContext()

    def test_python_version_floor(self):
        # namedtuple 同时支持比较与 .minor 属性访问
        fake_version = collections.namedtuple("version_info", "major minor")(3, 8)
        manager = self._new_manager(self.tmpdir / "pkg")
        with mock.patch.object(self.mod.sys, "version_info", fake_version):
            with self.assertRaisesRegex(RuntimeError, "3.9"):
                manager.ensure_hixl_binding()

    def test_cann_context_error_without_cann(self):
        env = {
            "ASCEND_HOME_PATH": str(self.tmpdir),
            "CANN_PATH": "",
            "ASCEND_OPP_PATH": "",
        }
        with mock.patch.dict(os.environ, env):
            with self.assertRaisesRegex(RuntimeError, "CANN"):
                self.mod._CannContext()

    def test_cache_hit_returns_without_compile(self):
        pkg_dir = self.tmpdir / "pkg"
        cache_dir = self.tmpdir / "cache"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        cache_dir.mkdir()
        so_name = "hixl" + self._ext_suffix()
        (cache_dir / so_name).write_bytes(b"fake")
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = cache_dir
        manager._write_build_meta(cache_dir)
        found = manager._find_available_so()
        self.assertEqual(found, cache_dir / so_name)

    def test_lockless_probe_keeps_files(self):
        """锁外探测 (allow_discard=False) 校验失败不删文件, 交给锁内处理."""
        pkg_dir = self.tmpdir / "pkg"
        cache_dir = self.tmpdir / "cache"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        cache_dir.mkdir()
        so_name = "hixl" + self._ext_suffix()
        (cache_dir / so_name).write_bytes(b"fake")
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = cache_dir
        # meta 缺失 → 校验失败; 锁外: 返回 None 但文件保留
        self.assertIsNone(manager._find_available_so(allow_discard=False))
        self.assertTrue((cache_dir / so_name).exists())
        # 锁内 (默认): 删除陈旧产物
        self.assertIsNone(manager._find_available_so())
        self.assertFalse((cache_dir / so_name).exists())

    def test_stale_upgraded_so_not_fast_pathed(self):
        """升级换包后 (build_timestamp 变化) 旧在线编译产物不走快路径."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        so_name = "hixl" + self._ext_suffix()
        (pkg_dir / so_name).write_bytes(b"old")
        manager._write_build_meta(pkg_dir)
        manager.build_timestamp = "newer_build"  # 模拟升级换包
        self.assertIsNone(manager._probe_pkg_dir_binding())
        # 锁内路径会将其清理
        self.assertIsNone(manager._find_available_so())
        self.assertFalse((pkg_dir / so_name).exists())

    def test_prebuilt_with_shipped_meta_fast_path(self):
        """wheel 预编译产物 (cpython 命名) 附带随包 meta 时正常走快路径."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        manager = self._new_manager(pkg_dir)
        so_name = "hixl" + self._ext_suffix()
        (pkg_dir / so_name).write_bytes(b"prebuilt")
        manager._write_build_meta(pkg_dir)  # 模拟随包 meta (时间戳一致)
        self.assertEqual(manager._probe_pkg_dir_binding(), pkg_dir / so_name)
        self.assertEqual(manager._find_available_so(), pkg_dir / so_name)

    def test_prebuilt_without_meta_rejected(self):
        """cpython 命名但无 meta (异常形态): 判陈旧, 只读目录下不抛异常."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        so_name = "hixl" + self._ext_suffix()
        (pkg_dir / so_name).write_bytes(b"prebuilt")
        self.assertIsNone(manager._probe_pkg_dir_binding())
        # _discard_cache 删除失败 (如只读目录) 不抛异常
        with mock.patch.object(
            Path, "unlink", side_effect=PermissionError("read-only")
        ):
            self.assertIsNone(manager._find_available_so())

    def test_wait_for_pkg_dir_does_not_revive_stale(self):
        """残留锁文件 + 无持锁者 + 陈旧产物: 等待路径不得复活陈旧绑定."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        so_name = "hixl" + self._ext_suffix()
        (pkg_dir / so_name).write_bytes(b"old")
        manager._write_build_meta(pkg_dir)
        manager.build_timestamp = "newer_build"  # 模拟升级换包
        manager._cache_dir_value = self.tmpdir / "cache"  # 跨目录场景, 走探测路径
        (pkg_dir / manager._lock_name).write_text(
            "", encoding="utf-8"
        )  # 残留锁, 无人持有
        # 残留锁无人持有: 首轮探测即获锁, 不发生实际等待 sleep
        with mock.patch.object(
            self.mod.time, "sleep", side_effect=AssertionError("should not sleep")
        ):
            self.assertFalse(manager._wait_for_pkg_dir_compilation())

    def test_gen_prebuilt_meta_emits_wheel_meta(self):
        """gen_manifest --prebuilt-so 生成轻量 schema 的随包 meta (轻校验字段)."""
        import importlib.util as _ilu

        repo_py_dir = Path(__file__).resolve().parents[2] / "src" / "python" / "hixl_py"
        spec = _ilu.spec_from_file_location(
            "gm_under_test", repo_py_dir / "gen_manifest.py"
        )
        gm = _ilu.module_from_spec(spec)
        spec.loader.exec_module(gm)

        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        so_path = pkg_dir / "hixl.cpython-313-x86_64-linux-gnu.so"
        so_path.write_bytes(b"prebuilt-bytes")
        gm.gen_prebuilt_meta(so_path, "20261111_000000")

        meta_path = pkg_dir / ".build_meta.cp313"
        self.assertTrue(meta_path.is_file())
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        # 轻量 schema: 只含运行时轻校验读取的两个字段
        self.assertEqual(
            meta, {"python_tag": "cp313", "build_timestamp": "20261111_000000"}
        )

    def test_legacy_generic_so_not_trusted(self):
        """旧包遗留的通用名 hixl.so 不再被信任加载 (换 Python 后会重现 ABI 错误)."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        (pkg_dir / "hixl.so").write_bytes(b"legacy")
        self.assertIsNone(manager._probe_pkg_dir_binding())
        self.assertIsNone(manager._find_available_so())

    def test_meta_not_dict_rejected(self):
        """meta 为合法 JSON 但非对象 (如 []) 时按校验失败处理, 不抛异常."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        (pkg_dir / ("hixl" + self._ext_suffix())).write_bytes(b"so")
        (pkg_dir / manager._meta_name).write_text("[]", encoding="utf-8")
        self.assertIsNone(manager._probe_pkg_dir_binding())

    def test_run_cmd_tolerates_non_utf8_output(self):
        """子进程输出非 UTF-8 字节: 解码容错, 失败路径抛 BuildOnlineError 而非解码异常."""
        # ① 成功路径: 输出含非法字节, 不抛
        self.mod._CMakeContext._run_cmd(
            [
                sys.executable,
                "-c",
                "import sys; sys.stdout.buffer.write(b'ok\\xff\\xfe')",
            ]
        )
        # ② 失败路径: stderr 含非法字节 + 非零退出 → BuildOnlineError 携带替换后的摘要
        with self.assertRaises(self.mod.BuildOnlineError) as ctx:
            self.mod._CMakeContext._run_cmd(
                [
                    sys.executable,
                    "-c",
                    "import sys; sys.stderr.buffer.write(b'err\\xff\\xfe'); sys.exit(1)",
                ]
            )
        self.assertIn("err", str(ctx.exception))

    def test_unresolvable_home_raises_build_online_error(self):
        """home 无法解析 (Path.home 抛 RuntimeError): 转为契约内 BuildOnlineError."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                mock.patch.object(self.mod.os, "getuid", return_value=12345)
            )
            stack.enter_context(
                mock.patch.object(
                    self.mod.Path,
                    "home",
                    side_effect=RuntimeError("Could not resolve homePath"),
                )
            )
            with self.assertRaises(self.mod.BuildOnlineError):
                manager._compute_cache_dir()

    def test_manifest_invalid_utf8_raises_build_online_error(self):
        """随包清单非法编码: 抛 BuildOnlineError (契约内), 不抛 UnicodeDecodeError."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        manager = self._new_manager(pkg_dir)
        (pkg_dir / "_src" / "MANIFEST.sha256").write_bytes(b"\xff\xfe bad")
        with self.assertRaises(self.mod.BuildOnlineError):
            manager._verify_manifest()

    def test_full_verify_survives_concurrent_removal(self):
        """全量校验期间产物被并发删除: 按校验失败返回 False, 不抛异常."""
        pkg_dir = self.tmpdir / "pkg"
        cache_dir = self.tmpdir / "cache"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        cache_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = cache_dir
        (cache_dir / ("hixl" + self._ext_suffix())).write_bytes(b"fake")
        manager._write_build_meta(cache_dir)
        with mock.patch.object(
            self.mod,
            "_sha256_file",
            side_effect=FileNotFoundError("deleted by lock holder"),
        ):
            # 锁外探测: 校验失败但不炸, 继续走等待与锁内复核
            self.assertFalse(manager._verify_build_meta(cache_dir))
            self.assertIsNone(manager._find_available_so(allow_discard=False))

    def test_meta_invalid_utf8_rejected(self):
        """meta 内容非法 UTF-8 时按校验失败处理, 不抛契约外异常."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        (pkg_dir / ("hixl" + self._ext_suffix())).write_bytes(b"so")
        (pkg_dir / manager._meta_name).write_bytes(b"\xff\xfe\x00bad")
        self.assertIsNone(manager._probe_pkg_dir_binding())

    def test_which_cmake_rejects_python_entry(self):
        """pip cmake 的 python console-script 入口 (shebang 含 python) 被拒绝, ELF 通过."""
        fake_bin = self.tmpdir / "bin"
        fake_bin.mkdir()
        py_entry = fake_bin / "cmake"
        py_entry.write_text("#!/usr/bin/env python3\nimport cmake\n", encoding="utf-8")
        elf_entry = fake_bin / "cmake"
        with mock.patch.object(self.mod.shutil, "which", return_value=str(py_entry)):
            self.assertIsNone(self.mod._CMakeContext._which_cmake())
        elf_entry.write_bytes(b"\x7fELF fake binary")
        with mock.patch.object(self.mod.shutil, "which", return_value=str(elf_entry)):
            self.assertIsNotNone(self.mod._CMakeContext._which_cmake())

    def test_wait_skipped_when_cache_is_pkg_dir(self):
        """缓存目录即 pkg_dir 时跳过轮询等待 (阻塞 flock 本身就是等待)."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = pkg_dir
        (pkg_dir / manager._lock_name).write_text("", encoding="utf-8")
        with mock.patch.object(
            manager, "_poll_until", side_effect=AssertionError("should not poll")
        ):
            self.assertFalse(manager._wait_for_pkg_dir_compilation())

    def test_discard_removes_legacy_generic_too(self):
        """清理陈旧产物时一并移除旧包遗留的通用名孤儿 hixl.so."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        (pkg_dir / "hixl.so").write_bytes(b"legacy")
        (pkg_dir / ("hixl" + self._ext_suffix())).write_bytes(b"stale")
        manager._discard_cache(pkg_dir)
        self.assertFalse((pkg_dir / "hixl.so").exists())
        self.assertFalse((pkg_dir / ("hixl" + self._ext_suffix())).exists())

    def test_cache_meta_tamper_discards_cache(self):
        pkg_dir = self.tmpdir / "pkg"
        cache_dir = self.tmpdir / "cache"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        cache_dir.mkdir()
        so_name = "hixl" + self._ext_suffix()
        (cache_dir / so_name).write_bytes(b"fake")
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = cache_dir
        (cache_dir / manager._meta_name).write_text(
            json.dumps({"manifest_hash": "bad", "python_tag": "cp99"}), encoding="utf-8"
        )
        found = manager._find_available_so()
        self.assertIsNone(found)
        self.assertFalse((cache_dir / so_name).exists())
        self.assertFalse((cache_dir / manager._meta_name).exists())

    def test_cache_so_tamper_discards_cache(self):
        pkg_dir = self.tmpdir / "pkg"
        cache_dir = self.tmpdir / "cache"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        cache_dir.mkdir()
        so_name = "hixl" + self._ext_suffix()
        (cache_dir / so_name).write_bytes(b"fake")
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = cache_dir
        manager._write_build_meta(cache_dir)
        (cache_dir / so_name).write_bytes(b"poisoned")
        found = manager._find_available_so()
        self.assertIsNone(found)
        self.assertFalse((cache_dir / so_name).exists())
        self.assertFalse((cache_dir / manager._meta_name).exists())

    def test_multi_python_meta_coexistence(self):
        pkg_dir = self.tmpdir / "pkg"
        cache_dir = self.tmpdir / "cache"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        cache_dir.mkdir()
        so_name = "hixl" + self._ext_suffix()
        (cache_dir / so_name).write_bytes(b"fake")
        first = self._new_manager(pkg_dir)
        first.python_tag = "cp310"
        first._meta_name = ".build_meta.cp310"
        first._lock_name = ".hixl_build.cp310.lock"
        first._cache_dir_value = cache_dir
        second = self._new_manager(pkg_dir)
        second.python_tag = "cp311"
        second._meta_name = ".build_meta.cp311"
        second._lock_name = ".hixl_build.cp311.lock"
        second._cache_dir_value = cache_dir

        first._write_build_meta(cache_dir)
        self.assertTrue((cache_dir / ".build_meta.cp310").exists())
        self.assertFalse((cache_dir / ".build_meta.cp311").exists())

        # 未编译版本校验不通过, 但不触碰已编译版本的 meta
        self.assertFalse(second._verify_build_meta(cache_dir))
        self.assertTrue((cache_dir / ".build_meta.cp310").exists())

        # 另一版本写入自己的 meta 后, 双方校验互不影响 (核心回归断言)
        second._write_build_meta(cache_dir)
        self.assertTrue(first._verify_build_meta(cache_dir))
        self.assertTrue(second._verify_build_meta(cache_dir))

        # 丢弃缓存只清理自己的 meta
        second._discard_cache(cache_dir)
        self.assertFalse((cache_dir / ".build_meta.cp311").exists())
        self.assertTrue((cache_dir / ".build_meta.cp310").exists())

    def test_manifest_tamper_rejects_compile(self):
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        manager = self._new_manager(pkg_dir)
        (pkg_dir / "_src" / "hixl_py.cc").write_text("// tampered", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "MANIFEST.sha256"):
            manager._verify_manifest()

    def test_manifest_missing_rejects_compile(self):
        pkg_dir = self.tmpdir / "pkg"
        (pkg_dir / "_src").mkdir(parents=True)
        manager = self._new_manager(pkg_dir)
        with self.assertRaisesRegex(RuntimeError, "manifest not found"):
            manager._verify_manifest()

    def test_flock_mutual_exclusion(self):
        lock_path = self.tmpdir / ".hixl_build.lock"
        fd_first = open(lock_path, "w")
        fd_second = open(lock_path, "w")
        try:
            self.assertTrue(self.mod.BuildOnlineHixlManager._try_acquire_lock(fd_first))
            self.assertFalse(
                self.mod.BuildOnlineHixlManager._try_acquire_lock(fd_second)
            )
            fcntl.flock(fd_first, fcntl.LOCK_UN)
            self.assertTrue(
                self.mod.BuildOnlineHixlManager._try_acquire_lock(fd_second)
            )
        finally:
            fd_first.close()
            fd_second.close()

    def test_wait_for_pkg_dir_compilation_shares_result(self):
        """持锁者活着时等待; 编完释放后命中 pkg_dir 产物, 锁文件残留无害."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = self.tmpdir / "cache"  # 跨目录场景
        so_name = "hixl" + self._ext_suffix()
        lock_path = pkg_dir / manager._lock_name
        holder_fd = open(lock_path, "w")  # 模拟持锁编译进程
        fcntl.flock(holder_fd, fcntl.LOCK_EX)
        (pkg_dir / so_name).write_bytes(b"fake")
        manager._write_build_meta(pkg_dir)  # 编完的完整产物含 meta
        try:
            # 持锁期间: 探测发现有人编译, 轮询等待
            state = {"waited": False}

            def fake_poll(predicate, **kwargs):
                state["waited"] = True
                self.assertFalse(predicate())  # 持锁者仍活着
                fcntl.flock(holder_fd, fcntl.LOCK_UN)  # 编译结束, 释放锁
                self.assertTrue(predicate())  # 探测立即成功
                return True

            with mock.patch.object(manager, "_poll_until", side_effect=fake_poll):
                self.assertTrue(manager._wait_for_pkg_dir_compilation())
            self.assertTrue(state["waited"])
        finally:
            holder_fd.close()

    def test_wait_for_pkg_dir_skips_idle_lock_file(self):
        """锁文件常驻: 无存活持锁者 (残留文件) 时不等待, 直接查产物."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = self.tmpdir / "cache"  # 跨目录场景
        so_name = "hixl" + self._ext_suffix()
        lock_path = pkg_dir / manager._lock_name
        lock_path.write_text("", encoding="utf-8")  # 残留锁文件, 无人持有
        (pkg_dir / so_name).write_bytes(b"fake")
        manager._write_build_meta(pkg_dir)  # 完整产物含 meta
        # 空闲锁首轮谓词即真, 不发生任何等待 sleep
        with mock.patch.object(
            self.mod.time, "sleep", side_effect=AssertionError("should not sleep")
        ):
            self.assertTrue(manager._wait_for_pkg_dir_compilation())

    def test_ensure_locked_keeps_lock_file(self):
        """编译完成后锁文件不删除 (常驻), 避免 inode 竞态."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        self._make_stub_src(pkg_dir / "_src")
        manager = self._new_manager(pkg_dir)
        out_dir = self.tmpdir / "out"
        out_dir.mkdir()
        so_path = out_dir / ("hixl" + self._ext_suffix())
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                mock.patch.object(manager, "_find_available_so", return_value=None)
            )
            stack.enter_context(
                mock.patch.object(
                    manager, "_wait_for_pkg_dir_compilation", return_value=False
                )
            )
            stack.enter_context(
                mock.patch.object(manager, "_cache_dir", return_value=out_dir)
            )
            stack.enter_context(
                mock.patch.object(manager, "_do_compile", return_value=so_path)
            )
            stack.enter_context(mock.patch.object(manager, "_load_from_cache"))
            manager._ensure_locked()
        self.assertTrue(manager._target_compiled)
        self.assertTrue((self.tmpdir / "out" / manager._lock_name).exists())

    def test_load_registers_module_before_exec(self):
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        so_path = self.tmpdir / "hixl_fake.so"
        so_path.write_bytes(b"fake")
        order = []

        def fake_exec(_module):
            order.append(_DOTTED_NAME in sys.modules)

        spec = mock.Mock()
        spec.loader.exec_module.side_effect = fake_exec
        created = mock.Mock()
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                mock.patch.object(
                    self.mod.importlib.util,
                    "spec_from_file_location",
                    return_value=spec,
                )
            )
            stack.enter_context(
                mock.patch.object(
                    self.mod.importlib.util, "module_from_spec", return_value=created
                )
            )
            manager._load_from_cache(so_path)
        self.assertEqual(order, [True])
        self.assertIs(sys.modules.get(_DOTTED_NAME), created)

    def test_load_idempotent_when_registered(self):
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        sys.modules[_DOTTED_NAME] = mock.Mock()
        so_path = self.tmpdir / "hixl_fake.so"
        so_path.write_bytes(b"fake")
        with mock.patch.object(
            self.mod.importlib.util, "spec_from_file_location"
        ) as factory:
            manager._load_from_cache(so_path)
            factory.assert_not_called()

    def test_cache_dir_fallback_when_pkg_not_writable(self):
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        env = {
            "ASCEND_HOME_PATH": "",
            "CANN_PATH": "",
            "ASCEND_OPP_PATH": "",
        }
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                mock.patch.object(self.mod.os, "getuid", return_value=12345)
            )
            stack.enter_context(mock.patch.dict(os.environ, env))
            stack.enter_context(
                mock.patch.object(self.mod.Path, "home", return_value=self.tmpdir)
            )
            cache_dir = manager._cache_dir()
        self.assertEqual(
            cache_dir, self.tmpdir / ".cache" / "cann" / "hixl" / "0.0.1_test"
        )

    def test_cache_dir_isolated_by_cann_version(self):
        """CANN 同路径原地升级后, 缓存目录 (ascend hash) 必须变化, 旧缓存不串用."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        cann_root = self.tmpdir / "cann"
        self._make_fake_cann(cann_root)  # 完整可用根 (头+库), 缓存键才认它
        version_file = cann_root / "opp" / "version.info"

        def compute(ver_text):
            version_file.write_text(ver_text, encoding="utf-8")
            manager = self._new_manager(pkg_dir)
            env = {
                "CANN_PATH": str(cann_root),
                "ASCEND_HOME_PATH": "",
                "ASCEND_OPP_PATH": "",
            }
            with contextlib.ExitStack() as stack:
                stack.enter_context(
                    mock.patch.object(self.mod.os, "getuid", return_value=12345)
                )
                stack.enter_context(mock.patch.dict(os.environ, env))
                stack.enter_context(
                    mock.patch.object(self.mod.Path, "home", return_value=self.tmpdir)
                )
                return manager._cache_dir()

        old_dir = compute("Version=9.2.0-beta.1\nversion_dir=cann\n")
        new_dir = compute("Version=9.3.0\nversion_dir=cann\n")
        same_dir = compute("Version=9.2.0-beta.1\nversion_dir=cann\n")
        self.assertNotEqual(old_dir, new_dir)
        self.assertEqual(old_dir, same_dir)

    def test_cache_key_follows_usable_root_not_env_order(self):
        """坏的 ASCEND_HOME_PATH 不参与缓存键: 键跟随实际可用的 CANN 根."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        good_root = self.tmpdir / "cann_good"
        self._make_fake_cann(good_root)
        broken_root = self.tmpdir / "cann_broken"
        broken_root.mkdir()  # 只有目录, 无头无库 → 不可用

        def cache_with_env(home_path: str):
            manager = self._new_manager(pkg_dir)
            manager._cache_dir_value = None
            env = {
                "ASCEND_HOME_PATH": home_path,
                "CANN_PATH": str(good_root),
                "ASCEND_OPP_PATH": "",
            }
            with contextlib.ExitStack() as stack:
                stack.enter_context(
                    mock.patch.object(self.mod.os, "getuid", return_value=12345)
                )
                stack.enter_context(mock.patch.dict(os.environ, env))
                stack.enter_context(
                    mock.patch.object(self.mod.Path, "home", return_value=self.tmpdir)
                )
                return manager._compute_cache_dir()

        with_broken_home = cache_with_env(str(broken_root))
        self.assertEqual(
            with_broken_home,
            cache_with_env(str(good_root)),  # 键按可用根计算, 与坏的 HOME 值无关
        )
        self.assertNotEqual(
            with_broken_home,
            self.tmpdir
            / ".cache"
            / "cann"
            / "hixl"
            / "0.0.1_test",  # 不是无 CANN 的裸目录
        )

    def test_cann_version_missing_falls_back_to_path_only(self):
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        cann_root = self.tmpdir / "cann_empty"
        cann_root.mkdir()
        env = {
            "CANN_PATH": str(cann_root),
            "ASCEND_HOME_PATH": "",
            "ASCEND_OPP_PATH": "",
        }
        manager = self._new_manager(pkg_dir)
        with mock.patch.dict(os.environ, env):
            self.assertEqual(manager._cann_version(str(cann_root)), "")

    def test_do_compile_pins_interpreter_version(self):
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        py_ctx = mock.Mock(
            minor=11,
            ext_suffix=".cpython-311-x86_64-linux-gnu.so",
            pybind11_cmake_dir=Path("/fake"),
        )
        cmake_ctx = mock.Mock()
        cann_ctx = mock.Mock(
            include_dir=Path("/fake/include"), lib_dir=Path("/fake/lib64")
        )
        captured = {}

        def fake_compile(**kwargs):
            captured.update(kwargs)

        cmake_ctx.compile.side_effect = fake_compile
        with contextlib.ExitStack() as stack:
            for target, value in (
                ("_PythonContext", py_ctx),
                ("_CMakeContext", cmake_ctx),
                ("_CannContext", cann_ctx),
            ):
                stack.enter_context(
                    mock.patch.object(self.mod, target, return_value=value)
                )
            stderr_buf = io.StringIO()
            with contextlib.redirect_stderr(stderr_buf):
                manager._do_compile(self.tmpdir / "out")
        ext = captured["cfg_cmd_ext"]
        self.assertNotIn("-DHIXL_ONLINE_STANDALONE", " ".join(ext))
        self.assertIn("-DPython3_EXECUTABLE_VERSION=3.11", ext)
        self.assertIn(f"-DPython3_EXECUTABLE={sys.executable}", ext)
        # 首次编译在 stderr 打一行提示, 避免无 handler 环境下像卡死
        self.assertIn(manager.python_tag, stderr_buf.getvalue())

    def test_ensure_hixl_binding_converts_oserror(self):
        """环境类 OSError 统一转 BuildOnlineError, 维持 ImportError 契约."""
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                mock.patch.object(
                    self.mod.BuildOnlineHixlManager,
                    "_instances",
                    {self.mod.BuildOnlineHixlManager: manager},
                )
            )
            stack.enter_context(
                mock.patch.object(
                    manager,
                    "ensure_hixl_binding",
                    side_effect=PermissionError("read-only home"),
                )
            )
            with self.assertRaises(self.mod.BuildOnlineError):
                self.mod.ensure_hixl_binding()

    def test_ensure_returns_gracefully_without_src(self):
        pkg_dir = self.tmpdir / "pkg"
        pkg_dir.mkdir()
        manager = self._new_manager(pkg_dir)
        manager._cache_dir_value = self.tmpdir / "cache"
        manager.ensure_hixl_binding()
        self.assertFalse(manager._target_compiled)


class TestGenManifestExtractOnline(unittest.TestCase):
    """gen_manifest.py 按 marker 提取在线段生成随包 CMakeLists."""

    @classmethod
    def setUpClass(cls):
        repo_py_dir = Path(__file__).resolve().parents[2] / "src" / "python" / "hixl_py"
        spec = importlib.util.spec_from_file_location(
            "gen_manifest_under_test", repo_py_dir / "gen_manifest.py"
        )
        cls.mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.mod)
        cls.repo_cmakelists = repo_py_dir / "CMakeLists.txt"

    def test_extract_from_real_repo_cmakelists(self):
        shipped = self.mod.extract_online_cmake(self.repo_cmakelists)
        self.assertIn("cmake_minimum_required(VERSION 3.16)", shipped)
        self.assertIn("hixl_python_binding_online", shipped)
        self.assertIn("install(TARGETS hixl", shipped)
        for must_absent in (
            "HIXL_ONLINE_STANDALONE",
            "return()",
            "execute_process",
            "add_custom_command",
            "hixl-0.0.1-py3-none-any.whl",
            self.mod.ONLINE_BEGIN,
        ):
            self.assertNotIn(must_absent, shipped)

    def test_extract_dedents_body(self):
        shipped = self.mod.extract_online_cmake(self.repo_cmakelists)
        for line in shipped.splitlines():
            if line.strip() and "    cmake_minimum" in line:
                self.fail("online body not dedented")

    def test_missing_markers_raises(self):
        bad = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, bad, ignore_errors=True)
        path = bad / "CMakeLists.txt"
        path.write_text("# no markers here\n", encoding="utf-8")
        with self.assertRaises(RuntimeError):
            self.mod.extract_online_cmake(path)

    def test_gen_manifest_writes_cmake_and_manifest(self):
        src_dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, src_dir, ignore_errors=True)
        (src_dir / "hixl_py.h").write_text("x", encoding="utf-8")
        self.mod.gen_manifest(src_dir, self.repo_cmakelists)
        manifest = (src_dir / self.mod.MANIFEST_NAME).read_text(encoding="utf-8")
        self.assertIn("CMakeLists.txt", manifest)
        self.assertIn("hixl_py.h", manifest)

    def test_extract_keeps_license_closing_separator(self):
        shipped = self.mod.extract_online_cmake(self.repo_cmakelists)
        lines = shipped.splitlines()
        seps = [
            i
            for i, line in enumerate(lines[:14])
            if line.startswith("# ---") and set(line.strip()) <= {"#", "-", " "}
        ]
        self.assertEqual(
            len(seps), 2, "license header must keep opening+closing separators"
        )
        self.assertLess(
            seps[-1], 12, "closing separator must appear before generated comment"
        )


class TestBuildOnlineError(unittest.TestCase):
    def test_build_online_error_is_import_error(self):
        import importlib.util

        repo_py_dir = Path(__file__).resolve().parents[2] / "src" / "python" / "hixl_py"
        spec = importlib.util.spec_from_file_location(
            "bo_err", repo_py_dir / "hixl" / "_build_online.py"
        )
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        self.assertTrue(issubclass(mod.BuildOnlineError, RuntimeError))
        self.assertTrue(issubclass(mod.BuildOnlineError, ImportError))


if __name__ == "__main__":
    unittest.main()
