#!/bin/bash
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------

# One-click script to download and package the third-party open-source software required for offline
# compilation. Run it in an environment with Internet access, upload opensource.tar.gz to the offline
# environment, and copy its content to {your_3rd_party_path}.
# Keep the download list in sync with the third-party table in docs/zh/build.md and docs/en/build.md.

set -e

# Tarballs that must be placed at the top level of {your_3rd_party_path}
DOWNLOAD_LIST=(
    "https://gitcode.com/cann-src-third-party/googletest/releases/download/v1.14.0/googletest-1.14.0.tar.gz"
    "https://gitcode.com/cann-src-third-party/json/releases/download/v3.12.0/json-3.12.0.tar.gz"
    "https://gitcode.com/cann-src-third-party/pybind11/releases/download/v2.13.6/pybind11-2.13.6.tar.gz"
    "https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/makeself/makeself-release-2.5.0.tar.gz"
)

# Patch files that must be placed in {your_3rd_party_path}/patch
PATCH_LIST=(
    "https://cann-3rd.obs.cn-north-4.myhuaweicloud.com/makeself/fix/makeself-2.5.0.patch"
)

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

CANN_CMAKE_TAG=$(grep -oP 'set\(CANN_CMAKE_TAG\s+"([^"]+)"\)' "${REPO_ROOT}/cmake/fetch_cann_cmake.cmake" | grep -oP '"[^"]+"' | tr -d '"' || true)
if [ -z "${CANN_CMAKE_TAG}" ]; then
    echo "Error: failed to read CANN_CMAKE_TAG from cmake/fetch_cann_cmake.cmake."
    exit 1
fi
DOWNLOAD_LIST+=("https://raw.gitcode.com/cann/cmake/archive/refs/heads/${CANN_CMAKE_TAG}.tar.gz")

command -v wget >/dev/null 2>&1 || {
    echo "Error: wget is required. Install it first, for example: sudo apt-get install wget."
    exit 1
}

WORK_DIR="${REPO_ROOT}/opensource"
OUTPUT_FILE="${REPO_ROOT}/opensource.tar.gz"
rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}/patch"

download_file() {
    local url="$1"
    local dest="$2"
    echo "Downloading ${url}"
    if ! wget -q -O "${dest}" "${url}" --show-progress; then
        echo "Error: failed to download ${url}."
        exit 1
    fi
}

for url in "${DOWNLOAD_LIST[@]}"; do
    download_file "${url}" "${WORK_DIR}/$(basename "${url}")"
done

for url in "${PATCH_LIST[@]}"; do
    download_file "${url}" "${WORK_DIR}/patch/$(basename "${url}")"
done

# The build looks up the archive by the exact name cmake-${CANN_CMAKE_TAG}.tar.gz.
mv "${WORK_DIR}/${CANN_CMAKE_TAG}.tar.gz" "${WORK_DIR}/cmake-${CANN_CMAKE_TAG}.tar.gz"

echo "Packing ${OUTPUT_FILE}"
if ! tar -zcf "${OUTPUT_FILE}" -C "${REPO_ROOT}" opensource; then
    echo "Error: failed to generate ${OUTPUT_FILE}."
    exit 1
fi
rm -rf "${WORK_DIR}"

echo "Third-party open-source software downloaded and packed: ${OUTPUT_FILE}"
echo "Offline usage: tar -xzf opensource.tar.gz && cp -r opensource/* {your_3rd_party_path}/"
exit 0
