/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/host_roce/host_roce_handshake.h"

#include <algorithm>
#include <array>
#include "common/hixl_checker.h"

namespace hixl {
namespace {
constexpr uint32_t kWireMagic = 0x48524345U;
constexpr uint32_t kWireVersion = 1U;
constexpr size_t kHandshakeWireBytes = 128U;
constexpr uint32_t kUint32Bytes = 4U;
constexpr uint32_t kBitsPerByte = 8U;
constexpr uint32_t kHighWordShift = 32U;
constexpr uint32_t kMaxWirePsn = 0xffffffU;
constexpr uint32_t kMaxWirePlaneCount = 256U;
constexpr uint32_t kWireFlagHasFeature = 1U;
constexpr uint32_t kWireFlagMore = 2U;
constexpr uint32_t kWireFlagsMask = kWireFlagHasFeature | kWireFlagMore;

class Writer {
 public:
  void Reserve(size_t bytes) {
    data_.reserve(bytes);
  }
  void U32(uint32_t n) {
    for (int i = static_cast<int>(kUint32Bytes) - 1; i >= 0; --i) {
      data_.push_back(static_cast<uint8_t>(n >> (static_cast<uint32_t>(i) * kBitsPerByte)));
    }
  }
  void U64(uint64_t n) {
    U32(static_cast<uint32_t>(n >> kHighWordShift));
    U32(static_cast<uint32_t>(n));
  }
  template <size_t N>
  void Bytes(const std::array<uint8_t, N> &a) {
    data_.insert(data_.end(), a.begin(), a.end());
  }
  std::vector<uint8_t> Take() {
    return std::move(data_);
  }

 private:
  std::vector<uint8_t> data_;
};

class Reader {
 public:
  Reader(const void *data, size_t size) : data_(static_cast<const uint8_t *>(data)), size_(size) {}
  uint32_t U32() {
    if (size_ < kUint32Bytes) {
      valid_ = false;
      return 0;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < kUint32Bytes; ++i) {
      n = (n << kBitsPerByte) | *data_++;
    }
    size_ -= kUint32Bytes;
    return n;
  }
  uint64_t U64() {
    const uint64_t hi = U32();
    return (hi << kHighWordShift) | U32();
  }
  template <size_t N>
  void Bytes(std::array<uint8_t, N> &a) {
    if (size_ < N) {
      valid_ = false;
      return;
    }
    std::copy_n(data_, N, a.begin());
    data_ += N;
    size_ -= N;
  }
  bool Done() const {
    return valid_ && size_ == 0U;
  }

 private:
  const uint8_t *data_;
  size_t size_;
  bool valid_{true};
};

bool IsValidStep(uint32_t step) {
  return step == static_cast<uint32_t>(HostRoceHandshakeStep::kPrepare) ||
         step == static_cast<uint32_t>(HostRoceHandshakeStep::kNegotiate) ||
         step == static_cast<uint32_t>(HostRoceHandshakeStep::kReady);
}
}  // namespace

std::vector<uint8_t> EncodeHostRoceHandshake(const HostRoceHandshake &value) {
  Writer w;
  w.Reserve(kHandshakeWireBytes);
  w.U32(kWireMagic);
  w.U32(kWireVersion);
  w.U32(static_cast<uint32_t>(value.step));
  w.U64(value.index);
  w.U32(value.peer.qpn);
  w.U32(value.peer.psn);
  w.U32(value.peer.mtu);
  w.U32(value.peer.read_depth);
  w.U32(value.peer.plane_count);
  w.U32(value.peer.plane);
  w.Bytes(value.peer.gid);
  w.Bytes(value.peer.feature);
  w.U32((value.peer.has_feature ? kWireFlagHasFeature : 0U) | (value.peer.more ? kWireFlagMore : 0U));
  return w.Take();
}

Status DecodeHostRoceHandshake(const void *data, size_t size, HostRoceHandshake &value) {
  HIXL_CHK_BOOL_RET_STATUS(data != nullptr && size == kHandshakeWireBytes, PARAM_INVALID,
                           "[HostRoce] Invalid handshake payload size, size=%zu", size);
  Reader r(data, size);
  const uint32_t magic = r.U32();
  const uint32_t version = r.U32();
  const uint32_t step = r.U32();
  HIXL_CHK_BOOL_RET_STATUS(magic == kWireMagic && version == kWireVersion && IsValidStep(step), PARAM_INVALID,
                           "[HostRoce] Invalid handshake header, magic=0x%x, version=%u, step=%u", magic, version,
                           step);
  HostRoceHandshake out;
  out.step = static_cast<HostRoceHandshakeStep>(step);
  out.index = r.U64();
  out.peer.qpn = r.U32();
  out.peer.psn = r.U32();
  out.peer.mtu = r.U32();
  out.peer.read_depth = r.U32();
  out.peer.plane_count = r.U32();
  out.peer.plane = r.U32();
  r.Bytes(out.peer.gid);
  r.Bytes(out.peer.feature);
  const uint32_t flags = r.U32();
  const bool valid_plane = (out.peer.plane_count == 0U && out.peer.plane == kHostRoceNoPlane) ||
                           (out.peer.plane_count > 0U && out.peer.plane_count <= kMaxWirePlaneCount &&
                            (out.peer.plane == kHostRoceNoPlane || out.peer.plane < out.peer.plane_count));
  HIXL_CHK_BOOL_RET_STATUS(r.Done() && (flags & ~kWireFlagsMask) == 0U && out.peer.psn <= kMaxWirePsn && valid_plane,
                           PARAM_INVALID, "[HostRoce] Invalid handshake fields");
  out.peer.has_feature = (flags & kWireFlagHasFeature) != 0U;
  out.peer.more = (flags & kWireFlagMore) != 0U;
  value = out;
  return SUCCESS;
}
}  // namespace hixl
