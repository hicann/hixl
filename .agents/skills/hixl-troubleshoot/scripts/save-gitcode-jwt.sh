#!/usr/bin/env bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
#
# Cache GitCode web JWT (localStorage.access_token) for download-issue-logs.sh.
#
# An agent with browser automation reads the token from a logged-in gitcode.com
# session (exact tool varies: Playwright MCP, Puppeteer, Cursor browser, etc.):
#   navigate to any gitcode page → evaluate localStorage.getItem('access_token')
# Then pass the JWT here.

set -euo pipefail

cache_dir="${HOME}/.hixl-troubleshoot"
jwt_file="${cache_dir}/gitcode_access_token"

usage() {
  cat <<EOF
Usage: $0 [JWT]

Save GitCode web JWT to ${jwt_file} (mode 600).
JWT source priority: argument > stdin > GITCODE_ACCESS_TOKEN env.

Agent (browser automation unavailable → see download-issue-logs.sh manual fallback):
  Read localStorage.access_token from a logged-in gitcode.com session, then:
  bash $0 '<jwt>'
EOF
}

jwt="${1:-}"
if [ -z "${jwt}" ] && [ ! -t 0 ]; then
  jwt="$(cat)"
fi
if [ -z "${jwt}" ]; then
  jwt="${GITCODE_ACCESS_TOKEN:-}"
fi
jwt="$(printf '%s' "${jwt}" | tr -d '\n\r ')"

if [ -z "${jwt}" ]; then
  usage >&2
  exit 1
fi

if ! printf '%s' "${jwt}" | grep -qE '^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$'; then
  echo "Invalid JWT format (expected header.payload.signature)." >&2
  exit 1
fi

mkdir -p "${cache_dir}"
printf '%s' "${jwt}" > "${jwt_file}"
chmod 600 "${jwt_file}"

if command -v python3 >/dev/null 2>&1; then
  python3 - "${jwt}" <<'PY' || true
import base64, json, sys, time
jwt = sys.argv[1]
payload_b64 = jwt.split(".")[1]
payload_b64 += "=" * (-len(payload_b64) % 4)
payload = json.loads(base64.urlsafe_b64decode(payload_b64))
exp = payload.get("exp")
sub = payload.get("sub") or payload.get("jti", "?")
if exp:
    remaining = exp - int(time.time())
    if remaining <= 0:
        print(f"WARN: JWT for {sub} already expired; login again before download.", file=sys.stderr)
    else:
        hours = remaining // 3600
        print(f"JWT cached for {sub}; ~{hours}h until exp.", file=sys.stderr)
PY
fi

echo "Saved JWT to ${jwt_file}"
