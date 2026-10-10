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

"""Parsing and normalization of ``hixl_engine`` extra-config options
(backend selection, LocalCommRes / GlobalResourceConfig handling)."""

import json
from typing import Any
from vllm.logger import logger
from .constants import (
    DEFAULT_LINK_TIMEOUT_MS,
    DEFAULT_TRANSFER_TIMEOUT_MS,
    HIXL_CS_LOCAL_COMM_RES,
    HIXL_ENGINE_BACKENDS,
    HIXL_ENGINE_BACKEND_COMM,
    HIXL_ENGINE_BACKEND_CS,
    HIXL_OPTION_GLOBAL_RESOURCE_CONFIG,
    HIXL_OPTION_LOCAL_COMM_RES,
    HIXL_PROTOCOL_DESC_FLAT,
    LISTEN_PORT_OFFSET,
)


def _loads_json_object(raw: str) -> dict[str, Any] | None:
    try:
        obj = json.loads(raw)
    except (TypeError, json.JSONDecodeError, ValueError):
        return None
    return obj if isinstance(obj, dict) else None


def _protocol_desc_from_grc(obj: dict[str, Any]) -> Any:
    if HIXL_PROTOCOL_DESC_FLAT in obj:
        return obj[HIXL_PROTOCOL_DESC_FLAT]
    crc = obj.get("comm_resource_config")
    if isinstance(crc, dict):
        return crc.get("protocol_desc")
    return None


def _protocol_desc_nonempty(desc: Any) -> bool:
    if desc is None:
        return False
    if isinstance(desc, str):
        return bool(desc)
    if isinstance(desc, list):
        return any(bool(item) for item in desc)
    return True


def _local_comm_res_is_cs(raw: str) -> bool:
    obj = _loads_json_object(raw)
    return obj is not None and obj.get("version") == "1.3"


def _has_protocol_desc(options: dict[str, str]) -> bool:
    raw = options.get(HIXL_OPTION_GLOBAL_RESOURCE_CONFIG)
    if not raw:
        return False
    obj = _loads_json_object(raw)
    if obj is None:
        return False
    return _protocol_desc_nonempty(_protocol_desc_from_grc(obj))


def _flatten_grc_protocol_desc(options: dict[str, str]) -> bool:
    raw = options.get(HIXL_OPTION_GLOBAL_RESOURCE_CONFIG)
    if not raw:
        return False
    obj = _loads_json_object(raw)
    if obj is None:
        return False
    if HIXL_PROTOCOL_DESC_FLAT in obj:
        return False
    crc = obj.get("comm_resource_config")
    if not isinstance(crc, dict) or "protocol_desc" not in crc:
        return False
    desc = crc.pop("protocol_desc")
    obj[HIXL_PROTOCOL_DESC_FLAT] = desc
    if not crc:
        obj.pop("comm_resource_config", None)
    options[HIXL_OPTION_GLOBAL_RESOURCE_CONFIG] = json.dumps(obj, separators=(",", ":"))
    return True


def _strip_protocol_desc(grc_raw: str) -> tuple[str | None, bool]:
    obj = _loads_json_object(grc_raw)
    if obj is None:
        return grc_raw, False
    stripped = False
    if HIXL_PROTOCOL_DESC_FLAT in obj:
        desc = obj.pop(HIXL_PROTOCOL_DESC_FLAT)
        stripped = stripped or _protocol_desc_nonempty(desc)
    crc = obj.get("comm_resource_config")
    if isinstance(crc, dict) and "protocol_desc" in crc:
        desc = crc.pop("protocol_desc")
        stripped = stripped or _protocol_desc_nonempty(desc)
        if not crc:
            obj.pop("comm_resource_config", None)
    if not obj:
        return None, stripped
    return json.dumps(obj, separators=(",", ":")), stripped


def _normalize_hixl_engine_options(raw: Any) -> dict[str, str]:
    if raw is None:
        return {}
    if not isinstance(raw, dict):
        raise ValueError(
            f"hixl_engine.options must be a dict, got {type(raw).__name__}."
        )
    out: dict[str, str] = {}
    for key, value in raw.items():
        if value is None:
            continue
        name = str(key)
        if isinstance(value, (dict, list)):
            out[name] = json.dumps(value, separators=(",", ":"))
        elif isinstance(value, str):
            out[name] = value
        else:
            out[name] = str(value)
    return out


def _apply_hixl_engine_backend_options(
    backend: str, options: dict[str, str]
) -> tuple[dict[str, str], str]:
    out = dict(options)
    if backend == HIXL_ENGINE_BACKEND_CS:
        if _flatten_grc_protocol_desc(out):
            logger.warning(
                "hixl_engine.options GlobalResourceConfig used nested "
                "comm_resource_config.protocol_desc; flattened to the key "
                "HixlOptions parses."
            )
        lcr = out.get(HIXL_OPTION_LOCAL_COMM_RES, "")
        if lcr and _local_comm_res_is_cs(lcr):
            return out, "none"
        if lcr:
            logger.warning(
                "hixl_engine.backend=hixl_cs but LocalCommRes is not version 1.3; replacing it. original=%s",
                lcr,
            )
            out[HIXL_OPTION_LOCAL_COMM_RES] = HIXL_CS_LOCAL_COMM_RES
            return out, "LocalCommRes:1.3"
        if _has_protocol_desc(out):
            return out, "none"
        out[HIXL_OPTION_LOCAL_COMM_RES] = HIXL_CS_LOCAL_COMM_RES
        return out, "LocalCommRes:1.3"
    if backend == HIXL_ENGINE_BACKEND_COMM:
        stripped: list[str] = []
        lcr = out.get(HIXL_OPTION_LOCAL_COMM_RES, "")
        if lcr and _local_comm_res_is_cs(lcr):
            out.pop(HIXL_OPTION_LOCAL_COMM_RES)
            stripped.append("LocalCommRes:1.3")
        grc = out.get(HIXL_OPTION_GLOBAL_RESOURCE_CONFIG)
        if grc:
            new_grc, did_strip = _strip_protocol_desc(grc)
            if did_strip:
                stripped.append("protocol_desc")
                if new_grc is None:
                    out.pop(HIXL_OPTION_GLOBAL_RESOURCE_CONFIG)
                else:
                    out[HIXL_OPTION_GLOBAL_RESOURCE_CONFIG] = new_grc
        if stripped:
            logger.warning(
                "hixl_engine.backend=comm stripped CS selector(s) %s.",
                ",".join(stripped),
            )
        return out, ("none" if not stripped else "stripped:" + ",".join(stripped))
    raise ValueError(
        f"hixl_engine.backend must be 'hixl_cs' or 'comm', got {backend!r}."
    )


def _parse_hixl_engine_extra(
    extra: dict[str, Any] | None,
    *,
    host: str,
    kv_port: int,
    dp_offset: int,
    device_index: int,
) -> tuple[str, int, str, dict[str, str], str, str, int, int]:
    cfg = extra or {}
    raw_backend = cfg.get("backend")
    if raw_backend is None or (
        isinstance(raw_backend, str) and not raw_backend.strip()
    ):
        backend_requested = "default"
        backend = HIXL_ENGINE_BACKEND_CS
    else:
        backend = str(raw_backend).strip()
        backend_requested = backend
    if backend not in HIXL_ENGINE_BACKENDS:
        raise ValueError(
            f"hixl_engine.backend must be 'hixl_cs' or 'comm', got {backend!r}."
        )
    options, injected = _apply_hixl_engine_backend_options(
        backend, _normalize_hixl_engine_options(cfg.get("options", {}))
    )
    listen_port_base = cfg.get("listen_port_base")
    if listen_port_base is None:
        listen_port_base = int(kv_port) + LISTEN_PORT_OFFSET
    # Same striding as the handshake port. Co-located DP ranks share pp/pcp/tp
    # ranks, so device_index alone repeats across engine processes on a host.
    listen_port = int(listen_port_base) + int(dp_offset) + int(device_index)
    return (
        host,
        listen_port,
        backend,
        options,
        backend_requested,
        injected,
        int(cfg.get("link_timeout_ms", DEFAULT_LINK_TIMEOUT_MS)),
        int(cfg.get("transfer_timeout_ms", DEFAULT_TRANSFER_TIMEOUT_MS)),
    )
