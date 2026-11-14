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
"""hixl 绑定层在线编译基础设施.

本模块不 import hixl.hixl (pybind11 扩展), 确保扩展 .so 缺失时仍可正常加载.
当用户 Python 版本与预编译产物不匹配时, 基于随包源码 (_src/) 现场编译绑定层并缓存.
"""

import fcntl
import hashlib
import importlib.machinery
import importlib.util
import json
import logging
import os
import re
import shutil
import subprocess
import sys
import sysconfig
import tempfile
import threading
import time
from pathlib import Path
from typing import Any, Callable, Dict, Iterator, List, Optional

_log = logging.getLogger(__name__)


class BuildOnlineError(RuntimeError, ImportError):
    """在线编译失败异常.

    继承 ImportError 以保持与旧 wheel 行为兼容 (缺 .so 时为 ModuleNotFoundError),
    下游 `try: import hixl except ImportError:` 的降级保护不会被 RuntimeError 漏接;
    同时保留 RuntimeError 语义, 标识这是运行时环境问题而非模块缺失.
    """


_MODULE_STEM = "hixl"
_DOTTED_NAME = "hixl.hixl"
_SRC_DIR_NAME = "_src"
_MANIFEST_NAME = "MANIFEST.sha256"
_META_PREFIX = ".build_meta"
_MIN_PY_MINOR = 9
_PYBIND11_MIN_VERSION = (2, 13, 6)
_FLOCK_TIMEOUT = 300  # unit: second

# 在线编译所需的 CANN 头文件, 相对 <cann>/include 的路径
# (绑定层已随仓内重构去除内部头依赖, 仅依赖 hixl 公共头)
_CANN_INCLUDE_HEADERS = (
    "hixl/hixl.h",
    "hixl/hixl_types.h",
    "external/ge_common/ge_api_error_codes.h",
)
# 在线编译需链接的共享库 (nm 实测绑定层符号引用: hixl::Hixl::* 在 libcann_hixl,
# ge::AscendString::* 在 libmetadef; 其余依赖经 libcann_hixl 传递闭包由 loader 解析)
_CANN_LINK_LIBS = (
    "cann_hixl",
    "metadef",
)


def _sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class _PythonContext:
    """校验当前解释器环境 (Python 版本 / 开发头文件 / pip pybind11)."""

    def __init__(self):
        # 版本门槛已在 ensure_hixl_binding 前置检查, 此处仅取值
        self.minor = int(sys.version_info.minor)
        suffix = sysconfig.get_config_var("EXT_SUFFIX")
        self.ext_suffix = suffix if suffix else ".so"
        self.pybind11_cmake_dir: Optional[Path] = None
        self._init_development_component()
        self._init_pip_pybind11()

    def _init_development_component(self):
        python_h = Path(sysconfig.get_path("include")) / "Python.h"
        if not python_h.exists():
            raise BuildOnlineError(
                f"Python development headers not found (expected {python_h}).\n"
                "Hint: install python3-dev (e.g. `apt install python3-dev`"
                " or `yum install python3-devel`)."
            )

    def _init_pip_pybind11(self):
        try:
            import pybind11
        except ImportError as e:
            raise BuildOnlineError(
                "pybind11 pip package not found.\n"
                "Hint: install it with `pip install pybind11>=2.13.6`."
            ) from e
        version_match = re.match(r"(\d+)\.(\d+)\.(\d+)", pybind11.__version__)
        current_version = (
            tuple(int(x) for x in version_match.groups())
            if version_match
            else (0, 0, 0)
        )
        if current_version < _PYBIND11_MIN_VERSION:
            raise BuildOnlineError(
                f"pybind11 version {pybind11.__version__} is too old,"
                " require >= 2.13.6.\n"
                "Hint: upgrade it with `pip install pybind11>=2.13.6`."
            )
        pybind11_dir = Path(pybind11.get_cmake_dir()).resolve()
        if not pybind11_dir.exists():
            raise BuildOnlineError(
                "pybind11 cmake dir not found.\n"
                "Hint: install it with `pip install pybind11>=2.13.6`."
            )
        self.pybind11_cmake_dir = pybind11_dir


class _CannContext:
    """探测 CANN 安装路径并校验在线编译所需的头文件与库."""

    def __init__(self):
        self.include_dir: Optional[Path] = None
        self.lib_dir: Optional[Path] = None
        self._init_cann_dirs()

    @staticmethod
    def _find_usable_root() -> tuple:
        """返回 (可用根, 最近一次检查的缺失项列表); 无可用根时根为 None.

        缓存键与编译上下文共用此选根逻辑, 保证键与实际参与编译的 CANN 一致。
        """
        problems: List[str] = []
        for root in _CannContext._candidate_roots():
            include_dir = _CannContext._resolve_subdir(root, "include")
            if include_dir is None:
                continue
            problems = _CannContext._collect_missing(
                include_dir, _CannContext._resolve_subdir(root, "lib64")
            )
            if not problems:
                return root, problems
        return None, problems

    def _init_cann_dirs(self):
        root, problems = self._find_usable_root()
        if root is not None:
            self.include_dir = _CannContext._resolve_subdir(root, "include")
            self.lib_dir = _CannContext._resolve_subdir(root, "lib64")
            return
        detail = "; ".join(problems) if problems else "no CANN installation found"
        raise BuildOnlineError(
            f"Can not locate a usable CANN environment for online compilation: {detail}.\n"
            "Hint: source CANN set_env.sh (set ASCEND_HOME_PATH), make sure HIXL "
            "is installed into this CANN environment."
        )

    @staticmethod
    def _candidate_roots() -> List[Path]:
        roots: List[Path] = []
        for env_key in ("ASCEND_HOME_PATH", "CANN_PATH"):
            value = os.environ.get(env_key)
            if value:
                roots.append(Path(value))
        opp_path = os.environ.get("ASCEND_OPP_PATH")
        if opp_path:
            roots.append(Path(opp_path).parent)
        return roots

    @staticmethod
    def _resolve_subdir(root: Path, name: str) -> Optional[Path]:
        direct = root / name
        if direct.is_dir():
            return direct
        arch_dir = root / f"{os.uname().machine}-linux" / name
        if arch_dir.is_dir():
            return arch_dir
        return None

    @staticmethod
    def _collect_missing(include_dir: Path, lib_dir: Optional[Path]) -> List[str]:
        missing: List[str] = []
        for header in _CANN_INCLUDE_HEADERS:
            if not (include_dir / header).is_file():
                missing.append(f"header '{header}' not found under {include_dir}")
        if lib_dir is None:
            missing.append("lib64 not found")
        else:
            for name in _CANN_LINK_LIBS:
                if not (lib_dir / f"lib{name}.so").is_file():
                    missing.append(f"library 'lib{name}.so' not found under {lib_dir}")
        return missing


class _CMakeContext:
    """定位系统级 cmake 并执行在线编译 (configure / build / install)."""

    def __init__(self):
        self.cmake = self._which_cmake()
        if self.cmake is None:
            raise BuildOnlineError(
                "Can not find cmake, please check your environment.\n"
                "Hint: install system-level cmake (e.g. `apt install cmake`"
                " or `yum install cmake`), the cmake pip package is NOT a valid substitute."
            )

    @staticmethod
    def _which_cmake() -> Optional[Path]:
        executable = shutil.which("cmake")
        if executable is None:
            return None
        path = Path(executable).resolve()
        # pip 安装的 cmake: 路径位于 site-packages/dist-packages, 或入口是
        # python console-script (venv --user 场景 resolve 后不含包路径), 均非系统 cmake
        if "site-packages" in path.parts or "dist-packages" in path.parts:
            return None
        try:
            head = path.read_bytes()[:128]
        except OSError:
            return None
        if head.startswith(b"#!") and b"python" in head.split(b"\n", 1)[0].lower():
            return None
        return path

    def compile(self, src_dir: Path, install_prefix: Path, cfg_cmd_ext: List[str]):
        with tempfile.TemporaryDirectory(prefix=".hixl_online_build.") as tmp_dir:
            build_dir = Path(tmp_dir, "build")
            build_dir.mkdir(parents=True)
            configure_cmd = [
                str(self.cmake),
                "-S",
                str(src_dir),
                "-B",
                str(build_dir),
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_INSTALL_PREFIX={install_prefix}",
                *cfg_cmd_ext,
            ]
            self._run_cmd(configure_cmd)
            # 绑定层为单翻译单元, 并行度无作用对象, 不传 -j
            build_cmd = [
                str(self.cmake),
                "--build",
                str(build_dir),
            ]
            self._run_cmd(build_cmd)
            install_cmd = [
                str(self.cmake),
                "--install",
                str(build_dir),
                f"--prefix={install_prefix}",
            ]
            self._run_cmd(install_cmd)

    @staticmethod
    def _run_cmd(cmd: List[str]):
        _log.info("run cmd: %s", " ".join(cmd))
        # errors="replace": 编译器诊断在非 UTF-8 locale 下含非法字节 (如 GBK),
        # 严格解码会在读取输出时抛 UnicodeDecodeError 逃逸契约; 诊断信息以替换符呈现即可
        result = subprocess.run(
            cmd, text=True, encoding="utf-8", errors="replace", capture_output=True
        )
        if result.returncode != 0:
            _log.error("cmd: %s, ret: %s", " ".join(cmd), result.returncode)
            _log.error("stdout:\n%s", result.stdout)
            _log.error("stderr:\n%s", result.stderr)
            # 转抛 BuildOnlineError 使下游 except ImportError 降级保护能接住编译失败
            # 注意: f-string 表达式里不能用反斜杠 (Python < 3.12 限制), 先 join 再引用
            stderr_tail = (result.stderr or "").strip().split("\n")[-5:]
            stderr_text = "\n".join(stderr_tail)
            raise BuildOnlineError(
                f"Command failed (ret={result.returncode}): {' '.join(cmd)}\n"
                f"stderr (last 5 lines): {stderr_text}"
            )
        if result.returncode == 0 and result.stderr:
            _log.debug("cmd stderr: %s", result.stderr)


class BuildOnlineHixlManager:
    """hixl 绑定层在线编译管理器 (进程内单例)."""

    _instances: Dict[type, Any] = {}
    _instance_lock: threading.Lock = threading.Lock()
    _compile_lock: threading.Lock = threading.Lock()

    def __new__(cls):
        if cls not in cls._instances:
            with cls._instance_lock:
                if cls not in cls._instances:
                    cls._instances[cls] = super().__new__(cls)
        return cls._instances[cls]

    def __init__(self):
        if getattr(self, "_initialized", False):
            return
        self.pkg_dir: Path = Path(__file__).resolve().parent
        self.src_dir: Path = self.pkg_dir / _SRC_DIR_NAME
        self.version, self.build_timestamp = self._load_build_info()
        ver_info = sys.version_info
        self.python_tag = f"cp{ver_info.major}{ver_info.minor}"
        self._lock_name = f".hixl_build.{self.python_tag}.lock"
        # 缓存元数据按 Python 版本命名: 多版本共享同一缓存目录时,
        # 各版本独立读写/丢弃自己的 meta 文件, 互不干扰
        self._meta_name = f"{_META_PREFIX}.{self.python_tag}"
        # 快速判断仅搜 pkg_dir, 不触发缓存目录的 I/O
        self._target_compiled = self._probe_pkg_dir_binding() is not None
        self._cache_dir_value: Optional[Path] = None
        self._initialized = True

    def _probe_pkg_dir_binding(self) -> Optional[Path]:
        """import 快路径探测: pkg_dir 有可用产物即视为已就绪.

        cpython 命名产物 (预编译随包/在线编译) 按构建时间戳判陈旧, 防止升级换包
        后继续加载旧版本绑定; 通用名产物 (hixl.so 等) 只可能来自旧包遗留, 换
        Python 版本后加载会重现 undefined symbol, 视为不存在 (在线编译写出
        cpython 命名产物后, 原生 import 按 EXT_SUFFIX 顺序必然优先命中新产物)。
        """
        found = self._search_dir(self.pkg_dir)
        if found is None or ".cpython-" not in found.name:
            return None
        if not self._verify_build_meta(self.pkg_dir, full=False):
            return None
        return found

    @staticmethod
    def _load_build_info() -> tuple:
        try:
            from . import _build_info

            return _build_info.VERSION, _build_info.BUILD_TIMESTAMP
        except ImportError:
            return "dev", "dev"

    @staticmethod
    def _search_dir(directory: Optional[Path]) -> Optional[Path]:
        """按 import 后缀优先级在指定目录搜索绑定 .so."""
        if directory is None:
            return None
        for suffix in importlib.machinery.EXTENSION_SUFFIXES:
            candidate = directory / f"{_MODULE_STEM}{suffix}"
            if candidate.exists():
                return candidate
        return None

    def ensure_hixl_binding(self):
        """确保绑定模块可用: 预编译/缓存命中直接返回, 否则在线编译."""
        # 版本检查前移: 旧解释器上 usedforsecurity 等 3.9+ 特性会先抛
        # TypeError, 抢在本应给出的版本提示之前
        if sys.version_info < (3, _MIN_PY_MINOR):
            raise BuildOnlineError(
                f"Python 3.{sys.version_info.minor} is not supported, require >= 3.{_MIN_PY_MINOR}.\n"
                f"Hint: use a Python 3.{_MIN_PY_MINOR}+ interpreter."
            )
        if self._target_compiled:
            return
        with self._compile_lock:
            if self._target_compiled:
                return
            self._ensure_locked()

    def _ensure_locked(self):
        # 锁外只探测不删除: 校验失败常因持锁进程已写 .so 未写 meta,
        # 锁外删除会误删其产物
        found = self._find_available_so(allow_discard=False)
        if found is not None:
            self._load_from_cache(found)
            self._target_compiled = True
            return
        if not self.src_dir.is_dir():
            # 源码树/异常安装形态下无随包源码, 不做在线编译,
            # 让原生 import 的报错暴露原始问题
            _log.warning(
                "Can't get hixl binding .so, but its src is empty: %s", self.src_dir
            )
            return
        self._verify_manifest()
        # 跨用户协同: 若其他进程正在往 pkg_dir 编译, 等待共享结果
        if self._wait_for_pkg_dir_compilation():
            self._target_compiled = True
            return
        target_dir = self._cache_dir()
        lock_path = target_dir / self._lock_name
        # 锁文件常驻不删: unlink 会让等待者的 fd 指向已删除 inode, 后续进程在新
        # inode 上获锁, 失去互斥; 持锁进程崩溃时 flock 自动释放, 残留文件无害
        lock_fd = open(lock_path, "w")
        try:
            fcntl.flock(lock_fd, fcntl.LOCK_EX)
            # 锁内复核: 前一持锁者可能刚编完本版本产物; 此时可安全校验并
            # 清理陈旧产物 (锁外并发进程不会再误删)
            found = self._find_available_so(allow_discard=True)
            if found is None:
                found = self._do_compile(target_dir)
            if found is None:
                raise BuildOnlineError(
                    "Online compilation succeeded but hixl binding .so not found"
                    f" in install output. Target directory: {target_dir}"
                )
            self._load_from_cache(found)
            self._target_compiled = True
        finally:
            try:
                fcntl.flock(lock_fd, fcntl.LOCK_UN)
            except OSError:
                pass
            lock_fd.close()

    def _find_available_so(self, allow_discard: bool = True) -> Optional[Path]:
        """搜索可用绑定 .so: pkg_dir 优先, cpython 命名产物需通过 meta 校验.

        预编译随包产物与在线编译产物同名同格式 (hixl.cpython-3XX-*.so), 统一由
        meta (轻校验: 构建时间戳/Python 版本) 判定可用性; 通用名产物 (hixl.so
        等) 为旧包遗留, 不信任。校验失败时仅当 allow_discard=True (已持锁) 才
        删除——锁外删除会与持锁进程 "写完 .so 未写 meta" 的窗口竞争, 误删其产物。
        """
        for directory in dict.fromkeys((self.pkg_dir, self._cache_dir())):
            found = self._search_dir(directory)
            if found is None or ".cpython-" not in found.name:
                continue  # 通用名产物为旧包遗留, 不信任 (见 _probe_pkg_dir_binding)
            if self._verify_build_meta(directory, full=directory != self.pkg_dir):
                return found
            if not allow_discard:
                # 锁外不删除, 跳过陈旧候选继续探测其他目录 (如缓存目录仍有有效产物)
                _log.warning(
                    "Binding failed integrity check, re-verify under lock: %s", found
                )
                continue
            _log.warning(
                "Cached binding failed integrity check, discard and recompile: %s",
                found,
            )
            self._discard_cache(directory)
        return None

    def _do_compile(self, target_dir: Path) -> Optional[Path]:
        # 默认日志环境无 handler, INFO 不可见; 直接 stderr 提示一次性编译,
        # 避免用户在首次导入的十余秒静默期内误认为卡死
        print(
            f"hixl: compiling python binding online for {self.python_tag}"
            " (first import only, ~10-20s)...",
            file=sys.stderr,
            flush=True,
        )
        py_ctx = _PythonContext()
        cmake_ctx = _CMakeContext()
        cann_ctx = _CannContext()
        cfg_cmd_ext = [
            f"-DPython3_EXECUTABLE={sys.executable}",
            f"-DPython3_EXECUTABLE_VERSION=3.{py_ctx.minor}",
            f"-DPY_EXT_SUFFIX={py_ctx.ext_suffix}",
            f"-DPYBIND11_CMAKE_DIR={py_ctx.pybind11_cmake_dir}",
            f"-DCANN_INCLUDE_DIR={cann_ctx.include_dir}",
            f"-DCANN_LIB_DIR={cann_ctx.lib_dir}",
        ]
        cmake_ctx.compile(
            src_dir=self.src_dir, install_prefix=target_dir, cfg_cmd_ext=cfg_cmd_ext
        )
        _log.info("Compiled and installed hixl binding to %s", target_dir)
        found = self._search_dir(target_dir)
        if found is not None:
            # pkg_dir 同样写 meta: 升级换包后旧产物按构建时间戳自动失效重编
            self._write_build_meta(target_dir)
        return found

    def _cache_dir(self) -> Path:
        """决定编译产物目标目录, 进程内单例缓存."""
        if self._cache_dir_value is not None:
            return self._cache_dir_value
        self._cache_dir_value = self._compute_cache_dir()
        return self._cache_dir_value

    def _compute_cache_dir(self) -> Path:
        # pkg_dir 归属当前用户且实测可写时装回 pkg_dir, 后续原生 import 直达
        try:
            stat_info = self.pkg_dir.stat()
            if stat_info.st_uid == os.getuid():
                test_file = self.pkg_dir / f".hixl_build.writable_test.{os.getpid()}"
                try:
                    test_file.touch()
                    test_file.unlink()
                    return self.pkg_dir
                except OSError:
                    pass
        except OSError:
            pass
        try:
            home = Path.home()
        except (RuntimeError, OSError) as e:
            # home 无法解析 (精简容器/损坏 passwd) 时 Path.home 抛 RuntimeError,
            # 不被顶层 OSError 兜底接住, 定向转为契约内异常
            raise BuildOnlineError(
                f"Cannot resolve home directory for build cache: {e!r}.\n"
                "Hint: set $HOME to a writable path."
            ) from e
        cache_dir = home / ".cache"
        cache_dir = (
            cache_dir / "cann" / "hixl" / f"{self.version}_{self.build_timestamp}"
        )
        cann_root = self._resolve_usable_cann_root()
        if cann_root is not None:
            # 隔离键 = 实际可用的 CANN 根 + 版本 (与 _CannContext 选根同判定):
            # 同路径原地升级 (覆盖安装) 后旧缓存自动失效, 坏环境变量不参与键计算
            cann_ver = self._cann_version(str(cann_root))
            ascend_hash = hashlib.md5(
                f"{cann_root}|{cann_ver}".encode(), usedforsecurity=False
            ).hexdigest()[:8]
            cache_dir = cache_dir / f"ascend_{ascend_hash}"
        cache_dir.mkdir(parents=True, exist_ok=True)
        return cache_dir

    @staticmethod
    def _resolve_usable_cann_root() -> Optional[Path]:
        """返回首个通过完整可用性检查的 CANN 根 (复用 _CannContext._find_usable_root).

        环境变量指向的根可能缺头文件/库而被跳过; 缓存键若按原始环境变量
        计算, 会与实际编译所用 CANN 脱节, 导致不同可用 CANN 共享同一缓存键。
        """
        return _CannContext._find_usable_root()[0]

    @staticmethod
    def _cann_version(ascend_path: str) -> str:
        """读取 CANN 版本 (opp/version.info 首行, 如 Version=9.2.0-beta.1).

        兼容 <cann> 与 <cann>/<arch> 两种布局, 任一缺失返回空串 (仅按路径隔离).
        """
        candidates = (Path(ascend_path), Path(ascend_path).parent)
        for base in candidates:
            try:
                first_line = (
                    (base / "opp" / "version.info")
                    .read_text(encoding="utf-8")
                    .splitlines()[0]
                )
                if first_line.startswith("Version="):
                    return first_line.split("=", 1)[1].strip()
            except (OSError, ValueError, IndexError):
                continue
        return ""

    def _wait_for_pkg_dir_compilation(self) -> bool:
        """探测 pkg_dir: 有存活持锁者在编译时等待其共享结果.

        仅跨目录场景 (自身缓存目录 != pkg_dir) 有效; 以只读 fd 尝试 flock 判定
        持锁者是否存活 (flock 不要求写权限, 持锁进程崩溃时自动释放), 避免对残留
        锁文件的死等。超时则放弃等待, 走自身缓存目录编译 (目录独立, 无并发写风险)。
        """
        if self._cache_dir() == self.pkg_dir:
            # 产物目录即 pkg_dir (当前用户可写): 后续的阻塞 flock 就是等待本身,
            # 轮询只制造"超时后另走独立目录"的假象, 直接跳过
            return False
        lock_path = self.pkg_dir / self._lock_name
        if not lock_path.exists():
            return False
        try:
            probe_fd = open(lock_path, "rb")
        except OSError:
            return False
        try:
            if not self._try_acquire_lock(probe_fd):
                # 锁被持有: 存活编译进行中, 等待其结束 (首轮即获锁 = 本就空闲)
                _log.info(
                    "Detected compilation in progress at %s, waiting...", lock_path
                )
            self._poll_until(lambda: self._try_acquire_lock(probe_fd))
        finally:
            probe_fd.close()
        # 带陈旧性校验的探测判定, 避免残留锁文件复活陈旧产物
        return self._probe_pkg_dir_binding() is not None

    @staticmethod
    def _try_acquire_lock(lock_fd) -> bool:
        try:
            fcntl.flock(lock_fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            return True
        except BlockingIOError:
            return False

    def _poll_until(self, predicate: Callable[[], bool]) -> bool:
        deadline = time.monotonic() + _FLOCK_TIMEOUT
        while True:
            if predicate():
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(1.0)

    def _iter_source_hashes(self) -> Iterator[tuple]:
        """遍历 _src/ 全部源文件 (MANIFEST 除外), 产出 (相对路径, sha256)."""
        if not self.src_dir.is_dir():
            return
        for file in sorted(self.src_dir.rglob("*")):
            if file.is_file() and file.name != _MANIFEST_NAME:
                yield file.relative_to(self.src_dir).as_posix(), _sha256_file(file)

    def _verify_manifest(self):
        """校验 _src/ 源文件与随包 MANIFEST.sha256 一致, 保证随包源码完整性."""
        manifest = self.src_dir / _MANIFEST_NAME
        if not manifest.is_file():
            raise BuildOnlineError(
                f"Source manifest not found: {manifest}.\nHint: reinstall the hixl package."
            )
        recorded: Dict[str, str] = {}
        try:
            manifest_text = manifest.read_text(encoding="utf-8")
        except (OSError, ValueError) as e:
            # 含非法编码等读取异常 (ValueError 子类不被 OSError 捕获), 按校验失败处理
            raise BuildOnlineError(
                f"Source manifest unreadable: {manifest} ({e!r}).\n"
                "Hint: reinstall the hixl package."
            ) from e
        for line in manifest_text.splitlines():
            line = line.strip()
            if not line:
                continue
            digest, _, name = line.partition("  ")
            recorded[name] = digest
        actual = dict(self._iter_source_hashes())
        if recorded != actual:
            raise BuildOnlineError(
                "Source integrity check failed (MANIFEST.sha256 mismatch)."
                f" Hint: reinstall the hixl package. Source dir: {self.src_dir}"
            )

    def _meta_content(self, cache_dir: Path) -> Dict[str, str]:
        """期望的 meta 内容 = 源码清单摘要 + Python 版本 + 构建时间戳 + 产物哈希."""
        lines = [f"{digest}  {name}" for name, digest in self._iter_source_hashes()]
        meta = {
            "manifest_hash": hashlib.sha256(
                "\n".join(lines).encode("utf-8")
            ).hexdigest(),
            "python_tag": self.python_tag,
            # 构建时间戳: 升级换包后 (_src 未变也) 令旧产物按 meta 判陈旧
            "build_timestamp": self.build_timestamp,
        }
        so_path = self._search_dir(cache_dir)
        if so_path is not None:
            meta["so_sha256"] = _sha256_file(so_path)
        return meta

    def _write_build_meta(self, cache_dir: Path):
        meta = self._meta_content(cache_dir)
        # 临时文件 + os.replace 原子落盘: 并发读者不会读到半截 json
        tmp_path = cache_dir / f"{self._meta_name}.tmp.{os.getpid()}"
        tmp_path.write_text(json.dumps(meta), encoding="utf-8")
        os.replace(tmp_path, cache_dir / self._meta_name)

    def _verify_build_meta(self, cache_dir: Path, *, full: bool = True) -> bool:
        meta_path = cache_dir / self._meta_name
        try:
            # UnicodeDecodeError 是 ValueError 子类; 内容异常一律按校验失败处理,
            # 不让 import 快路径抛出契约外异常
            meta = json.loads(meta_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return False
        if not isinstance(meta, dict):
            return False
        if not full:
            # pkg_dir 属 site-packages 信任边界: 只校验陈旧性 (构建时间戳/
            # Python 版本), 不做产物内容哈希, 避免 import 快路径的额外 I/O
            return (
                meta.get("build_timestamp") == self.build_timestamp
                and meta.get("python_tag") == self.python_tag
            )
        try:
            # 全量校验含 .so 哈希与 _src 遍历, 锁外执行期间产物可能被持锁
            # 进程清理 → 读不到按校验失败处理, 走等待与锁内复核, 不使 import 失败
            return meta == self._meta_content(cache_dir)
        except OSError:
            return False

    def _discard_cache(self, cache_dir: Path):
        # 通用名产物 (hixl.so/.abi3.so) 是旧包升级后的孤儿文件 (新 wheel 只发
        # cpython 命名产物), 与陈旧的 cpython 产物一并清理
        for suffix in importlib.machinery.EXTENSION_SUFFIXES:
            try:
                (cache_dir / f"{_MODULE_STEM}{suffix}").unlink(missing_ok=True)
            except OSError:
                # 只读目录等场景删除失败不致命: 校验已判陈旧, 该产物不会被本进程加载
                _log.warning(
                    "Failed to remove stale binding: %s",
                    cache_dir / f"{_MODULE_STEM}{suffix}",
                )
        # 仅清理当前 Python 版本自己的 meta, 其他版本的产物与元数据不受影响
        try:
            (cache_dir / self._meta_name).unlink(missing_ok=True)
        except OSError:
            pass

    def _load_from_cache(self, so_path: Path):
        """通过 importlib 动态加载 .so 并注册为 hixl.hixl.

        .so 位于 pkg_dir 时可被 Python 原生 import 发现, 无需手动注册.
        """
        if so_path.parent == self.pkg_dir:
            return
        if _DOTTED_NAME in sys.modules:
            return
        spec = importlib.util.spec_from_file_location(_DOTTED_NAME, so_path)
        if spec is None or spec.loader is None:
            raise BuildOnlineError(f"Failed to create import spec for {so_path}")
        module = importlib.util.module_from_spec(spec)
        # 注册必须在 exec_module 之前, 防止 pybind11 模块初始化时递归 import
        sys.modules[_DOTTED_NAME] = module
        parent = sys.modules.get("hixl")
        if parent is not None:
            setattr(parent, _MODULE_STEM, module)
        try:
            spec.loader.exec_module(module)
        except BaseException:
            # 初始化失败回滚注册, 避免残留半初始化模块使重试报错变形
            sys.modules.pop(_DOTTED_NAME, None)
            if parent is not None and getattr(parent, _MODULE_STEM, None) is module:
                delattr(parent, _MODULE_STEM)
            raise
        _log.info("Loaded hixl binding from cache: %s", so_path)


def ensure_hixl_binding():
    """确保 hixl.hixl 绑定模块可用 (预编译命中直接返回, 否则在线编译)."""
    try:
        BuildOnlineHixlManager().ensure_hixl_binding()
    except OSError as e:
        # 只读 HOME / 磁盘满等环境异常统一转入 BuildOnlineError,
        # 维持 ImportError 契约, 下游 except ImportError 降级保护可接住
        raise BuildOnlineError(
            f"Online compilation aborted by environment error: {e!r}.\n"
            "Hint: check $HOME writability and disk space."
        ) from e
