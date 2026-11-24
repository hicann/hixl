/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/host_roce/host_roce_verbs_loader.h"

#if defined(__linux__)
#include <dlfcn.h>
#include "common/hixl_checker.h"
#include "common/hixl_log.h"

namespace hixl {
namespace {
constexpr uint32_t kHuaweiVendorId = 0x19e5U;
constexpr uint32_t kHrnAltVendorId = 0xcc08U;
constexpr uint32_t kHuaweiHrnPartIdMin = 0x230U;
constexpr uint32_t kHuaweiHrnPartIdMax = 0x232U;
constexpr uint32_t kHrnAltPartIdA = 0x8200U;
constexpr uint32_t kHrnAltPartIdB = 0x8201U;
constexpr uint32_t kIbvExtendMajor = 4U;
constexpr uint32_t kIbvExtendMinMinor = 3U;

void LogExtendedVersion(const void *version_api, uint32_t major, uint32_t minor, uint32_t patch) {
  Dl_info info{};
  const int found = dladdr(version_api, &info);
  HIXL_LOGI("[HostRoce] Extend library, path=%s, version=%u.%u.%u",
            found != 0 && info.dli_fname != nullptr ? info.dli_fname : "unknown", major, minor, patch);
}

template <typename T>
bool BindSymbol(void *library, T &fn, const char *symbol_name) {
  fn = reinterpret_cast<T>(dlsym(library, symbol_name));
  return fn != nullptr;
}
}  // namespace

HostRoceVerbsLibrary::~HostRoceVerbsLibrary() {
  if (extend != nullptr) {
    (void)dlclose(extend);
  }
  if (hrn != nullptr) {
    (void)dlclose(hrn);
  }
  if (library != nullptr) {
    (void)dlclose(library);
  }
}

Status HostRoceVerbsLibrary::Initialize() {
  library = dlopen("libibverbs.so.1", RTLD_NOW | RTLD_LOCAL);
  HIXL_CHK_BOOL_RET_STATUS(library != nullptr, UNSUPPORTED, "[HostRoce] dlopen libibverbs.so.1 failed");
  const bool bound =
      BindSymbol(library, devices, "ibv_get_device_list") &&
      BindSymbol(library, free_devices, "ibv_free_device_list") && BindSymbol(library, name, "ibv_get_device_name") &&
      BindSymbol(library, open, "ibv_open_device") && BindSymbol(library, close, "ibv_close_device") &&
      BindSymbol(library, query_device, "ibv_query_device") && BindSymbol(library, query_port, "ibv_query_port") &&
      BindSymbol(library, query_gid, "ibv_query_gid") && BindSymbol(library, alloc_pd, "ibv_alloc_pd") &&
      BindSymbol(library, free_pd, "ibv_dealloc_pd") && BindSymbol(library, reg_mr, "ibv_reg_mr") &&
      BindSymbol(library, dereg_mr, "ibv_dereg_mr") && BindSymbol(library, create_cq, "ibv_create_cq") &&
      BindSymbol(library, destroy_cq, "ibv_destroy_cq") && BindSymbol(library, create_qp, "ibv_create_qp") &&
      BindSymbol(library, destroy_qp, "ibv_destroy_qp") && BindSymbol(library, modify_qp, "ibv_modify_qp");
  HIXL_CHK_BOOL_RET_STATUS(bound, UNSUPPORTED, "[HostRoce] dlsym required libibverbs APIs failed");
  Dl_info info{};
  const auto located = dladdr(reinterpret_cast<void *>(query_port), &info);
  HIXL_LOGI("[HostRoce] Loaded verbs library, query_port_path=%s, port_attr_bytes=%zu, device_attr_bytes=%zu",
            located != 0 && info.dli_fname != nullptr ? info.dli_fname : "unknown", sizeof(ibv_port_attr),
            sizeof(ibv_device_attr));
  return SUCCESS;
}

void HostRoceVerbsLibrary::LoadHrn() {
  if (hrn_loaded) {
    return;
  }
  hrn_loaded = true;
  hrn = dlopen("libhrn5-rdmav34.so", RTLD_NOW | RTLD_LOCAL);
  if (hrn == nullptr) {
    hrn = dlopen("libhrn5.so.1", RTLD_NOW | RTLD_LOCAL);
  }
  if (hrn != nullptr) {
    plane_count = reinterpret_cast<decltype(plane_count)>(dlsym(hrn, "roce_get_qp_num"));
    set_plane = reinterpret_cast<decltype(set_plane)>(dlsym(hrn, "roce_set_qp_lb_value"));
    get_plane = reinterpret_cast<decltype(get_plane)>(dlsym(hrn, "roce_get_qp_lb_value"));
  }
}

void HostRoceVerbsLibrary::LoadExtend() {
  if (extend_loaded) {
    return;
  }
  extend_loaded = true;
  extend = dlopen("libibv_extend.so", RTLD_NOW | RTLD_LOCAL);
  if (extend == nullptr) {
    const char *error = dlerror();
    HIXL_LOGI("[HostRoce] Extend disabled, reason=library_load_failed, detail=%s",
              error != nullptr ? error : "unknown");
    return;
  }
  auto close_extend_lib = [this]() {
    open_extend = nullptr;
    close_extend = nullptr;
    query_feature = nullptr;
    negotiate = nullptr;
    modify_extend = nullptr;
    (void)dlclose(extend);
    extend = nullptr;
  };
  auto version =
      reinterpret_cast<const char *(*)(uint32_t *, uint32_t *, uint32_t *)>(dlsym(extend, "ibv_extend_get_version"));
  if (version == nullptr) {
    HIXL_LOGI("[HostRoce] Extend disabled, reason=missing_ibv_extend_get_version");
    close_extend_lib();
    return;
  }
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t patch = 0;
  (void)version(&major, &minor, &patch);
  LogExtendedVersion(reinterpret_cast<void *>(version), major, minor, patch);
  if (major != kIbvExtendMajor || minor < kIbvExtendMinMinor) {
    HIXL_LOGI("[HostRoce] Extend disabled, reason=incompatible_version, required_major=%u, minimum_minor=%u",
              static_cast<uint32_t>(kIbvExtendMajor), static_cast<uint32_t>(kIbvExtendMinMinor));
    close_extend_lib();
    return;
  }
  open_extend = reinterpret_cast<decltype(open_extend)>(dlsym(extend, "ibv_open_extend"));
  close_extend = reinterpret_cast<decltype(close_extend)>(dlsym(extend, "ibv_close_extend"));
  query_feature = reinterpret_cast<decltype(query_feature)>(dlsym(extend, "ibv_query_qp_supported_hyroce_feature"));
  negotiate = reinterpret_cast<decltype(negotiate)>(dlsym(extend, "ibv_nego_qp_hyroce_feature"));
  modify_extend = reinterpret_cast<decltype(modify_extend)>(dlsym(extend, "ibv_modify_qp_extend"));
  if (open_extend == nullptr || close_extend == nullptr || query_feature == nullptr || negotiate == nullptr ||
      modify_extend == nullptr) {
    HIXL_LOGI(
        "[HostRoce] Extend disabled, reason=incomplete_apis, open=%d, close=%d, query=%d, negotiate=%d, modify=%d",
        static_cast<int>(open_extend != nullptr), static_cast<int>(close_extend != nullptr),
        static_cast<int>(query_feature != nullptr), static_cast<int>(negotiate != nullptr),
        static_cast<int>(modify_extend != nullptr));
    close_extend_lib();
  }
}

bool HostRoceVerbsLibrary::IsHrnVendor(const ibv_device_attr &attr) {
  return attr.vendor_id == kHuaweiVendorId || attr.vendor_id == kHrnAltVendorId;
}

bool HostRoceVerbsLibrary::RequiresHrnPlanes(const ibv_device_attr &attr) {
  return (attr.vendor_id == kHuaweiVendorId && attr.vendor_part_id >= kHuaweiHrnPartIdMin &&
          attr.vendor_part_id <= kHuaweiHrnPartIdMax) ||
         (attr.vendor_id == kHrnAltVendorId &&
          (attr.vendor_part_id == kHrnAltPartIdA || attr.vendor_part_id == kHrnAltPartIdB));
}

}  // namespace hixl
#endif
