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

"""Local glue around the native ``hixl`` binding: deferred import and the
``_Hixl`` engine wrapper (initialize / register / connect / transfer)."""

import threading
import time
from typing import Any
from vllm.config import VllmConfig
from vllm.logger import logger
from .constants import ORPHAN_DRAIN_MS, TERMINAL_FAIL
from .engine_options import _parse_hixl_engine_extra


# ``import hixl`` is the address-level pybind (hixl_py.cc). Deferred so
# registering the connector name does not load the native library.
_HIXL_MOD = None


def _load_hixl():
    global _HIXL_MOD
    if _HIXL_MOD is None:
        import hixl  # type: ignore[import-not-found]

        _HIXL_MOD = hixl
    return _HIXL_MOD


def _unique_physical_regions(regions: list[tuple[int, int]]) -> list[tuple[int, int]]:
    by_base: dict[int, int] = {}
    for base, length in regions:
        prev = by_base.get(base)
        if prev is None or length > prev:
            by_base[base] = length
    return list(by_base.items())


def _transfer_status_name(status: Any) -> str:
    name = getattr(status, "name", None)
    if name is not None:
        return name
    return str(status).split(".")[-1]


class _Hixl:
    """Local glue around ``hixl.Hixl``: connect-once + TransferAsync polled to a blocking read."""

    def __init__(
        self,
        host: str,
        listen_port: int,
        backend: str,
        options: dict[str, str],
        backend_requested: str,
        backend_injected: str,
        link_timeout_ms: int,
        transfer_timeout_ms: int,
        hixl_mod: Any | None = None,
    ):
        self.host = host
        self.listen_port = listen_port
        self.endpoint = f"{host}:{listen_port}"
        self.backend = backend
        self.options = options
        self.backend_requested = backend_requested
        self.backend_injected = backend_injected
        self.link_timeout_ms = link_timeout_ms
        self.transfer_timeout_ms = transfer_timeout_ms
        self._hixl_mod = hixl_mod
        self._hixl: Any | None = None
        self._initialized = False
        self._lock = threading.Lock()
        self._connected: set[str] = set()
        # Handles whose transfer outlived its deadline. Kept so hixl can release
        # the request record, which it only does once a terminal status is read.
        self._orphan_handles: dict[int, str] = {}
        # base address -> (mem handle, registered span). Keyed by base alone:
        # TransferAsync addresses by (addr, len) and never dereferences a
        # handle, so one registration covers every logical view of a segment.
        self._registered_bases: dict[int, tuple[int, int]] = {}

    @classmethod
    def from_vllm_config(
        cls, vllm_config: VllmConfig, *, host: str, dp_offset: int, device_index: int
    ) -> "_Hixl":
        kvtc = vllm_config.kv_transfer_config
        extra = kvtc.get_from_extra_config("hixl_engine", {}) or {}
        parsed = _parse_hixl_engine_extra(
            extra,
            host=host,
            kv_port=int(kvtc.kv_port),
            dp_offset=dp_offset,
            device_index=device_index,
        )
        return cls(*parsed)

    def initialize(self) -> int:
        if self._initialized:
            return self.listen_port
        if self._hixl_mod is None:
            self._hixl_mod = _load_hixl()
        self._hixl = self._hixl_mod.Hixl()
        logger.info(
            "HIXLConnector Initialize backend=%s requested=%s injected=%s endpoint=%s options=%s",
            self.backend,
            self.backend_requested,
            self.backend_injected,
            self.endpoint,
            self.options,
        )
        status = self._hixl.initialize(self.endpoint, self.options)
        self._check(status, "Initialize")
        self._initialized = True
        return self.listen_port

    def register_physical_regions(self, regions: list[tuple[int, int]]) -> int:
        added = 0
        with self._lock:
            if not self._initialized or self._hixl is None:
                raise RuntimeError("hixl.Hixl is not initialized")
            # Start from a clean slate so a second call is idempotent: the KV
            # cache may have been rebuilt at other addresses, or the same base
            # may now need a wider span.
            self._deregister_all_locked()
            mt = self._hixl_mod.MemType.MEM_DEVICE
            try:
                for addr, length in _unique_physical_regions(regions):
                    status, handle = self._hixl.register_mem(
                        self._hixl_mod.MemDesc(addr, length), mt
                    )
                    self._check(status, "RegisterMem")
                    self._registered_bases[addr] = (handle, length)
                    added += 1
            except Exception:
                # A mid-loop failure must not leave this round's already
                # registered segments behind in the engine and the ledger: the
                # ledger was empty when the loop started, so deregistering
                # everything rolls back exactly this round, then re-raise.
                self._deregister_all_locked()
                raise
        return added

    def disconnect_all(self) -> None:
        """Disconnect every connected peer. Best effort; called during
        shutdown before ``finalize`` so the engine's links are closed
        cleanly instead of dying with the process.

        The whole loop runs under the lock so it is serialized against
        ``finalize``: a disconnect issued outside the lock could race the
        native engine teardown, and re-checking liveness here (instead of
        trusting a snapshot) keeps the loop a no-op once finalized.
        """
        with self._lock:
            if not self._initialized or self._hixl is None:
                return
            for peer in list(self._connected):
                try:
                    self._hixl.disconnect(peer, self.link_timeout_ms)
                except Exception:  # noqa: BLE001
                    logger.warning(
                        "HIXLConnector Disconnect(%s) during shutdown raised; continuing.",
                        peer,
                    )
                self._connected.discard(peer)

    def finalize(self) -> None:
        """Deregister every KV segment and shut the engine down. Idempotent.

        Holding the lock keeps an in-flight connect or transfer from touching
        the engine while it is being torn down. Clearing the connection set
        makes any later pull fail loudly instead of using a freed engine.
        """
        with self._lock:
            if not self._initialized or self._hixl is None:
                return
            self._deregister_all_locked()
            self._hixl.finalize()
            self._hixl = None
            self._initialized = False
            self._connected.clear()
            self._orphan_handles.clear()
        logger.info("HIXLConnector finalized endpoint=%s", self.endpoint)

    def _deregister_all_locked(self) -> None:
        for addr, (handle, _length) in self._registered_bases.items():
            try:
                status = self._hixl.deregister_mem(handle)
                self._check(status, "DeregisterMem")
            except Exception:  # noqa: BLE001
                # A stale handle must not block re-registration or shutdown.
                logger.warning(
                    "HIXLConnector DeregisterMem failed for base=%#x handle=%s; continuing.",
                    addr,
                    handle,
                )
        self._registered_bases.clear()

    def _ensure_connected(self, remote_engine: str) -> None:
        if not remote_engine:
            raise ValueError("remote_engine endpoint is empty")
        if remote_engine in self._connected:
            return
        with self._lock:
            if remote_engine in self._connected:
                return
            if not self._initialized or self._hixl is None:
                raise RuntimeError("hixl.Hixl is not initialized")
            status = self._hixl.connect(remote_engine, self.link_timeout_ms)
            # A peer dropped by _invalidate_peer may still be live on the
            # engine's side; that is a usable link, not a failure.
            if status != self._hixl_mod.ALREADY_CONNECTED:
                self._check(status, "Connect")
            self._connected.add(remote_engine)

    def _invalidate_peer(self, remote_engine: str) -> None:
        """Drop a peer so the next pull re-runs Connect.

        ``_connected`` is a positive-only cache. Without this, a peer that
        restarted on the same endpoint is never reconnected and every later
        pull to it fails until this process restarts.
        """
        with self._lock:
            if remote_engine not in self._connected:
                return
            self._connected.discard(remote_engine)
            if self._initialized and self._hixl is not None:
                try:
                    self._hixl.disconnect(remote_engine, self.link_timeout_ms)
                except Exception:  # noqa: BLE001
                    logger.warning(
                        "HIXLConnector Disconnect(%s) raised; reconnecting anyway.",
                        remote_engine,
                    )
        logger.warning(
            "HIXLConnector dropped the link to %s; the next pull reconnects.",
            remote_engine,
        )

    def sync_read(
        self,
        remote_engine: str,
        local_addrs: list[int],
        remote_addrs: list[int],
        length_list: list[int],
    ) -> int:
        if not (len(local_addrs) == len(remote_addrs) == len(length_list)):
            raise ValueError(
                f"local/remote/length lists must have the same length, got "
                f"{len(local_addrs)}, {len(remote_addrs)}, {len(length_list)}"
            )
        if not local_addrs:
            return 0
        self._reap_orphans()
        self._ensure_connected(remote_engine)
        descs = [
            self._hixl_mod.TransferOpDesc(
                local_addr=int(local), remote_addr=int(remote), len=int(nbytes)
            )
            for local, remote, nbytes in zip(
                local_addrs, remote_addrs, length_list, strict=True
            )
        ]
        with self._lock:
            self._check_live()
            status, handle = self._hixl.transfer_async(
                remote_engine, self._hixl_mod.TransferOp.READ, descs
            )
        if status != self._hixl_mod.SUCCESS:
            self._invalidate_peer(remote_engine)
            raise RuntimeError(f"hixl TransferAsync failed, code={status}")
        try:
            self._await_transfer(handle, remote_engine)
        except Exception:
            self._invalidate_peer(remote_engine)
            raise
        return 0

    def _await_transfer(self, handle: int, remote_engine: str) -> None:
        """Poll one handle to a terminal status.

        ``transfer_timeout_ms`` is deliberately a soft deadline. A WAITING READ
        is still writing into local KV blocks, and the caller reports those
        blocks as reusable the moment this raises, so returning at the soft
        deadline would let a late DMA land in another request's cache. Past it
        we keep draining for ORPHAN_DRAIN_MS and only then abandon the handle,
        which is logged as a correctness hazard rather than a plain timeout.
        """
        soft_deadline = time.monotonic() + self.transfer_timeout_ms / 1000.0
        hard_deadline = soft_deadline + ORPHAN_DRAIN_MS / 1000.0
        overdue = False
        while True:
            with self._lock:
                self._check_live()
                st = self._get_transfer_status(handle)
            if st is None:
                self._forget_orphan(handle)
                raise RuntimeError(
                    "GetTransferStatus record gone before COMPLETED was observed (consumed failure or error)"
                )
            name = _transfer_status_name(st)
            if name == "COMPLETED":
                if overdue:
                    # The data did land, so failing the request here would only
                    # force a needless recompute.
                    self._forget_orphan(handle)
                    logger.warning(
                        "HIXLConnector transfer to %s completed late, past its %d ms deadline.",
                        remote_engine,
                        self.transfer_timeout_ms,
                    )
                return
            if name in TERMINAL_FAIL:
                self._forget_orphan(handle)
                raise RuntimeError(f"hixl transfer failed, status={name}")
            if name != "WAITING":
                self._forget_orphan(handle)
                raise RuntimeError(f"hixl unknown transfer status={name!r}")
            now = time.monotonic()
            if not overdue and now >= soft_deadline:
                overdue = True
                self._remember_orphan(handle, remote_engine)
                logger.warning(
                    "HIXLConnector transfer to %s still WAITING after %d ms; draining up to %d ms more "
                    "before abandoning it.",
                    remote_engine,
                    self.transfer_timeout_ms,
                    ORPHAN_DRAIN_MS,
                )
            if overdue and now >= hard_deadline:
                logger.error(
                    "HIXLConnector abandoning an in-flight READ from %s after %d ms. The engine may still "
                    "write into this request's KV blocks after they are recycled.",
                    remote_engine,
                    self.transfer_timeout_ms + ORPHAN_DRAIN_MS,
                )
                raise RuntimeError(
                    f"hixl transfer abandoned while still WAITING after "
                    f"{self.transfer_timeout_ms + ORPHAN_DRAIN_MS} ms"
                )
            time.sleep(0.001)

    def _remember_orphan(self, handle: int, remote_engine: str) -> None:
        with self._lock:
            self._orphan_handles[handle] = remote_engine

    def _forget_orphan(self, handle: int) -> None:
        with self._lock:
            self._orphan_handles.pop(handle, None)

    def _reap_orphans(self) -> None:
        """Re-query abandoned handles so hixl can release their request records.

        hixl drops a record only when a terminal status is observed, so a handle
        nobody queries again leaks for the life of the process.
        """
        with self._lock:
            if not self._orphan_handles or not self._initialized or self._hixl is None:
                return
            for handle in list(self._orphan_handles):
                try:
                    st = self._get_transfer_status(handle)
                except Exception:  # noqa: BLE001
                    self._orphan_handles.pop(handle, None)
                    continue
                if st is None or _transfer_status_name(st) != "WAITING":
                    self._orphan_handles.pop(handle, None)

    def _get_transfer_status(self, req: int) -> Any | None:
        status, st = self._hixl.get_transfer_status(req)
        if status == self._hixl_mod.PARAM_INVALID:
            return None
        self._check(status, "GetTransferStatus")
        return st

    def _check_live(self) -> None:
        if not self._initialized or self._hixl is None:
            raise RuntimeError("hixl.Hixl was finalized while a transfer was in flight")

    def _check(self, status: int, ctx: str) -> None:
        if status != self._hixl_mod.SUCCESS:
            raise RuntimeError(f"hixl {ctx} failed, code={status}")
