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
# Select read-only source refs for a log investigation. This script never checks out a ref.

set -euo pipefail

base_dir="${HOME}/hixl-troubleshooting"
before=""
pins=()
repos=()
allowed_repos=(runtime hcomm driver hixl hixl.wiki)
default_repos=(runtime hcomm driver hixl)

usage() {
  cat <<EOF
Usage: $0 [--before YYYY-MM-DD] [--ref REPO=REF]... [--repo REPO]...

Select a source commit for troubleshooting without changing any worktree.

  --ref REPO=REF    Exact commit, tag, or ref for one repository (highest confidence).
  --before DATE     Fallback: select the last commit not later than DATE (date approximation).
  --repo REPO       Limit output to one repository; may be repeated.

With no --repo, checks: ${default_repos[*]}.

Examples:
  $0 --ref hixl=abc123 --ref hcomm=v9.0.0
  $0 --before 2026-06-16

Use the selected SHA read-only, for example:
  git -C ${base_dir}/hcomm show <sha>:path/to/file.cc
EOF
}

is_allowed_repo() {
  local target_repo="$1"
  local repo
  for repo in "${allowed_repos[@]}"; do
    [ "${repo}" = "${target_repo}" ] && return 0
  done
  return 1
}

validate_repo() {
  local repo="$1"
  if ! is_allowed_repo "${repo}"; then
    echo "Unsupported repository '${repo}'." >&2
    exit 2
  fi
}

parse_pin() {
  local pin="$1"
  local repo="${pin%%=*}"
  local ref="${pin#*=}"
  if [ "${repo}" = "${pin}" ] || [ -z "${repo}" ] || [ -z "${ref}" ]; then
    echo "Invalid --ref '${pin}'; expected REPO=REF." >&2
    exit 2
  fi
  validate_repo "${repo}"
}

reference_for_repo() {
  local target_repo="$1"
  local pin repo ref
  for pin in "${pins[@]}"; do
    repo="${pin%%=*}"
    ref="${pin#*=}"
    if [ "${repo}" = "${target_repo}" ]; then
      printf '%s\n' "${ref}"
      return 0
    fi
  done
  return 1
}

select_commit() {
  local repo="$1"
  local directory="${base_dir}/${repo}"
  local ref sha method commit_date
  if [ ! -d "${directory}/.git" ]; then
    echo "WARN: ${repo} is unavailable at ${directory}; run deps.sh first." >&2
    return 1
  fi

  ref="$(reference_for_repo "${repo}" || true)"
  if [ -n "${ref}" ]; then
    if ! sha="$(git -C "${directory}" rev-parse --verify "${ref}^{commit}" 2>/dev/null)"; then
      echo "WARN: ${repo} cannot resolve exact ref '${ref}'." >&2
      return 1
    fi
    method="exact-ref"
  elif [ -n "${before}" ]; then
    if [ "$(git -C "${directory}" rev-parse --is-shallow-repository)" = "true" ]; then
      echo "WARN: ${repo} is shallow; run deps.sh before date-based alignment." >&2
      return 1
    fi
    sha="$(git -C "${directory}" log -1 --before="${before} 23:59:59" --format='%H')"
    if [ -z "${sha}" ]; then
      echo "WARN: ${repo} has no commit at or before ${before}." >&2
      return 1
    fi
    method="date-approximation"
  else
    echo "WARN: ${repo} has no exact --ref and no --before fallback." >&2
    return 1
  fi

  commit_date="$(git -C "${directory}" show -s --format='%cI' "${sha}")"
  printf '%s\t%s\t%s\t%s\n' "${repo}" "${sha}" "${commit_date}" "${method}"
}

while [ $# -gt 0 ]; do
  case "$1" in
    --before)
      [ $# -ge 2 ] || { echo "--before requires YYYY-MM-DD" >&2; exit 2; }
      before="$2"
      shift 2
      ;;
    --ref)
      [ $# -ge 2 ] || { echo "--ref requires REPO=REF" >&2; exit 2; }
      parse_pin "$2"
      pins+=("$2")
      shift 2
      ;;
    --repo)
      [ $# -ge 2 ] || { echo "--repo requires a repository name" >&2; exit 2; }
      validate_repo "$2"
      repos+=("$2")
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [ -n "${before}" ] && ! [[ "${before}" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]]; then
  echo "Invalid --before '${before}'; expected YYYY-MM-DD." >&2
  exit 2
fi
if [ "${#repos[@]}" -eq 0 ]; then
  repos=("${default_repos[@]}")
fi

echo -e "repo\tcommit\tcommit_date\talignment"
echo "NOTE: date-approximation is not proof that a CANN package used this exact source commit." >&2

rc=0
for repo in "${repos[@]}"; do
  select_commit "${repo}" || rc=1
done
exit "${rc}"
