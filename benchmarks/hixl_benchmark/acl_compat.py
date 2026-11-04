#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""Unified ACL access layer for the HIXL benchmark tool.

Callers see a single ``acl`` namespace shaped exactly like pyACL (``acl.init()``,
``acl.rt.malloc(size, policy) -> (ptr, ret)``). Two interchangeable backends
provide it:

- ``import acl`` succeeds: the real pyACL module, its ``rt`` wrapped so the
  ctypes-backed extension methods pyACL never exposes are added.
- ``import acl`` fails: a pure-ctypes backend over ``libascendcl.so`` that
  fabricates the same module shape.

Extension methods (always ctypes, identical on both backends):

- ``rt.mem_set_access(va, size, location_type, location_id) -> int``
- ``rt.reserve_mem_address_no_uc(size) -> (va, int)``
- ``rt.has_reserve_no_uc_memory() -> bool``
- ``rt.runtime_skips_user_to_logic_convert() -> bool``
"""

import ctypes
import logging
import os
import platform
import re

LOG = logging.getLogger("hixl_benchmark")

ACL_SUCCESS = 0
ACL_ERROR_REPEAT_INITIALIZE = 100002
ACL_HBM_MEM_HUGE = 4
ACL_MEM_ALLOCATION_TYPE_PINNED = 0
ACL_MEMCPY_DEVICE_TO_HOST = 2
ACL_MEM_HANDLE_TYPE_NONE = 0
ACL_MEM_LOCATION_TYPE_DEVICE = 1
ACL_MEM_LOCATION_TYPE_HOST = 0
ACL_MEM_LOCATION_TYPE_HOST_NUMA = 4
ACL_MEM_MALLOC_HUGE_ONLY = 1
ACL_MEM_P2P_HUGE = 16
ACL_MEM_P2P_HUGE1G = 17
ACL_RESERVE_FLAG_HUGE_PAGE = 1
ACL_RT_MEM_ACCESS_FLAGS_READWRITE = 3
# aclsysGetVersionNum encodes runtime 9.2.0 as 90200000. From that version the caller
# device id is already the id aclrtMemSetAccess expects (engine: ubmem_allocator.cc).
ACL_RUNTIME_VERSION_SKIP_LOGIC_ID_CONVERT = 90200000


class AclUnavailableError(RuntimeError):
    """Neither pyACL nor libascendcl.so could be loaded."""


def _fail(message):
    LOG.error("%s", message)
    raise SystemExit(1)


class AclrtMemLocation(ctypes.Structure):
    _fields_ = [("id", ctypes.c_uint32), ("type", ctypes.c_int32)]


class AclrtPhysicalMemProp(ctypes.Structure):
    _fields_ = [
        ("handle_type", ctypes.c_int32),
        ("allocation_type", ctypes.c_int32),
        ("mem_attr", ctypes.c_int32),
        ("location", AclrtMemLocation),
        ("reserve", ctypes.c_uint64),
    ]


class AclrtMemAccessDesc(ctypes.Structure):
    _fields_ = [
        ("flags", ctypes.c_int32),
        ("location", AclrtMemLocation),
        ("rsv", ctypes.c_uint8 * 12),
    ]


def _build_physical_mem_prop(prop):
    """Translate a pyACL-style prop dict into the aclrtPhysicalMemProp struct."""
    struct = AclrtPhysicalMemProp()
    struct.handle_type = prop.get("handleType", ACL_MEM_HANDLE_TYPE_NONE)
    struct.allocation_type = prop.get("allocationType", ACL_MEM_ALLOCATION_TYPE_PINNED)
    struct.mem_attr = prop.get("memAttr", 0)
    location = prop.get("location", {})
    struct.location.id = location.get("id", 0)
    struct.location.type = location.get("type", 0)
    struct.reserve = prop.get("reserve", 0)
    return struct


def _build_mem_access_desc(location_type, location_id):
    desc = AclrtMemAccessDesc()
    desc.flags = ACL_RT_MEM_ACCESS_FLAGS_READWRITE
    desc.location.id = location_id
    desc.location.type = location_type
    return desc


def missing_library_name(error_text):
    match = re.search(r"([A-Za-z0-9_+.-]+\.so(?:\.[0-9]+)*)", error_text)
    return match.group(1) if match else None


def find_cann_library(library_name):
    """Locate a CANN library in the configured toolkit or runtime directories."""
    machine = platform.machine()
    triplet = {"x86_64": "x86_64-linux", "aarch64": "aarch64-linux"}.get(
        machine, machine + "-linux"
    )
    roots = []
    home = os.environ.get("ASCEND_HOME_PATH")
    if home:
        roots.append(home)
    roots.append("/usr/local/Ascend/ascend-toolkit/latest")
    roots.append("/usr/local/Ascend/nnrt/latest")
    candidates = []
    for root in roots:
        candidates.append(os.path.join(root, "lib64"))
        candidates.append(os.path.join(root, triplet, "lib64"))
    for path in os.environ.get("LD_LIBRARY_PATH", "").split(":"):
        if path:
            candidates.append(path)
    for directory in candidates:
        path = os.path.join(directory, library_name)
        if os.path.isfile(path):
            return path
    return None


def find_libascendcl():
    """Locate libascendcl.so: ASCEND_HOME_PATH, ascend-toolkit, nnrt, then LD_LIBRARY_PATH."""
    return find_cann_library("libascendcl.so")


def load_library_with_deps(lib_path, seen=None, use_errno=False):
    """dlopen a library, preloading sibling dependencies the loader cannot find.

    ``seen`` threads the guard across recursive preloads so mutually missing
    libraries raise instead of recursing until RecursionError.
    """
    if seen is None:
        seen = set()
    lib_dir = os.path.dirname(lib_path)
    dep_dirs = [lib_dir, os.path.join(os.path.dirname(lib_dir), "devlib")]
    while True:
        try:
            return ctypes.CDLL(lib_path, use_errno=use_errno)
        except OSError as exc:
            name = missing_library_name(str(exc))
            if name is None or name in seen:
                raise
            dep_path = None
            for directory in dep_dirs:
                candidate = os.path.join(directory, name)
                if os.path.isfile(candidate):
                    dep_path = candidate
                    break
            if dep_path is None:
                raise
            seen.add(name)
            load_library_with_deps(dep_path, seen)


def _load_lib(pyacl_present):
    """Load libascendcl.so via ctypes; None means the extras fail lazily when used."""
    lib_path = find_libascendcl()
    if lib_path is not None:
        try:
            return load_library_with_deps(lib_path)
        except OSError as exc:
            _fail(f"Load libascendcl.so failed, ret:{exc}, param:path:{lib_path}")
    if pyacl_present:
        try:
            return ctypes.CDLL("libascendcl.so")
        except OSError:
            return None
    return None


_RT_PHYSICAL = [
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.c_size_t,
    ctypes.POINTER(AclrtPhysicalMemProp),
    ctypes.c_uint64,
]
_RT_RESERVE = [
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.c_size_t,
    ctypes.c_size_t,
    ctypes.c_void_p,
    ctypes.c_uint64,
]
_SYMBOLS = {
    "aclInit": [ctypes.c_char_p],
    "aclFinalize": [],
    "aclrtSetDevice": [ctypes.c_int32],
    "aclrtResetDevice": [ctypes.c_int32],
    "aclrtGetDevice": [ctypes.POINTER(ctypes.c_int32)],
    "aclrtMalloc": [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t, ctypes.c_int32],
    "aclrtMallocHost": [ctypes.POINTER(ctypes.c_void_p), ctypes.c_size_t],
    "aclrtFree": [ctypes.c_void_p],
    "aclrtFreeHost": [ctypes.c_void_p],
    "aclrtMemset": [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int32, ctypes.c_size_t],
    "aclrtMemcpy": [
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_int32,
    ],
    "aclrtMallocPhysical": _RT_PHYSICAL,
    "aclrtFreePhysical": [ctypes.c_void_p],
    "aclrtReserveMemAddress": _RT_RESERVE,
    "aclrtReserveMemAddressNoUCMemory": _RT_RESERVE,
    "aclrtMapMem": [
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.c_size_t,
        ctypes.c_void_p,
        ctypes.c_uint64,
    ],
    "aclrtUnmapMem": [ctypes.c_void_p],
    "aclrtReleaseMemAddress": [ctypes.c_void_p],
    "aclrtGetLogicDevIdByUserDevId": [ctypes.c_int32, ctypes.POINTER(ctypes.c_int32)],
    "aclrtGetPhyDevIdByLogicDevId": [ctypes.c_int32, ctypes.POINTER(ctypes.c_int32)],
    "aclrtGetSocName": [],
    "aclrtMemSetAccess": [
        ctypes.c_void_p,
        ctypes.c_size_t,
        ctypes.POINTER(AclrtMemAccessDesc),
        ctypes.c_size_t,
    ],
    "aclsysGetVersionNum": [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int32)],
}


class _SymbolBinder:
    """Lazy libascendcl symbol binding driven by the _SYMBOLS argtype registry."""

    def __init__(self, lib):
        self._lib = lib
        self._cache = {}

    def require(self, name):
        fn = self._cache.get(name)
        if fn is not None:
            return fn
        if self._lib is None:
            _fail(
                "Call api:%s failed, libascendcl.so not found and pyACL does not expose it, "
                "set ASCEND_HOME_PATH or source the CANN set_env.sh first" % name
            )
        try:
            fn = getattr(self._lib, name)
        except AttributeError:
            _fail(f"Call api:{name} failed, symbol not found in libascendcl.so")
        fn.argtypes = _SYMBOLS[name]
        fn.restype = ctypes.c_int32
        self._cache[name] = fn
        return fn

    def has_symbol(self, name):
        return self._lib is not None and hasattr(self._lib, name)


def _out_ptr(binder, symbol, *args):
    """Call a libascendcl API whose first parameter is a void* out-param."""
    ptr = ctypes.c_void_p()
    ret = binder.require(symbol)(ctypes.byref(ptr), *args)
    return ptr.value or 0, ret


def _out_int(binder, symbol, *args):
    """Call a libascendcl API whose last parameter is an int32 out-param."""
    out = ctypes.c_int32()
    ret = binder.require(symbol)(*args, ctypes.byref(out))
    return out.value, ret


class _RtExtensions:
    """ctypes-backed rt methods absent from pyACL; shared by both rt backends."""

    _has_no_uc = None
    _skips_logic_id_convert = None

    def mem_set_access(self, va, size, location_type, location_id):
        desc = _build_mem_access_desc(location_type, location_id)
        return self._binder.require("aclrtMemSetAccess")(
            va, size, ctypes.byref(desc), 1
        )

    def reserve_mem_address_no_uc(self, size):
        return _out_ptr(
            self._binder,
            "aclrtReserveMemAddressNoUCMemory",
            size,
            0,
            None,
            ACL_RESERVE_FLAG_HUGE_PAGE,
        )

    def has_reserve_no_uc_memory(self):
        if self._has_no_uc is None:
            self._has_no_uc = self._binder.has_symbol(
                "aclrtReserveMemAddressNoUCMemory"
            )
        return self._has_no_uc

    def runtime_skips_user_to_logic_convert(self):
        """Whether aclrtMemSetAccess expects the caller device id directly.

        Mirrors the engine rule (src/hixl/cs/ubmem/ubmem_allocator.cc): runtime
        >= 9.2.0 (aclsysGetVersionNum code 90200000) uses the caller device id;
        older runtime, a failed or an absent version query still converts the
        user id to a driver logical id before SetAccess.
        """
        if self._skips_logic_id_convert is None:
            self._skips_logic_id_convert = self._query_runtime_skip()
        return self._skips_logic_id_convert

    def _query_runtime_skip(self):
        if not self._binder.has_symbol("aclsysGetVersionNum"):
            return False
        version, ret = _out_int(self._binder, "aclsysGetVersionNum", b"runtime")
        if ret != 0:
            LOG.warning(
                "Call api:aclsysGetVersionNum failed, ret:%d, package:runtime; "
                "convert user device id before SetAccess",
                ret,
            )
            return False
        return version >= ACL_RUNTIME_VERSION_SKIP_LOGIC_ID_CONVERT


class _CtypesRt(_RtExtensions):
    """Full ctypes rt implementation with pyACL return conventions."""

    def __init__(self, binder):
        self._binder = binder

    def set_device(self, device_id):
        return self._binder.require("aclrtSetDevice")(device_id)

    def reset_device(self, device_id):
        return self._binder.require("aclrtResetDevice")(device_id)

    def get_device(self):
        return _out_int(self._binder, "aclrtGetDevice")

    def malloc(self, size, policy):
        return _out_ptr(self._binder, "aclrtMalloc", size, policy)

    def malloc_host(self, size):
        return _out_ptr(self._binder, "aclrtMallocHost", size)

    def free(self, ptr):
        return self._binder.require("aclrtFree")(ptr)

    def free_host(self, ptr):
        return self._binder.require("aclrtFreeHost")(ptr)

    def memset(self, ptr, max_count, value, count):
        return self._binder.require("aclrtMemset")(ptr, max_count, value, count)

    def memcpy(self, dst, dst_max, src, count, kind):
        return self._binder.require("aclrtMemcpy")(dst, dst_max, src, count, kind)

    def malloc_physical(self, size, prop, flags):
        return _out_ptr(
            self._binder,
            "aclrtMallocPhysical",
            size,
            ctypes.byref(_build_physical_mem_prop(prop)),
            flags,
        )

    def free_physical(self, handle):
        return self._binder.require("aclrtFreePhysical")(handle)

    def reserve_mem_address(self, size, alignment, expect, flags):
        return _out_ptr(
            self._binder, "aclrtReserveMemAddress", size, alignment, expect, flags
        )

    def map_mem(self, va, size, offset, handle, flags):
        return self._binder.require("aclrtMapMem")(va, size, offset, handle, flags)

    def unmap_mem(self, va):
        return self._binder.require("aclrtUnmapMem")(va)

    def release_mem_address(self, va):
        return self._binder.require("aclrtReleaseMemAddress")(va)

    def get_logic_dev_id_by_user_dev_id(self, user_dev_id):
        return _out_int(self._binder, "aclrtGetLogicDevIdByUserDevId", user_dev_id)

    def get_phy_dev_id_by_logic_dev_id(self, logic_dev_id):
        return _out_int(self._binder, "aclrtGetPhyDevIdByLogicDevId", logic_dev_id)

    def get_soc_name(self):
        fn = self._binder.require("aclrtGetSocName")
        fn.restype = ctypes.c_char_p
        raw = fn()
        if raw is None:
            return None
        return raw.decode("utf-8", errors="replace")


class _PyaclRt(_RtExtensions):
    """Delegate unknown rt calls to real pyACL; extensions come from the mixin."""

    def __init__(self, real_rt, binder):
        self._real_rt = real_rt
        self._binder = binder

    def __getattr__(self, name):
        return getattr(self._real_rt, name)


class _CtypesBackend:
    """A pyACL-shaped module fabricated from libascendcl symbols."""

    def __init__(self, lib):
        self._binder = _SymbolBinder(lib)
        self.rt = _CtypesRt(self._binder)

    def init(self):
        return self._binder.require("aclInit")(None)

    def finalize(self):
        return self._binder.require("aclFinalize")()

    def get_soc_name(self):
        return self.rt.get_soc_name()


class _Facade:
    """The single ``acl`` namespace callers interact with."""

    def __init__(self, source, backend, rt):
        self._source = source
        self._backend = backend
        self._rt = rt

    @property
    def source(self):
        return self._source

    @property
    def rt(self):
        return self._rt

    def init(self):
        return self._backend.init()

    def finalize(self):
        return self._backend.finalize()

    def get_soc_name(self):
        return self._backend.get_soc_name()


def _build():
    try:
        import acl as backend
    except ImportError:
        lib = _load_lib(pyacl_present=False)
        if lib is None:
            raise AclUnavailableError(
                "ACL unavailable: import acl failed and libascendcl.so not found, "
                "set ASCEND_HOME_PATH or source the CANN set_env.sh first"
            )
        backend = _CtypesBackend(lib)
        return _Facade("ctypes", backend, backend.rt)
    return _Facade(
        "pyacl",
        backend,
        _PyaclRt(backend.rt, _SymbolBinder(_load_lib(pyacl_present=True))),
    )


_FACADE = None


def load():
    """Return the cached unified acl facade (lazy: never touches ACL before first call)."""
    global _FACADE
    if _FACADE is None:
        _FACADE = _build()
    return _FACADE
