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

set -euo pipefail

base_dir="${HOME}/hixl-troubleshooting"
max_age_days=7

usage() {
  echo "Usage: $0"
  echo "  Clone/update CANN dependency repos under ${base_dir}"
  echo "  Keeps complete commit history and tags for version-aligned code verification."
  echo "  Reuses existing clones; pulls if last update is older than ${max_age_days} days."
}

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

mkdir -p "${base_dir}"

needs_update() {
  local dst="$1" stamp mtime age
  [ -d "${dst}/.git" ] || return 0
  stamp="${dst}/.last_update"
  [ -f "${stamp}" ] || return 0
  mtime="$(stat -c %Y "${stamp}" 2>/dev/null || stat -f %m "${stamp}" 2>/dev/null || echo 0)"
  age=$(( ( $(date +%s) - mtime ) / 86400 ))
  [ "${age}" -ge "${max_age_days}" ]
}

ensure_full_history() {
  local repo="$1"
  local dst="$2"
  local shallow attempt
  shallow="$(git -C "${dst}" rev-parse --is-shallow-repository 2>/dev/null || echo true)"
  [ "${shallow}" = "true" ] || return 0
  for attempt in 1 2 3; do
    echo "Fetching complete history for ${repo} at ${dst}"
    if git -C "${dst}" fetch --unshallow --tags; then
      return 0
    fi
    echo "History fetch for ${repo} failed (attempt ${attempt}/3), retrying..." >&2
    sleep "$((attempt * 2))"
  done
  return 1
}

clone_or_update_gitcode() {
  local repo="$1"
  local dst="${base_dir}/${repo}"
  local url="https://gitcode.com/cann/${repo}.git"

  if [ -d "${dst}/.git" ]; then
    if ! ensure_full_history "${repo}" "${dst}"; then
      echo "WARN: complete history fetch failed for ${repo}; code verification cannot align history." >&2
      return 1
    fi
    if needs_update "${dst}"; then
      echo "Updating ${repo} at ${dst}"
      if git -C "${dst}" pull --ff-only; then
        touch "${dst}/.last_update"
      else
        echo "WARN: pull --ff-only failed for ${repo}; keeping existing copy." >&2
        return 1
      fi
    else
      echo "Reusing ${repo} at ${dst}"
    fi
    return 0
  fi

  echo "Cloning ${repo} to ${dst}"
  rm -rf "${dst}"
  local attempt
  for attempt in 1 2 3; do
    if git clone "${url}" "${dst}"; then
      touch "${dst}/.last_update"
      return 0
    fi
    echo "Clone ${repo} failed (attempt ${attempt}/3), retrying..." >&2
    rm -rf "${dst}"
    sleep "$((attempt * 2))"
  done
  echo "Failed to clone ${repo} after 3 attempts" >&2
  return 1
}

rc=0
for repo in runtime hcomm driver hixl hixl.wiki; do
  clone_or_update_gitcode "${repo}" || { rc=1; echo "WARN: ${repo} unavailable (code verification incomplete for this repo)." >&2; }
done

echo
if [ "${rc}" -eq 0 ]; then
  echo "Dependencies are available under ${base_dir}"
else
  echo "Some dependencies unavailable; see warnings above. Available repos under ${base_dir}" >&2
fi
exit "${rc}"
