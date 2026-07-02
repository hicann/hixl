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
# Download GitCode issue log attachments (zips) for troubleshooting.
#
# Flow:
#   1. v5 API (GITCODE_API_TOKEN) fetches issue body + comments, extracts attachment URLs.
#   2. Download zips with web JWT: Authorization: Bearer <localStorage.access_token>.
#   3. JWT is obtained by the agent via browser automation (see save-gitcode-jwt.sh).
#      Shell cannot drive a browser; if JWT missing, print intent + manual fallback.

set -euo pipefail

owner="${1:-cann}"
repo="${2:-hixl}"
issue_no="${3:-}"
out_root="${HOME}/hixl-troubleshooting/issues"
cache_dir="${HOME}/.hixl-troubleshoot"
jwt_file="${cache_dir}/gitcode_access_token"
api_base="${GITCODE_API_BASE:-https://api.gitcode.com}/api/v5"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat <<EOF
Usage: $0 [owner] [repo] <issue_number>

Download issue attachment zips (issue body + comments) to:
  ${out_root}/<owner>-<repo>-<issue_number>/extracted

Env:
  GITCODE_API_TOKEN    required; v5 API for issue + comments
  GITCODE_ACCESS_TOKEN optional; web JWT (localStorage.access_token). Cached in ${jwt_file}.

JWT (agent with browser automation):
  Navigate to a logged-in gitcode.com issue page, read localStorage.access_token, then:
  bash ${script_dir}/save-gitcode-jwt.sh '<jwt>'

Manual fallback (no browser automation):
  Notify user that the issue has attachments; ask them to download/unzip manually
  and provide the extracted log directory path.
EOF
}

print_need_jwt() {
  local web_url="$1" out_dir="$2" n="$3"
  cat >&2 <<EOF
No GitCode web JWT available (GITCODE_ACCESS_TOKEN unset, ${jwt_file} missing).

Option A — browser automation (preferred; tool-specific):
  Open ${web_url} in a logged-in gitcode.com browser session, run JS:
    localStorage.getItem('access_token')
  If null/empty, ask the user to log in to gitcode.com once, then retry.
  Then:
    bash ${script_dir}/save-gitcode-jwt.sh '<jwt>'
    bash ${script_dir}/download-issue-logs.sh ${owner} ${repo} ${issue_no}

Option B — manual (no browser automation):
  Tell the user this issue has ${n} log attachment(s) (${web_url}).
  Ask them to download and unzip manually, then provide the extracted log directory path.
  Attachment URLs also saved in: ${out_dir}/attachment_urls.txt

Issue/comment JSON already saved under ${out_dir}.
EOF
}

looks_like_auth_error() {
  local f="$1"
  head -c 400 "${f}" 2>/dev/null | grep -qiE '未登录|UNAUTHORIZED|error_code.?:?401|<html|login'
}

if [ -z "${issue_no}" ]; then
  usage >&2
  exit 1
fi

if [ -z "${GITCODE_API_TOKEN:-}" ]; then
  echo "GITCODE_API_TOKEN is not set" >&2
  exit 1
fi

web_url="https://gitcode.com/${owner}/${repo}/issues/${issue_no}"
out_dir="${out_root}/${owner}-${repo}-${issue_no}"
mkdir -p "${out_dir}"

api_token="${GITCODE_API_TOKEN}"
echo "Fetching ${owner}/${repo}#${issue_no} via v5 API (issue + comments)..."
if ! curl -fsSL -H "PRIVATE-TOKEN: ${api_token}" \
      "${api_base}/repos/${owner}/${repo}/issues/${issue_no}" \
      -o "${out_dir}/issue.json"; then
  echo "Failed to fetch issue ${owner}/${repo}#${issue_no} (check GITCODE_API_TOKEN)." >&2
  exit 1
fi
if ! curl -fsSL -H "PRIVATE-TOKEN: ${api_token}" \
      "${api_base}/repos/${owner}/${repo}/issues/${issue_no}/comments" \
      -o "${out_dir}/comments.json"; then
  echo "Failed to fetch comments for issue #${issue_no}." >&2
  exit 1
fi

: > "${out_dir}/bodies.txt"
{
  jq -r '.body // .description // empty' "${out_dir}/issue.json" 2>/dev/null || true
  jq -r '.[].body // empty' "${out_dir}/comments.json" 2>/dev/null || true
} >> "${out_dir}/bodies.txt"

urls_file="${out_dir}/attachment_urls.txt"
: > "${urls_file}"
grep -oE "https://gitcode\.com/user-attachments/files/[^\"'<> )]+" "${out_dir}/bodies.txt" 2>/dev/null \
  | sed -E 's/[\\)]+$//' \
  | awk '!seen[$0]++' >> "${urls_file}" || true

count=$(wc -l < "${urls_file}" | tr -d ' ')
if [ "${count}" -eq 0 ]; then
  echo "No attachment URLs found in issue #${issue_no} (body or comments)." >&2
  exit 3
fi
echo "Found ${count} attachment(s):"
sed 's/^/  /' "${urls_file}"

web_jwt="${GITCODE_ACCESS_TOKEN:-}"
if [ -z "${web_jwt}" ] && [ -f "${jwt_file}" ]; then
  web_jwt="$(cat "${jwt_file}")"
fi
web_jwt="$(printf '%s' "${web_jwt}" | tr -d '\n\r ')"

if [ -z "${web_jwt}" ]; then
  print_need_jwt "${web_url}" "${out_dir}" "${count}"
  exit 2
fi

mkdir -p "${cache_dir}"
printf '%s' "${web_jwt}" > "${jwt_file}"
chmod 600 "${jwt_file}"

extracted="${out_dir}/extracted"
mkdir -p "${extracted}"
rc=0
while IFS= read -r url; do
  [ -z "${url}" ] && continue
  fname="$(basename "${url%%\?*}")"
  zip_path="${out_dir}/${fname}"
  echo "Downloading ${url}"
  http_code="000"
  curl_err="${zip_path}.curlerr"
  if ! http_code=$(curl -sSL -w "%{http_code}" -o "${zip_path}" \
        -H "Authorization: Bearer ${web_jwt}" -H "Referer: ${web_url}" \
        "${url}" 2>"${curl_err}"); then
    echo "  download failed (network/curl error; JWT kept). $(tr '\n' ' ' < "${curl_err}" 2>/dev/null)" >&2
    rm -f "${zip_path}" "${curl_err}"
    rc=5
    continue
  fi
  rm -f "${curl_err}"
  if [ "${http_code}" = "401" ] || [ "${http_code}" = "403" ]; then
    echo "  download failed (HTTP ${http_code}; JWT expired/rejected)." >&2
    rm -f "${jwt_file}" "${zip_path}"
    rc=4
    break
  fi
  if [ "${http_code}" != "200" ]; then
    echo "  download failed (HTTP ${http_code}; server error, JWT kept)." >&2
    rm -f "${zip_path}"
    rc=5
    continue
  fi
  if ! file "${zip_path}" | grep -qiE 'zip archive|compressed data'; then
    echo "  not a zip. First bytes:" >&2
    head -c 200 "${zip_path}" >&2 || true
    echo >&2
    if looks_like_auth_error "${zip_path}"; then
      echo "  -> JWT expired/rejected; cache cleared. Re-fetch via browser automation." >&2
      rm -f "${jwt_file}" "${zip_path}"
      rc=4
      break
    fi
    echo "  -> not a zip and not an auth error; JWT kept." >&2
    rm -f "${zip_path}"
    rc=5
    continue
  fi
  if ! unzip -o -q "${zip_path}" -d "${extracted}" 2>/dev/null; then
    echo "  unzip failed (archive may be corrupt): ${zip_path}" >&2
    rc=5
    continue
  fi
  echo "  -> ${extracted}"
done < "${urls_file}"

if [ "${rc}" -ne 0 ]; then
  if [ "${rc}" -eq 4 ]; then
    print_need_jwt "${web_url}" "${out_dir}" "${count}"
  fi
  exit "${rc}"
fi

echo "Done. Logs under ${extracted}"
echo "Next: bash ${script_dir}/log-triage.sh ${extracted}"
