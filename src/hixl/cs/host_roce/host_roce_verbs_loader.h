/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_VERBS_LOADER_H_
#define CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_VERBS_LOADER_H_

#if defined(__linux__)
#include <cstddef>
#include <cstdint>
#include <infiniband/verbs.h>
#include "cs/host_roce/ibv_extend_compat.h"
#include "hixl/hixl_types.h"

namespace hixl {

struct HostRoceVerbsLibrary {
  HostRoceVerbsLibrary() = default;
  HostRoceVerbsLibrary(const HostRoceVerbsLibrary &) = delete;
  HostRoceVerbsLibrary &operator=(const HostRoceVerbsLibrary &) = delete;
  HostRoceVerbsLibrary(HostRoceVerbsLibrary &&) = delete;
  HostRoceVerbsLibrary &operator=(HostRoceVerbsLibrary &&) = delete;
  ~HostRoceVerbsLibrary();

  Status Initialize();
  void LoadHrn();
  void LoadExtend();
  static bool IsHrnVendor(const ibv_device_attr &attr);
  static bool RequiresHrnPlanes(const ibv_device_attr &attr);

  void *library{nullptr};
  void *hrn{nullptr};
  void *extend{nullptr};
  bool hrn_loaded{false};
  bool extend_loaded{false};

  decltype(&ibv_get_device_list) devices{nullptr};
  decltype(&ibv_free_device_list) free_devices{nullptr};
  decltype(&ibv_get_device_name) name{nullptr};
  decltype(&ibv_open_device) open{nullptr};
  decltype(&ibv_close_device) close{nullptr};
  decltype(&ibv_query_device) query_device{nullptr};
  decltype(&ibv_query_port) query_port{nullptr};
  decltype(&ibv_query_gid) query_gid{nullptr};
  decltype(&ibv_alloc_pd) alloc_pd{nullptr};
  decltype(&ibv_dealloc_pd) free_pd{nullptr};
  ibv_mr *(*reg_mr)(ibv_pd *, void *, size_t, int){nullptr};
  decltype(&ibv_dereg_mr) dereg_mr{nullptr};
  decltype(&ibv_create_cq) create_cq{nullptr};
  decltype(&ibv_destroy_cq) destroy_cq{nullptr};
  decltype(&ibv_create_qp) create_qp{nullptr};
  decltype(&ibv_destroy_qp) destroy_qp{nullptr};
  decltype(&ibv_modify_qp) modify_qp{nullptr};
  int (*plane_count)(ibv_context *, int *){nullptr};
  int (*set_plane)(ibv_qp *, int){nullptr};
  int (*get_plane)(ibv_qp *, int *){nullptr};
  ibv_context_extend *(*open_extend)(ibv_context *){nullptr};
  int (*close_extend)(ibv_context_extend *){nullptr};
  int (*query_feature)(ibv_context_extend *, ibv_qp *, uint32_t, uint32_t, HostRoceHyperFeature *){nullptr};
  int (*negotiate)(ibv_context_extend *, ibv_qp *, const HostRoceHyperFeature *, HostRoceHyperFeature *,
                   uint32_t *){nullptr};
  int (*modify_extend)(ibv_context_extend *, HostRoceExtendedQpAttr *, int){nullptr};
};

}  // namespace hixl
#endif
#endif  // CANN_HIXL_SRC_HIXL_CS_HOST_ROCE_VERBS_LOADER_H_
