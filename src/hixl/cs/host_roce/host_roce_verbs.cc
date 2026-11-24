/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/host_roce/host_roce_verbs.h"
#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdlib>
#include <cstring>
#include "acl/acl_rt.h"
#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/hixl_utils.h"
#include "common/scope_guard.h"

#if defined(__linux__)
#include <arpa/inet.h>
#include <fstream>
#include <infiniband/verbs.h>
#include <iterator>
#include <sys/random.h>
#include "cs/host_roce/host_roce_verbs_loader.h"
#include "cs/host_roce/ibv_extend_compat.h"
#include "proxy/ascend_hal_proxy.h"
#include "securec.h"
#endif

namespace hixl {
namespace {
#if defined(__linux__)
constexpr int kMaxPlaneCount = 256;
constexpr int kMaxReadDepth = 16;
constexpr size_t kIpv4MappedIndex10 = 10U;
constexpr size_t kIpv4MappedIndex11 = 11U;
constexpr uint8_t kIpv4MappedFill = 0xffU;
constexpr size_t kIpv4MappedPrefixOffset = 12U;
constexpr uint32_t kPsnMask = 0xffffffU;
constexpr int kExtendedQpFeatureMask = 1 << 1;
constexpr uint8_t kMinRnrTimer = 12U;
constexpr uint8_t kDefaultGrhHopLimit = 64U;
constexpr uint8_t kMaxRnrRetry = 7U;
constexpr int kPollCqBatchSize = kHostRocePollBatchSize;
constexpr size_t kLocalFlushBytes = sizeof(uint64_t);
constexpr int kVendorFlushOpcode = 0x40;
constexpr uint64_t kLocalFlushWrId = 1ULL;
constexpr uint8_t kUbMainboardIds[] = {0x03, 0x05, 0x07, 0x21, 0x23, 0x25, 0x27, 0x29, 0x2b,
                                       0x2d, 0x2f, 0x31, 0x33, 0x44, 0x46, 0x48, 0x4a, 0x4c};

const char *SafeDeviceName(const char *name) {
  return name != nullptr ? name : "unknown";
}

Status VerbsError(int ret, const char *api) {
  if (ret == 0) {
    return SUCCESS;
  }
  HIXL_LOGE(FAILED, "[HostRoce] Call api:%s failed, ret=%d, errno=%d", api, ret, errno);
  return FAILED;
}
struct Device final : HostRoceDevice {
  std::shared_ptr<HostRoceVerbsLibrary> api;
  ibv_context *context{nullptr};
  ibv_pd *pd{nullptr};
  ibv_context_extend *extended{nullptr};
  uint8_t port{0};
  uint8_t gid_index{0};
  ~Device() override {
    if (pd != nullptr) {
      (void)VerbsError(api->free_pd(pd), "ibv_dealloc_pd");
    }
    if (extended != nullptr) {
      (void)VerbsError(api->close_extend(extended), "ibv_close_extend");
    }
    if (context != nullptr) {
      (void)VerbsError(api->close(context), "ibv_close_device");
    }
  }
};
struct Segment {
  Segment() = default;
  Segment(const Segment &) = delete;
  Segment &operator=(const Segment &) = delete;
  Segment(Segment &&) = delete;
  Segment &operator=(Segment &&) = delete;
  uint32_t device{0};
  uint64_t base{0};
  uint64_t size{0};
  ~Segment() {
    const Status ret = AscendHalProxy::MemUnRegUbSegment(device, base, size);
    if (ret != SUCCESS) {
      HIXL_LOGE(ret, "[HostRoce] MemUnRegUbSegment failed, device=%u, size=%" PRIu64 " bytes", device, size);
    }
  }
};
struct Memory final : HostRoceMr {
  std::shared_ptr<Device> device;
  std::shared_ptr<Segment> segment;
  aclrtContext context{nullptr};
  ibv_mr *mr{nullptr};
  ~Memory() override {
    if (mr == nullptr || VerbsError(device->api->dereg_mr(mr), "ibv_dereg_mr") == SUCCESS) {
      return;
    }
    // Failed deregistration still owns a provider MR. Do not unpin its UB segment or close its PD.
    static std::mutex quarantine_mutex;
    using Retained = std::tuple<std::shared_ptr<Device>, std::shared_ptr<Segment>, ibv_mr *>;
    static auto *quarantine = new std::vector<Retained>();
    std::lock_guard<std::mutex> lock(quarantine_mutex);
    quarantine->emplace_back(std::move(device), std::move(segment), mr);
  }
};
struct Qp final : HostRoceQp {
  std::shared_ptr<Device> device;
  ibv_qp *qp{nullptr};
  ibv_cq *cq{nullptr};
  std::vector<ibv_sge> sge_scratch;
  std::vector<ibv_send_wr> wr_scratch;
  ~Qp() override {
    if (qp != nullptr) {
      (void)VerbsError(device->api->destroy_qp(qp), "ibv_destroy_qp");
    }
    if (cq != nullptr) {
      (void)VerbsError(device->api->destroy_cq(cq), "ibv_destroy_cq");
    }
  }
};
Status StopNativeQp(Qp &q) {
  if (q.qp != nullptr) {
    ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_ERR;
    (void)VerbsError(q.device->api->modify_qp(q.qp, &attr, IBV_QP_STATE), "ibv_modify_qp ERR");
    HIXL_CHK_STATUS_RET(VerbsError(q.device->api->destroy_qp(q.qp), "ibv_destroy_qp"));
    q.qp = nullptr;
  }
  if (q.cq != nullptr) {
    HIXL_CHK_STATUS_RET(VerbsError(q.device->api->destroy_cq(q.cq), "ibv_destroy_cq"));
    q.cq = nullptr;
  }
  return SUCCESS;
}
bool MatchGid(const std::string &ip, const ibv_gid &gid) {
  ibv_gid expected{};
  if (inet_pton(AF_INET6, ip.c_str(), expected.raw) == 1) {
    return std::memcmp(gid.raw, expected.raw, kHostRoceGidBytes) == 0;
  }
  expected.raw[kIpv4MappedIndex10] = kIpv4MappedFill;
  expected.raw[kIpv4MappedIndex11] = kIpv4MappedFill;
  return inet_pton(AF_INET, ip.c_str(), expected.raw + kIpv4MappedPrefixOffset) == 1 &&
         std::memcmp(gid.raw, expected.raw, kHostRoceGidBytes) == 0;
}
bool IsRoceV2Gid(const char *name, uint8_t port, int index) {
  const std::string path = std::string("/sys/class/infiniband/") + name + "/ports/" + std::to_string(port) +
                           "/gid_attrs/types/" + std::to_string(index);
  errno = 0;
  std::ifstream file(path);
  const auto open_error = errno;
  if (!file.is_open()) {
    HIXL_LOGW("[HostRoce] Open GID type file failed, path=%s, errno=%d", path.c_str(), open_error);
    return false;
  }
  std::string type;
  errno = 0;
  const bool read = static_cast<bool>(std::getline(file, type));
  const auto read_error = errno;
  if (!read) {
    HIXL_LOGW("[HostRoce] Read GID type file failed, path=%s, state=%u, errno=%d", path.c_str(),
              static_cast<unsigned>(file.rdstate()), read_error);
    return false;
  }
  HIXL_LOGD("[HostRoce] GID type, path=%s, value='%s', length=%zu bytes, roce_v2=%d", path.c_str(), type.c_str(),
            type.size(), static_cast<int>(type == "RoCE v2"));
  return type == "RoCE v2";
}
Status FindPortGid(Device &d, const char *name, const std::string &ip, uint8_t port, const ibv_port_attr &attr) {
  uint32_t query_errors = 0;
  for (int i = 0; i < attr.gid_tbl_len && i <= UINT8_MAX; ++i) {
    ibv_gid gid{};
    errno = 0;
    const auto ret = d.api->query_gid(d.context, port, i, &gid);
    const auto error = errno;
    if (ret != 0) {
      if (query_errors++ == 0) {
        HIXL_LOGW("[HostRoce] ibv_query_gid failed, nic=%s, port=%u, index=%d, ret=%d, errno=%d", name, port, i, ret,
                  error);
      }
      continue;
    }
    const bool matches = MatchGid(ip, gid);
    if (!std::all_of(std::begin(gid.raw), std::end(gid.raw), [](uint8_t byte) { return byte == 0; })) {
      char value[INET6_ADDRSTRLEN]{};
      const auto *formatted = inet_ntop(AF_INET6, gid.raw, value, sizeof(value));
      HIXL_LOGD("[HostRoce] GID candidate, nic=%s, port=%u, index=%d, gid=%s, target_ip=%s, matches=%d", name, port, i,
                formatted != nullptr ? formatted : "invalid", ip.c_str(), static_cast<int>(matches));
    }
    if (!matches || !IsRoceV2Gid(name, port, i)) {
      continue;
    }
    d.port = port;
    d.gid_index = static_cast<uint8_t>(i);
    std::copy_n(gid.raw, kHostRoceGidBytes, d.local.gid.begin());
    d.local.mtu = attr.active_mtu;
    d.max_message_size = attr.max_msg_sz;
    HIXL_LOGI("[HostRoce] Selected RoCEv2 GID, nic=%s, port=%u, gid_index=%d, ip=%s, mtu=%u, max_msg_sz=%u", name, port,
              i, ip.c_str(), static_cast<uint32_t>(attr.active_mtu), attr.max_msg_sz);
    return SUCCESS;
  }
  HIXL_LOGI("[HostRoce] No matching GID on port, nic=%s, port=%u, target_ip=%s, gid_tbl_len=%d, query_errors=%u", name,
            port, ip.c_str(), attr.gid_tbl_len, query_errors);
  return NOT_CONNECTED;
}
Status FindGid(Device &d, const char *name, const std::string &ip, const ibv_device_attr &attr) {
  for (uint32_t port = 1; port <= attr.phys_port_cnt; ++port) {
    ibv_port_attr p{};
    errno = 0;
    const auto ret =
        d.api->query_port(d.context, static_cast<uint8_t>(port), reinterpret_cast<_compat_ibv_port_attr *>(&p));
    const auto error = errno;
    HIXL_LOGI("[HostRoce] Query port, nic=%s, port=%u, ret=%d, errno=%d, state=%u, link_layer=%u, gid_tbl_len=%d", name,
              port, ret, error, static_cast<unsigned>(p.state), p.link_layer, p.gid_tbl_len);
    if (ret != 0 || p.state != IBV_PORT_ACTIVE || p.link_layer != IBV_LINK_LAYER_ETHERNET) {
      continue;
    }
    if (FindPortGid(d, name, ip, static_cast<uint8_t>(port), p) == SUCCESS) {
      return SUCCESS;
    }
  }
  return NOT_CONNECTED;
}
class NativeVerbs final : public HostRoceVerbs {
 public:
  Status Open(const std::string &ip, std::shared_ptr<HostRoceDevice> &out) override;
  Status Register(const std::shared_ptr<HostRoceDevice> &device, uint64_t base, uint64_t size, bool device_memory,
                  uint32_t memory_device, std::shared_ptr<HostRoceMr> &out) override;
  Status Deregister(HostRoceMr &mr) override;
  Status CreateQp(const std::shared_ptr<HostRoceDevice> &device, const HostRoceConfig &config,
                  std::unique_ptr<HostRoceQp> &out) override;
  Status Negotiate(HostRoceQp &qp, const HostRocePeer &peer, HostRocePeer &local) override;
  Status Connect(HostRoceQp &qp, const HostRocePeer &peer, const HostRoceConfig &config) override;
  Status PostBatch(HostRoceQp &qp, bool read, const std::vector<HostRoceWorkRequest> &wrs, size_t &posted) override;
  Status Poll(HostRoceQp &qp, std::vector<HostRoceCompletion> &out) override;
  Status CreateFlush(const std::shared_ptr<HostRoceDevice> &device, const std::shared_ptr<HostRoceMr> &target,
                     const HostRoceConfig &config, std::unique_ptr<HostRoceFlush> &flush) override;
  Status PostFlush(HostRoceFlush &flush) override;
  Status PollFlush(HostRoceFlush &flush, bool &done) override;
  Status Stop(HostRoceQp &qp) override;

 private:
  Status LoadOptional(Device &device, const ibv_device_attr &attr);
  Status LoadPlanes(Device &device, const ibv_device_attr &attr);
  Status InitializeQp(Qp &qp, const HostRoceConfig &config) const;
  Status ApplyQpFeature(const Qp &qp) const;
  Status RefreshGid(const Qp &qp, const ibv_port_attr &port, uint8_t &index) const;
  Status CreateDevice(ibv_device *nic, const std::string &ip, std::shared_ptr<Device> &out);
  Status SegmentFor(uint32_t user_device, uint64_t base, uint64_t size, std::shared_ptr<Segment> &segment);
  static Status AllocateFlushBuffers(HostRoceFlushResources &resources, uint32_t memory_device);
  Status RegisterFlushBuffer(const std::shared_ptr<HostRoceDevice> &device, void *address, bool hbm,
                             uint32_t memory_device, std::shared_ptr<HostRoceMr> &mr);
  Status ConnectFlushPair(const std::shared_ptr<HostRoceDevice> &device, const HostRoceConfig &channel_config,
                          HostRoceFlushResources &resources);
  Status ToRtr(const Qp &qp, const HostRocePeer &peer, const HostRoceConfig &config, uint32_t &path_mtu,
               uint8_t &sgid_index) const;
  std::shared_ptr<HostRoceVerbsLibrary> api_;
  std::mutex mutex_;
  std::map<std::string, std::weak_ptr<Device>> devices_;
  std::map<std::string, std::shared_ptr<std::atomic<uint64_t>>> plane_counters_;
  std::map<std::tuple<uint32_t, uint64_t, uint64_t>, std::weak_ptr<Segment>> segments_;
};
Status NativeVerbs::LoadPlanes(Device &device, const ibv_device_attr &attr) {
  api_->LoadHrn();
  if (!HostRoceVerbsLibrary::IsHrnVendor(attr)) {
    return SUCCESS;
  }
  // These HRN-capable NIC models must expose the plane query even when it reports a single/no plane.
  // This is a provider check; UB versus PCIe memory routing is determined separately through ACL/HAL.
  const bool hrn_required = HostRoceVerbsLibrary::RequiresHrnPlanes(attr);
  HIXL_CHK_BOOL_RET_STATUS(!hrn_required || api_->plane_count != nullptr, UNSUPPORTED,
                           "[HostRoce] HRN NIC requires roce_get_qp_num");
  if (api_->plane_count == nullptr) {
    return SUCCESS;
  }
  int count = 0;
  const auto ret = api_->plane_count(device.context, &count);
  if (!hrn_required && (ret == EOPNOTSUPP || ret == -EOPNOTSUPP)) {
    return SUCCESS;
  }
  HIXL_CHK_STATUS_RET(VerbsError(ret, "roce_get_qp_num"));
  HIXL_CHK_BOOL_RET_STATUS(count >= 0 && count <= kMaxPlaneCount, UNSUPPORTED, "[HostRoce] Invalid plane count=%d",
                           count);
  HIXL_CHK_BOOL_RET_STATUS(count == 0 || api_->set_plane != nullptr, UNSUPPORTED,
                           "[HostRoce] HRN plane selection API is unavailable");
  device.local.plane_count = static_cast<uint32_t>(count);
  return SUCCESS;
}
Status NativeVerbs::LoadOptional(Device &device, const ibv_device_attr &attr) {
  HIXL_CHK_STATUS_RET(LoadPlanes(device, attr));
  api_->LoadExtend();
  if (api_->extend != nullptr) {
    device.extended = api_->open_extend(device.context);
    HIXL_LOGI("[HostRoce] Extend context, nic=%s, enabled=%d, reason=%s",
              SafeDeviceName(api_->name(device.context->device)), static_cast<int>(device.extended != nullptr),
              device.extended != nullptr ? "opened" : "ibv_open_extend_returned_null");
  }
  return SUCCESS;
}
Status NativeVerbs::CreateDevice(ibv_device *nic, const std::string &ip, std::shared_ptr<Device> &out) {
  const char *nic_name = SafeDeviceName(api_->name(nic));
  auto d = std::make_shared<Device>();
  d->api = api_;
  errno = 0;
  d->context = api_->open(nic);
  if (d->context == nullptr) {
    const int saved_errno = errno;
    HIXL_LOGW("[HostRoce] ibv_open_device failed, nic=%s, errno=%d", nic_name, saved_errno);
    return NOT_CONNECTED;
  }
  ibv_device_attr attr{};
  errno = 0;
  const int query_ret = api_->query_device(d->context, &attr);
  if (query_ret != 0) {
    const int saved_errno = errno;
    HIXL_LOGW("[HostRoce] ibv_query_device failed, nic=%s, ret=%d, errno=%d", nic_name, query_ret, saved_errno);
    return NOT_CONNECTED;
  }
  HIXL_LOGI("[HostRoce] Query device, nic=%s, target_ip=%s, phys_port_cnt=%u, vendor_id=%u, vendor_part_id=%u",
            nic_name, ip.c_str(), attr.phys_port_cnt, attr.vendor_id, attr.vendor_part_id);
  // A GID miss only rejects this candidate. Open reports when no device matches.
  const auto gid_status = FindGid(*d, nic_name, ip, attr);
  if (gid_status != SUCCESS) {
    return gid_status;
  }
  const auto key = std::string(nic_name) + ":" + std::to_string(d->port);
  auto &counter = plane_counters_[key];
  if (counter == nullptr) {
    counter = std::make_shared<std::atomic<uint64_t>>(0);
  }
  d->next_plane = counter;
  const int read_depth = std::min({kMaxReadDepth, attr.max_qp_rd_atom, attr.max_qp_init_rd_atom});
  HIXL_CHK_BOOL_RET_STATUS(d->max_message_size > 0 && attr.max_qp_wr > 0 && attr.max_cqe > 0 && read_depth > 0,
                           UNSUPPORTED, "[HostRoce] Invalid verbs device limits");
  d->max_wr = static_cast<uint32_t>(attr.max_qp_wr);
  d->max_cqe = static_cast<uint32_t>(attr.max_cqe);
  d->local.read_depth = static_cast<uint32_t>(read_depth);
  d->resolve_device_memory =
      true;  // Resolve allocation ranges and HAL topology for Device MRs, independently of plane count.
  HIXL_CHK_STATUS_RET(LoadOptional(*d, attr));
  errno = 0;
  d->pd = api_->alloc_pd(d->context);
  const int pd_errno = errno;
  HIXL_CHK_BOOL_RET_STATUS(d->pd != nullptr, FAILED, "[HostRoce] ibv_alloc_pd failed, errno=%d", pd_errno);
  HIXL_EVENT(
      "[HostRoce] Opened device, nic=%s, ip=%s, port=%u, vendor_id=0x%x, vendor_part_id=0x%x, "
      "active_mtu=%u, max_msg_sz=%u, max_qp_wr=%u, max_cqe=%u, max_sge=%d, read_depth=%u "
      "(rd_atom=%d, init_rd_atom=%d), plane_count=%u, extend=%d",
      nic_name, ip.c_str(), static_cast<uint32_t>(d->port), attr.vendor_id, attr.vendor_part_id, d->local.mtu,
      d->max_message_size, d->max_wr, d->max_cqe, attr.max_sge, d->local.read_depth, attr.max_qp_rd_atom,
      attr.max_qp_init_rd_atom, d->local.plane_count, static_cast<int>(d->extended != nullptr));
  out = std::move(d);
  return SUCCESS;
}
Status NativeVerbs::Open(const std::string &ip, std::shared_ptr<HostRoceDevice> &out) {
  ibv_gid address{};
  const bool valid =
      inet_pton(AF_INET, ip.c_str(), address.raw) == 1 || inet_pton(AF_INET6, ip.c_str(), address.raw) == 1;
  HIXL_CHK_BOOL_RET_STATUS(valid, PARAM_INVALID, "[HostRoce] Numeric IPv4/IPv6 address required, ip=%s", ip.c_str());
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = devices_.find(ip);
  if (it != devices_.end()) {
    out = it->second.lock();
    if (out != nullptr) {
      return SUCCESS;
    }
  }
  if (api_ == nullptr) {
    auto api = std::make_shared<HostRoceVerbsLibrary>();
    HIXL_CHK_STATUS_RET(api->Initialize());
    api_ = std::move(api);
  }
  int count = 0;
  errno = 0;
  auto **nics = api_->devices(&count);
  const int dev_errno = errno;
  HIXL_CHK_BOOL_RET_STATUS(nics != nullptr, FAILED, "[HostRoce] ibv_get_device_list failed, errno=%d", dev_errno);
  ScopeGuard cleanup([&] { api_->free_devices(nics); });
  for (int i = 0; i < count; ++i) {
    std::shared_ptr<Device> d;
    const Status ret = CreateDevice(nics[i], ip, d);
    if (ret == NOT_CONNECTED) {
      continue;
    }
    HIXL_CHK_STATUS_RET(ret);
    devices_[ip] = d;
    out = std::move(d);
    return SUCCESS;
  }
  HIXL_LOGE(NOT_CONNECTED, "[HostRoce] No active RoCEv2 GID matches endpoint IP=%s", ip.c_str());
  return NOT_CONNECTED;
}
Status NativeVerbs::SegmentFor(uint32_t user_device, uint64_t base, uint64_t size, std::shared_ptr<Segment> &segment) {
  HIXL_CHK_BOOL_RET_STATUS(user_device <= static_cast<uint32_t>(INT32_MAX), PARAM_INVALID,
                           "[HostRoce] Invalid user device ID=%u for segment registration", user_device);
  int64_t board = 0;
  const aclError info_ret = aclrtGetDeviceInfo(user_device, ACL_DEV_ATTR_MAINBOARD_ID, &board);
  if (info_ret != ACL_SUCCESS) {
    HIXL_LOGW("[HostRoce] aclrtGetDeviceInfo mainboard failed, ret=%d, user_device=%u; using PCIe registration",
              static_cast<int32_t>(info_ret), user_device);
    return SUCCESS;
  }
  if (std::find(std::begin(kUbMainboardIds), std::end(kUbMainboardIds), board) == std::end(kUbMainboardIds)) {
    return SUCCESS;
  }
  int32_t logic_device = -1;
  HIXL_CHK_ACL_RET(aclrtGetLogicDevIdByUserDevId(static_cast<int32_t>(user_device), &logic_device),
                   "[HostRoce] aclrtGetLogicDevIdByUserDevId failed, user_device=%u", user_device);
  HIXL_CHK_BOOL_RET_STATUS(logic_device >= 0, PARAM_INVALID, "[HostRoce] Invalid logic device ID=%d for user_device=%u",
                           logic_device, user_device);
  const auto logic_dev_id = static_cast<uint32_t>(logic_device);
  std::lock_guard<std::mutex> lock(mutex_);
  const auto seg_key = std::make_tuple(logic_dev_id, base, size);
  const auto seg_it = segments_.find(seg_key);
  if (seg_it != segments_.end()) {
    segment = seg_it->second.lock();
    if (segment != nullptr) {
      return SUCCESS;
    }
    segments_.erase(seg_it);
  }
  HIXL_CHK_STATUS_RET(AscendHalProxy::MemRegUbSegment(logic_dev_id, base, size));
  segment = std::make_shared<Segment>();
  segment->device = logic_dev_id;
  segment->base = base;
  segment->size = size;
  segments_[seg_key] = segment;
  return SUCCESS;
}
Status NativeVerbs::Register(const std::shared_ptr<HostRoceDevice> &device, uint64_t base, uint64_t size,
                             bool device_memory, uint32_t memory_device, std::shared_ptr<HostRoceMr> &out) {
  HIXL_CHECK_NOTNULL(device);
  auto mr = std::make_shared<Memory>();
  mr->device = std::static_pointer_cast<Device>(device);
  if (device_memory) {
    (void)aclrtGetCurrentContext(&mr->context);
  }
  if (device_memory && device->resolve_device_memory && memory_device != UINT32_MAX) {
    mr->memory_device = memory_device;
    HIXL_CHK_STATUS_RET(SegmentFor(memory_device, base, size, mr->segment));
  }
  errno = 0;
  mr->mr = api_->reg_mr(
      mr->device->pd, reinterpret_cast<void *>(base), size,
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC);
  const int reg_errno = errno;
  if (mr->mr == nullptr) {
    if (mr->segment != nullptr) {
      const auto seg_key = std::make_tuple(mr->segment->device, mr->segment->base, mr->segment->size);
      mr->segment.reset();
      std::lock_guard<std::mutex> lock(mutex_);
      const auto seg_it = segments_.find(seg_key);
      if (seg_it != segments_.end() && seg_it->second.expired()) {
        segments_.erase(seg_it);
      }
    }
    HIXL_LOGE(FAILED, "[HostRoce] ibv_reg_mr failed, size=%" PRIu64 " bytes, errno=%d", size, reg_errno);
    return FAILED;
  }
  mr->base = base;
  mr->size = size;
  mr->lkey = mr->mr->lkey;
  mr->rkey = mr->mr->rkey;
  out = std::move(mr);
  return SUCCESS;
}
Status NativeVerbs::Deregister(HostRoceMr &mr) {
  auto &memory = static_cast<Memory &>(mr);
  if (memory.mr != nullptr) {
    HIXL_CHK_STATUS_RET(VerbsError(memory.device->api->dereg_mr(memory.mr), "ibv_dereg_mr"));
    memory.mr = nullptr;
  }
  if (memory.segment != nullptr) {
    const auto key = std::make_tuple(memory.segment->device, memory.segment->base, memory.segment->size);
    memory.segment.reset();
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = segments_.find(key);
    if (it != segments_.end() && it->second.expired()) {
      segments_.erase(it);
    }
  }
  return SUCCESS;
}
Status NativeVerbs::CreateQp(const std::shared_ptr<HostRoceDevice> &device, const HostRoceConfig &config,
                             std::unique_ptr<HostRoceQp> &out) {
  HIXL_CHECK_NOTNULL(device);
  auto q = std::make_unique<Qp>();
  q->device = std::static_pointer_cast<Device>(device);
  const uint32_t depth = std::min({config.queue_depth, device->max_wr, device->max_cqe});
  HIXL_CHK_BOOL_RET_STATUS(depth > 0, PARAM_INVALID, "[HostRoce] Invalid QP queue depth=%u", depth);
  errno = 0;
  q->cq = api_->create_cq(q->device->context, static_cast<int>(depth), nullptr, nullptr, 0);
  const int cq_errno = errno;
  HIXL_CHK_BOOL_RET_STATUS(q->cq != nullptr, FAILED, "[HostRoce] ibv_create_cq failed, depth=%u, errno=%d", depth,
                           cq_errno);
  ibv_qp_init_attr init{};
  init.send_cq = q->cq;
  init.recv_cq = q->cq;
  init.qp_type = IBV_QPT_RC;
  init.cap.max_send_wr = depth;
  init.cap.max_send_sge = 1;
  init.cap.max_recv_wr = 1;
  init.cap.max_recv_sge = 1;
  errno = 0;
  q->qp = api_->create_qp(q->device->pd, &init);
  const int qp_errno = errno;
  auto &qp = *q;
  out = std::move(q);
  HIXL_CHK_BOOL_RET_STATUS(qp.qp != nullptr, FAILED, "[HostRoce] ibv_create_qp failed, depth=%u, errno=%d", depth,
                           qp_errno);
  qp.capacity = std::min(depth, init.cap.max_send_wr);
  qp.local = device->local;
  qp.local.qpn = qp.qp->qp_num;
  HIXL_EVENT(
      "[HostRoce] Created QP, nic=%s, port=%u, qpn=%u, capacity=%u "
      "(requested_depth=%u, clamped_depth=%u, hw_max_send_wr=%u, cq_cqe=%d, dev_max_wr=%u, dev_max_cqe=%u), "
      "tc=%u, sl=%u, retry_cnt=%u, timeout=%u",
      SafeDeviceName(api_->name(qp.device->context->device)), static_cast<uint32_t>(qp.device->port), qp.local.qpn,
      qp.capacity, config.queue_depth, depth, init.cap.max_send_wr, qp.cq->cqe, device->max_wr, device->max_cqe,
      config.tc, config.sl, config.retry_count, config.timeout);
  return InitializeQp(qp, config);
}
Status NativeVerbs::InitializeQp(Qp &qp, const HostRoceConfig &config) const {
  uint32_t psn = 0;
  ssize_t random_bytes = -1;
  do {
    random_bytes = getrandom(&psn, sizeof(psn), 0);
  } while (random_bytes < 0 && errno == EINTR);
  HIXL_CHK_BOOL_RET_STATUS(random_bytes == static_cast<ssize_t>(sizeof(psn)), FAILED,
                           "[HostRoce] getrandom PSN failed");
  qp.local.psn = psn & kPsnMask;
  ibv_qp_attr attr{};
  attr.qp_state = IBV_QPS_INIT;
  attr.port_num = qp.device->port;
  attr.pkey_index = 0;
  attr.qp_access_flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
  HIXL_CHK_STATUS_RET(
      VerbsError(api_->modify_qp(qp.qp, &attr, IBV_QP_STATE | IBV_QP_PORT | IBV_QP_PKEY_INDEX | IBV_QP_ACCESS_FLAGS),
                 "ibv_modify_qp INIT"));
  if (qp.device->extended != nullptr) {
    HostRoceHyperFeature feature{};
    const int feat_ret = api_->query_feature(qp.device->extended, qp.qp, config.sl, config.tc, &feature);
    if (feat_ret == EOPNOTSUPP || feat_ret == -EOPNOTSUPP) {
      HIXL_LOGI("[HostRoce] Extend feature query unsupported by driver, qpn=%u, ret=%d", qp.local.qpn, feat_ret);
    } else {
      HIXL_CHK_STATUS_RET(VerbsError(feat_ret, "ibv_query_qp_supported_hyroce_feature"));
      static_assert(sizeof(feature) == sizeof(qp.local.feature), "Feature wire size");
      const errno_t rc = memcpy_s(qp.local.feature.data(), qp.local.feature.size(), &feature, sizeof(feature));
      HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "[HostRoce] Call api:memcpy_s failed, ret=%d, size=%zu bytes",
                               static_cast<int32_t>(rc), sizeof(feature));
      qp.local.has_feature = true;
      HIXL_LOGI("[HostRoce] Extend feature queried, qpn=%u, tc=%u, sl=%u, type=%u, version=%u, sack=%u", qp.local.qpn,
                config.tc, config.sl, static_cast<uint32_t>(feature.type), static_cast<uint32_t>(feature.version),
                static_cast<uint32_t>(feature.sack_enable));
    }
  } else {
    HIXL_LOGI("[HostRoce] Extend feature query skipped, qpn=%u, reason=no_extend_context", qp.local.qpn);
  }
  return SUCCESS;
}
Status NativeVerbs::Negotiate(HostRoceQp &qp, const HostRocePeer &peer, HostRocePeer &local) {
  auto &q = static_cast<Qp &>(qp);
  local = q.local;
  if (!local.has_feature || !peer.has_feature) {
    HIXL_LOGI("[HostRoce] Extend feature negotiation skipped, qpn=%u, local_has_feature=%d, peer_has_feature=%d",
              q.local.qpn, static_cast<int>(local.has_feature), static_cast<int>(peer.has_feature));
    local.has_feature = false;
    local.feature.fill(0);
    local.more = false;
    q.local = local;
    return SUCCESS;
  }
  HostRoceHyperFeature input{};
  HostRoceHyperFeature output{};
  uint32_t more = 0;
  const errno_t in_rc = memcpy_s(&input, sizeof(input), peer.feature.data(), peer.feature.size());
  HIXL_CHK_BOOL_RET_STATUS(in_rc == EOK, FAILED, "[HostRoce] Call api:memcpy_s failed, ret=%d, size=%zu bytes",
                           static_cast<int32_t>(in_rc), sizeof(input));
  HIXL_CHK_STATUS_RET(
      VerbsError(api_->negotiate(q.device->extended, q.qp, &input, &output, &more), "ibv_nego_qp_hyroce_feature"));
  const errno_t out_rc = memcpy_s(local.feature.data(), local.feature.size(), &output, sizeof(output));
  HIXL_CHK_BOOL_RET_STATUS(out_rc == EOK, FAILED, "[HostRoce] Call api:memcpy_s failed, ret=%d, size=%zu bytes",
                           static_cast<int32_t>(out_rc), sizeof(output));
  local.more = more != 0;
  q.local = local;
  HIXL_LOGI(
      "[HostRoce] Extend feature negotiated, qpn=%u, input_type=%u, input_version=%u, input_sack=%u, "
      "output_type=%u, output_version=%u, output_sack=%u, need_more=%u",
      q.local.qpn, static_cast<uint32_t>(input.type), static_cast<uint32_t>(input.version),
      static_cast<uint32_t>(input.sack_enable), static_cast<uint32_t>(output.type),
      static_cast<uint32_t>(output.version), static_cast<uint32_t>(output.sack_enable), more);
  return SUCCESS;
}
Status NativeVerbs::RefreshGid(const Qp &qp, const ibv_port_attr &port, uint8_t &index) const {
  const char *nic_name = SafeDeviceName(api_->name(qp.device->context->device));
  for (int i = 0; i < port.gid_tbl_len && i <= UINT8_MAX; ++i) {
    ibv_gid gid{};
    if (api_->query_gid(qp.device->context, qp.device->port, i, &gid) != 0 ||
        std::memcmp(gid.raw, qp.local.gid.data(), sizeof(gid.raw)) != 0) {
      continue;
    }
    if (!IsRoceV2Gid(nic_name, qp.device->port, i)) {
      continue;
    }
    index = static_cast<uint8_t>(i);
    return SUCCESS;
  }
  HIXL_LOGE(NOT_CONNECTED, "[HostRoce] Local RoCE v2 GID disappeared before RTR, qpn=%u, port=%u", qp.local.qpn,
            qp.device->port);
  return NOT_CONNECTED;
}
Status NativeVerbs::ToRtr(const Qp &qp, const HostRocePeer &peer, const HostRoceConfig &config, uint32_t &path_mtu,
                          uint8_t &sgid_index) const {
  ibv_qp_attr attr{};
  attr.qp_state = IBV_QPS_RTR;
  attr.dest_qp_num = peer.qpn;
  attr.rq_psn = peer.psn;
  ibv_port_attr port{};
  HIXL_CHK_STATUS_RET(VerbsError(
      api_->query_port(qp.device->context, qp.device->port, reinterpret_cast<_compat_ibv_port_attr *>(&port)),
      "ibv_query_port RTR"));
  path_mtu = std::min<uint32_t>(qp.local.mtu, peer.mtu);
  HIXL_CHK_BOOL_RET_STATUS(port.active_mtu >= IBV_MTU_1024 && path_mtu >= static_cast<uint32_t>(IBV_MTU_1024) &&
                               static_cast<uint32_t>(port.active_mtu) >= path_mtu,
                           UNSUPPORTED, "[HostRoce] Invalid path MTU, active_mtu=%u, local_mtu=%u, peer_mtu=%u",
                           static_cast<uint32_t>(port.active_mtu), qp.local.mtu, peer.mtu);
  attr.path_mtu = static_cast<ibv_mtu>(path_mtu);
  attr.max_dest_rd_atomic = static_cast<uint8_t>(std::min(qp.local.read_depth, peer.read_depth));
  attr.min_rnr_timer = kMinRnrTimer;
  attr.ah_attr.is_global = 1;
  attr.ah_attr.port_num = qp.device->port;
  attr.ah_attr.sl = static_cast<uint8_t>(config.sl);
  HIXL_CHK_STATUS_RET(RefreshGid(qp, port, attr.ah_attr.grh.sgid_index));
  sgid_index = attr.ah_attr.grh.sgid_index;
  attr.ah_attr.grh.hop_limit = kDefaultGrhHopLimit;
  attr.ah_attr.grh.traffic_class = static_cast<uint8_t>(config.tc);
  std::copy(peer.gid.begin(), peer.gid.end(), attr.ah_attr.grh.dgid.raw);
  return VerbsError(api_->modify_qp(qp.qp, &attr,
                                    IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                                        IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER),
                    "ibv_modify_qp RTR");
}
Status NativeVerbs::ApplyQpFeature(const Qp &qp) const {
  if (qp.local.has_feature) {
    HostRoceExtendedQpAttr ext{};
    ext.qp = qp.qp;
    const errno_t rc = memcpy_s(&ext.feature, sizeof(ext.feature), qp.local.feature.data(), qp.local.feature.size());
    HIXL_CHK_BOOL_RET_STATUS(rc == EOK, FAILED, "[HostRoce] Call api:memcpy_s failed, ret=%d, size=%zu bytes",
                             static_cast<int32_t>(rc), sizeof(ext.feature));
    const int ret = api_->modify_extend(qp.device->extended, &ext, kExtendedQpFeatureMask);
    HIXL_CHK_STATUS_RET(VerbsError(ret, "ibv_modify_qp_extend feature"));
    HIXL_LOGI("[HostRoce] Extend feature applied, qpn=%u, mask=0x%x, type=%u, version=%u, sack=%u", qp.local.qpn,
              static_cast<uint32_t>(kExtendedQpFeatureMask), static_cast<uint32_t>(ext.feature.type),
              static_cast<uint32_t>(ext.feature.version), static_cast<uint32_t>(ext.feature.sack_enable));
  } else {
    HIXL_LOGI("[HostRoce] Extend feature apply skipped, qpn=%u, reason=no_negotiated_feature", qp.local.qpn);
  }
  return SUCCESS;
}
Status NativeVerbs::Connect(HostRoceQp &qp, const HostRocePeer &peer, const HostRoceConfig &config) {
  auto &q = static_cast<Qp &>(qp);
  HIXL_CHK_STATUS_RET(ApplyQpFeature(q));
  uint32_t path_mtu = 0U;
  uint8_t sgid_index = 0U;
  HIXL_CHK_STATUS_RET(ToRtr(q, peer, config, path_mtu, sgid_index));
  ibv_qp_attr attr{};
  attr.qp_state = IBV_QPS_RTS;
  attr.sq_psn = q.local.psn;
  attr.timeout = static_cast<uint8_t>(config.timeout);
  attr.retry_cnt = static_cast<uint8_t>(config.retry_count);
  attr.rnr_retry = kMaxRnrRetry;
  attr.max_rd_atomic = static_cast<uint8_t>(std::min(q.local.read_depth, peer.read_depth));
  HIXL_CHK_STATUS_RET(VerbsError(api_->modify_qp(q.qp, &attr,
                                                 IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                                                     IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC),
                                 "ibv_modify_qp RTS"));
  if (q.local.plane_count != 0) {
    HIXL_CHK_BOOL_RET_STATUS(q.local.plane < q.local.plane_count && api_->set_plane != nullptr, PARAM_INVALID,
                             "[HostRoce] Invalid HRN plane=%u, plane_count=%u", q.local.plane, q.local.plane_count);
    HIXL_CHK_STATUS_RET(VerbsError(api_->set_plane(q.qp, static_cast<int>(q.local.plane)), "roce_set_qp_lb_value"));
    if (api_->get_plane != nullptr) {
      int plane = -1;
      HIXL_CHK_STATUS_RET(VerbsError(api_->get_plane(q.qp, &plane), "roce_get_qp_lb_value"));
      HIXL_CHK_BOOL_RET_STATUS(plane == static_cast<int>(q.local.plane), FAILED,
                               "[HostRoce] Plane readback mismatch, requested=%u, actual=%d", q.local.plane, plane);
    }
  }
  HIXL_EVENT(
      "[HostRoce] QP ready (RTS), qpn=%u, dest_qpn=%u, capacity=%u, path_mtu=%u, sgid_index=%u, max_rd_atomic=%u, "
      "tc=%u, sl=%u, retry_cnt=%u, timeout=%u, rnr_retry=%u, plane=%u/%u, extend_feature=%d",
      q.local.qpn, peer.qpn, q.capacity, path_mtu, static_cast<uint32_t>(sgid_index),
      static_cast<uint32_t>(attr.max_rd_atomic), config.tc, config.sl, config.retry_count, config.timeout,
      static_cast<uint32_t>(attr.rnr_retry), q.local.plane, q.local.plane_count, static_cast<int>(q.local.has_feature));
  return SUCCESS;
}
Status NativeVerbs::PostBatch(HostRoceQp &qp, bool read, const std::vector<HostRoceWorkRequest> &wrs, size_t &posted) {
  posted = 0;
  HIXL_CHK_BOOL_RET_STATUS(!wrs.empty() && wrs.size() <= qp.capacity, PARAM_INVALID,
                           "[HostRoce] Invalid post batch size=%zu, capacity=%u", wrs.size(), qp.capacity);
  auto &q = static_cast<Qp &>(qp);
  auto &sges = q.sge_scratch;
  auto &send_wrs = q.wr_scratch;
  sges.resize(wrs.size());
  send_wrs.resize(wrs.size());
  for (size_t i = 0; i < wrs.size(); ++i) {
    const auto &request = wrs[i];
    auto &sge = sges[i];
    sge = ibv_sge{};
    sge.addr = request.local;
    sge.length = request.size;
    sge.lkey = request.lkey;
    auto &wr = send_wrs[i];
    wr = ibv_send_wr{};
    wr.wr_id = request.id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.opcode = read ? IBV_WR_RDMA_READ : IBV_WR_RDMA_WRITE;
    if (request.signaled) {
      wr.send_flags |= IBV_SEND_SIGNALED;
    }
    if (request.fence) {
      wr.send_flags |= IBV_SEND_FENCE;
    }
    wr.wr.rdma.remote_addr = request.remote;
    wr.wr.rdma.rkey = request.rkey;
    wr.next = i + 1 < send_wrs.size() ? &send_wrs[i + 1] : nullptr;
  }
  ibv_send_wr *bad = nullptr;
  const auto ret = ibv_post_send(q.qp, send_wrs.data(), &bad);
  if (ret == 0) {
    posted = send_wrs.size();
    return SUCCESS;
  }
  const auto failed =
      std::find_if(send_wrs.begin(), send_wrs.end(), [bad](const ibv_send_wr &wr) { return &wr == bad; });
  HIXL_CHK_BOOL_RET_STATUS(failed != send_wrs.end(), FAILED,
                           "[HostRoce] ibv_post_send returned an invalid bad_wr, ret=%d, count=%zu", ret,
                           send_wrs.size());
  posted = static_cast<size_t>(std::distance(send_wrs.begin(), failed));
  if (ret == ENOMEM || ret == EAGAIN) {
    return RESOURCE_EXHAUSTED;
  }
  return VerbsError(ret, "ibv_post_send batch");
}
Status NativeVerbs::Poll(HostRoceQp &qp, std::vector<HostRoceCompletion> &out) {
  out.clear();
  auto &q = static_cast<Qp &>(qp);
  ibv_wc wc[kPollCqBatchSize]{};
  const auto count = ibv_poll_cq(q.cq, kPollCqBatchSize, wc);
  HIXL_CHK_BOOL_RET_STATUS(count >= 0 && count <= kPollCqBatchSize, FAILED, "[HostRoce] ibv_poll_cq failed, ret=%d",
                           count);
  for (int i = 0; i < count; ++i) {
    out.push_back({wc[i].wr_id, static_cast<uint32_t>(wc[i].status), wc[i].vendor_err});
  }
  return SUCCESS;
}
Status NativeVerbs::AllocateFlushBuffers(HostRoceFlushResources &resources, uint32_t memory_device) {
  HIXL_CHK_STATUS_RET(ResolveHostRoceFlushContext(memory_device, resources.context, resources.owns_context));
  const auto policy = static_cast<aclrtMemMallocPolicy>(ACL_MEM_TYPE_HIGH_BAND_WIDTH);
  HIXL_CHK_ACL_RET(aclrtMalloc(&resources.remote, kLocalFlushBytes, policy),
                   "[HostRoce] Allocate flush HBM failed, size=%zu bytes", kLocalFlushBytes);
  if (resources.vendor_opcode) {
    HIXL_CHK_ACL_RET(aclrtMalloc(&resources.local, kLocalFlushBytes, policy),
                     "[HostRoce] Allocate flush destination HBM failed, size=%zu bytes", kLocalFlushBytes);
  } else {
    resources.local = std::malloc(kLocalFlushBytes);
    HIXL_CHK_BOOL_RET_STATUS(resources.local != nullptr, FAILED,
                             "[HostRoce] Allocate flush host buffer failed, size=%zu bytes", kLocalFlushBytes);
  }
  return SUCCESS;
}
Status NativeVerbs::RegisterFlushBuffer(const std::shared_ptr<HostRoceDevice> &device, void *address, bool hbm,
                                        uint32_t memory_device, std::shared_ptr<HostRoceMr> &mr) {
  return Register(device, reinterpret_cast<uint64_t>(address), kLocalFlushBytes, hbm, hbm ? memory_device : UINT32_MAX,
                  mr);
}
Status NativeVerbs::ConnectFlushPair(const std::shared_ptr<HostRoceDevice> &device,
                                     const HostRoceConfig &channel_config, HostRoceFlushResources &resources) {
  HostRoceConfig config = channel_config;
  config.queue_depth = 1;
  HIXL_CHK_STATUS_RET(CreateQp(device, config, resources.tx));
  HIXL_CHK_STATUS_RET(CreateQp(device, config, resources.rx));
  for (auto *q : {resources.tx.get(), resources.rx.get()}) {
    // HCCP's local pair offers the zero RoCEv2 feature and does not select a channel plane.
    q->local.plane_count = 0;
    q->local.plane = kHostRoceNoPlane;
    HostRocePeer plain;
    plain.has_feature = q->local.has_feature;
    HostRocePeer local;
    const Status nego_ret = Negotiate(*q, plain, local);
    if (nego_ret != SUCCESS || local.more) {
      HIXL_LOGW("[HostRoce] Local flush feature negotiation skipped, qpn=%u, ret=%u, more=%d", q->local.qpn,
                static_cast<uint32_t>(nego_ret), static_cast<int>(local.more));
      q->local.has_feature = false;
      q->local.feature.fill(0);
      q->local.more = false;
    }
  }
  HIXL_CHK_STATUS_RET(Connect(*resources.tx, resources.rx->local, config));
  return Connect(*resources.rx, resources.tx->local, config);
}
Status NativeVerbs::CreateFlush(const std::shared_ptr<HostRoceDevice> &device,
                                const std::shared_ptr<HostRoceMr> &target, const HostRoceConfig &config,
                                std::unique_ptr<HostRoceFlush> &flush) {
  HIXL_CHECK_NOTNULL(device);
  HIXL_CHECK_NOTNULL(target);
  // A GET caller may run on a different host thread from registration. Reuse the MR owner's ACL context.
  TemporaryRtContext guard(static_cast<Memory &>(*target).context);
  auto created = std::make_unique<HostRoceNativeFlush>(this);
  auto &r = *created->resources;
  flush = std::move(created);
  // HCOMM uses this same HRN capability distinction for its private 0x40 operation.
  r.vendor_opcode = device->local.plane_count > 0;
  HIXL_CHK_STATUS_RET(AllocateFlushBuffers(r, target->memory_device));
  HIXL_CHK_STATUS_RET(RegisterFlushBuffer(device, r.local, r.vendor_opcode, target->memory_device, r.local_mr));
  HIXL_CHK_STATUS_RET(RegisterFlushBuffer(device, r.remote, true, target->memory_device, r.remote_mr));
  HIXL_CHK_STATUS_RET(ConnectFlushPair(device, config, r));
  HIXL_LOGI("[HostRoce] Created local flush QP pair, memory_device=%u, vendor_opcode=%d, tc=%u, sl=%u",
            target->memory_device, static_cast<int>(r.vendor_opcode), config.tc, config.sl);
  return SUCCESS;
}
Status NativeVerbs::PostFlush(HostRoceFlush &flush) {
  auto *resources = static_cast<HostRoceNativeFlush &>(flush).resources.get();
  HIXL_CHK_BOOL_RET_STATUS(resources != nullptr && resources->tx != nullptr && resources->local_mr != nullptr &&
                               resources->remote_mr != nullptr,
                           FAILED, "[HostRoce] Flush resources are not initialized");
  auto &r = *resources;
  auto &q = static_cast<Qp &>(*r.tx);
  ibv_sge sge{};
  sge.addr = reinterpret_cast<uint64_t>(r.local);
  sge.length = kLocalFlushBytes;
  sge.lkey = r.local_mr->lkey;
  ibv_send_wr wr{};
  wr.wr_id = kLocalFlushWrId;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = r.vendor_opcode ? static_cast<ibv_wr_opcode>(kVendorFlushOpcode) : IBV_WR_RDMA_READ;
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.wr.rdma.remote_addr = reinterpret_cast<uint64_t>(r.remote);
  wr.wr.rdma.rkey = r.remote_mr->rkey;
  ibv_send_wr *bad = nullptr;
  return VerbsError(ibv_post_send(q.qp, &wr, &bad), "ibv_post_send local flush");
}
Status NativeVerbs::PollFlush(HostRoceFlush &flush, bool &done) {
  done = false;
  auto *resources = static_cast<HostRoceNativeFlush &>(flush).resources.get();
  HIXL_CHK_BOOL_RET_STATUS(resources != nullptr && resources->tx != nullptr, FAILED,
                           "[HostRoce] Flush resources are not initialized");
  auto &r = *resources;
  auto &q = static_cast<Qp &>(*r.tx);
  ibv_wc wc{};
  const auto count = ibv_poll_cq(q.cq, 1, &wc);
  HIXL_CHK_BOOL_RET_STATUS(count >= 0, FAILED, "[HostRoce] ibv_poll_cq local flush failed, ret=%d", count);
  if (count == 0) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(wc.wr_id == kLocalFlushWrId && wc.status == IBV_WC_SUCCESS, FAILED,
                           "[HostRoce] Local flush completion failed, wr_id=%" PRIu64 ", status=%u, vendor_err=%u",
                           wc.wr_id, static_cast<uint32_t>(wc.status), wc.vendor_err);
  done = true;
  return SUCCESS;
}
Status NativeVerbs::Stop(HostRoceQp &qp) {
  return StopNativeQp(static_cast<Qp &>(qp));
}
#else
class NativeVerbs final : public HostRoceVerbs {
 public:
  Status Open(const std::string &, std::shared_ptr<HostRoceDevice> &) override {
    HIXL_LOGE(UNSUPPORTED, "[HostRoce] Native Host RoCE is only supported on Linux");
    return UNSUPPORTED;
  }
  Status Register(const std::shared_ptr<HostRoceDevice> &, uint64_t, uint64_t, bool, uint32_t,
                  std::shared_ptr<HostRoceMr> &) override {
    return UNSUPPORTED;
  }
  Status Deregister(HostRoceMr &) override {
    return UNSUPPORTED;
  }
  Status CreateQp(const std::shared_ptr<HostRoceDevice> &, const HostRoceConfig &,
                  std::unique_ptr<HostRoceQp> &) override {
    return UNSUPPORTED;
  }
  Status Negotiate(HostRoceQp &, const HostRocePeer &, HostRocePeer &) override {
    return UNSUPPORTED;
  }
  Status Connect(HostRoceQp &, const HostRocePeer &, const HostRoceConfig &) override {
    return UNSUPPORTED;
  }
  Status PostBatch(HostRoceQp &, bool, const std::vector<HostRoceWorkRequest> &, size_t &posted) override {
    posted = 0;
    return UNSUPPORTED;
  }
  Status Poll(HostRoceQp &, std::vector<HostRoceCompletion> &out) override {
    out.clear();
    return UNSUPPORTED;
  }
  Status CreateFlush(const std::shared_ptr<HostRoceDevice> &, const std::shared_ptr<HostRoceMr> &,
                     const HostRoceConfig &, std::unique_ptr<HostRoceFlush> &) override {
    return UNSUPPORTED;
  }
  Status PostFlush(HostRoceFlush &) override {
    return UNSUPPORTED;
  }
  Status PollFlush(HostRoceFlush &, bool &done) override {
    done = false;
    return UNSUPPORTED;
  }
  Status Stop(HostRoceQp &) override {
    return SUCCESS;
  }
};
#endif
#ifdef HIXL_ENABLE_TEST_HOOKS
std::mutex g_verbs_mutex;
std::shared_ptr<HostRoceVerbs> g_test_verbs;
#endif
}  // namespace
std::shared_ptr<HostRoceVerbs> GetHostRoceVerbs() {
#ifdef HIXL_ENABLE_TEST_HOOKS
  std::lock_guard<std::mutex> lock(g_verbs_mutex);
  if (g_test_verbs != nullptr) {
    return g_test_verbs;
  }
#endif
  static auto native = std::make_shared<NativeVerbs>();
  return native;
}
#ifdef HIXL_ENABLE_TEST_HOOKS
void SetHostRoceVerbsForTest(std::shared_ptr<HostRoceVerbs> verbs) {
  std::lock_guard<std::mutex> lock(g_verbs_mutex);
  g_test_verbs = std::move(verbs);
}
#endif
}  // namespace hixl
