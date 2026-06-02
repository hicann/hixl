/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file root_info_builder.cc
 * @brief RootInfo 构建模块实现
 *
 * 实现根据 NPU ID 构建 RootInfo 的功能
 */

#include "rootinfo_builder_generator_v1.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "common/hixl_log.h"

namespace hixl {

// ============ EID 解析实现 ============

EidByte6Info ParseEidByte6(const std::string &eid) {
  constexpr size_t kEidMinStrLen = 12U;
  constexpr size_t kByte6StrOffset = 10U;
  constexpr size_t kByteStrLen = 2U;
  constexpr int32_t kHexBase = 16;
  constexpr int32_t kNibbleShift = 4;
  constexpr uint8_t kNibbleMask = 0xFU;
  constexpr uint8_t kDieIdBit = 0x4U;
  constexpr uint8_t kPgEidValue1 = 0x3U;
  constexpr uint8_t kPgEidValue2 = 0x7U;

  EidByte6Info info{};
  if (eid.length() < kEidMinStrLen) {
    return info;
  }

  try {
    std::string byte_str = eid.substr(kByte6StrOffset, kByteStrLen);
    info.byte6 = static_cast<uint8_t>(std::stoi(byte_str, nullptr, kHexBase));
    info.high_nibble = static_cast<uint8_t>((info.byte6 >> kNibbleShift) & kNibbleMask);
    info.low_nibble = static_cast<uint8_t>(info.byte6 & kNibbleMask);
    info.die_id = (info.high_nibble & kDieIdBit) != 0 ? 1U : 0U;
    info.is_pg_eid = (info.high_nibble == kPgEidValue1 || info.high_nibble == kPgEidValue2);
    info.port = static_cast<int32_t>(info.low_nibble);
  } catch (...) {
    info = EidByte6Info{};
  }
  return info;
}

// ============ URMA Device 获取实现（内部函数） ============

namespace {

constexpr int32_t kSetwWidth = 2;
constexpr int32_t kNpuGroupSize = 8;
constexpr int32_t kFirstHalfThreshold = 3;
constexpr int32_t kPortMaxValue = 8;
constexpr size_t kSecondElementIndex = 2;

std::string ConvertEidToString(const unsigned char *raw, size_t len) {
  if (raw == nullptr || len < static_cast<size_t>(kDcmiUrmaEidSize)) {
    return "";
  }
  std::ostringstream oss;
  for (int32_t k = 0; k < kDcmiUrmaEidSize; ++k) {
    oss << std::hex << std::setfill('0') << std::setw(kSetwWidth) << static_cast<int>(raw[k]);
  }
  return oss.str();
}

int32_t LoadUrmaDevicesFromDcmi(int32_t npu_id, std::vector<UrmaDevice> &urma_devices) {
  if (DcmiProxy::LoadDcmi() != 0) {
    HIXL_LOGE(FAILED, "DCMI not loaded");
    return FAILED;
  }

  uint32_t logic_id = 0;
  if (DcmiProxy::GetLogicIdFromPhyId(npu_id, &logic_id) != 0) {
    HIXL_LOGE(FAILED, "Failed to get logic id from npu id: %d", npu_id);
    return FAILED;
  }

  uint32_t dev_cnt = 0;
  int32_t ret = DcmiProxy::GetUrmaDeviceCnt(logic_id, &dev_cnt);
  if (ret != 0) {
    HIXL_LOGE(FAILED, "Failed to get urma device count, ret=%d", ret);
    return FAILED;
  }

  for (size_t i = 0; i < dev_cnt; ++i) {
    UrmaDevice urma_dev;
    urma_dev.name = "udma" + std::to_string(i);

    DcmiUrmaEidInfo eid_buf[kMaxEidPerUe];
    int32_t eid_cnt = kMaxEidPerUe;
    ret = DcmiProxy::GetEidList(logic_id, i, eid_buf, &eid_cnt);
    if (ret != 0) {
      continue;
    }

    for (int32_t j = 0; j < eid_cnt; ++j) {
      std::string eid_str = ConvertEidToString(eid_buf[j].eid.raw, sizeof(eid_buf[j].eid.raw));
      if (!eid_str.empty() && eid_str != "00000000000000000000000000000000") {
        urma_dev.eid_list.push_back(eid_str);
      }
    }

    if (!urma_dev.eid_list.empty()) {
      urma_devices.push_back(urma_dev);
    }
  }

  return SUCCESS;
}

// ============ RootInfo 构建实现 ============

}  // anonymous namespace

int32_t GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice> &urma_devices) {
  urma_devices.clear();
  return LoadUrmaDevicesFromDcmi(npu_id, urma_devices);
}

/**
 * @brief 确定 Mesh 层的 die_id
 * @param npu_id NPU ID
 * @param is_server 是否为 Server 产品形态
 * @return Mesh 层所在的 die_id
 *
 * Server: Mesh 在 1die
 * Pod: 根据 npu_id % 8 判断，0-3 在 0die，4-7 在 1die
 */
int32_t GetMeshDieId(int32_t npu_id, bool is_server) {
  if (is_server) {
    // Server: Mesh 在 1die
    return 1;
  } else {
    // Pod: Mesh 在哪个 die 取决于 npu_id % kNpuGroupSize
    int32_t mod = npu_id % kNpuGroupSize;
    if (mod >= 0 && mod <= kFirstHalfThreshold) {
      return 0;  // 前4个 NPU，Mesh 在 0die
    } else {
      return 1;  // 后4个 NPU，Mesh 在 1die
    }
  }
}

void PrintEidDebugInfo(const std::string &eid, const EidByte6Info &info) {
  HIXL_LOGD("EID: %s, byte6=0x%x, high=0x%x, low=0x%x, die_id=%d, is_pg=%s, port=%d", eid.c_str(), info.byte6,
            info.high_nibble, info.low_nibble, info.die_id, info.is_pg_eid ? "true" : "false", info.port);
}

void PrintRootInfo(const NpuRootInfo &root_info) {
  HIXL_LOGI("Built %zu port->eid mappings, clos_pg_eids=%zu", root_info.port_to_eid.size(),
            root_info.clos_pg_eids.size());
  for (const auto &kv : root_info.port_to_eid) {
    HIXL_LOGD("  %s -> %s", kv.first.c_str(), kv.second.c_str());
  }
  for (const auto &pg : root_info.clos_pg_eids) {
    HIXL_LOGD("  CLOS PG EID: %s", pg.eid.c_str());
  }
}

void CollectMeshPorts(const std::vector<UrmaDevice> &urma_devices, int32_t mesh_die_id, NpuRootInfo &root_info) {
  for (const auto &urma_dev : urma_devices) {
    if (urma_dev.eid_list.empty()) {
      continue;
    }

    for (const auto &eid : urma_dev.eid_list) {
      EidByte6Info info = ParseEidByte6(eid);
      PrintEidDebugInfo(eid, info);

      if (info.is_pg_eid) {
        continue;
      }

      if (info.port >= 0 && info.port <= kPortMaxValue && info.die_id == mesh_die_id) {
        std::string port_key = std::to_string(info.die_id) + "/" + std::to_string(info.port);
        root_info.port_to_eid[port_key] = eid;
        HIXL_LOGI("Add Mesh port: %s", port_key.c_str());
      }
    }
  }
}

void CollectClosPgEids(const std::vector<UrmaDevice> &urma_devices, int32_t mesh_die_id, NpuRootInfo &root_info) {
  struct UrmaGroupInfo {
    std::string pg_eid;
    int32_t die_id;
    size_t total_eids;
  };
  std::vector<UrmaGroupInfo> mesh_groups;
  std::vector<UrmaGroupInfo> non_mesh_groups;

  for (const auto &urma_dev : urma_devices) {
    if (urma_dev.eid_list.empty()) {
      continue;
    }

    std::string pg_eid;
    int32_t pg_die_id = -1;
    for (const auto &eid : urma_dev.eid_list) {
      EidByte6Info info = ParseEidByte6(eid);
      if (info.is_pg_eid) {
        pg_eid = eid;
        pg_die_id = info.die_id;
      }
    }
    if (pg_eid.empty()) {
      continue;
    }

    size_t total_eids = urma_dev.eid_list.size();
    // mesh 组（7 直连串口 + 1 PG = 8 EID）跳过
    if (pg_die_id == mesh_die_id && total_eids == 8) {
      continue;
    }

    if (pg_die_id == mesh_die_id) {
      mesh_groups.push_back({pg_eid, pg_die_id, total_eids});
    } else {
      non_mesh_groups.push_back({pg_eid, pg_die_id, total_eids});
    }
  }

  // 非 mesh_die_id：取 EID 数最多的组的 PG 作为 plane_pg_0
  if (!non_mesh_groups.empty()) {
    auto best =
        std::max_element(non_mesh_groups.begin(), non_mesh_groups.end(),
                         [](const UrmaGroupInfo &a, const UrmaGroupInfo &b) { return a.total_eids < b.total_eids; });
    root_info.clos_pg_eids.push_back({best->pg_eid, best->die_id});
  }

  // mesh_die_id：多个 PG 取 EID 数第二多的作为 plane_pg_1
  if (mesh_groups.size() >= kSecondElementIndex) {
    std::sort(mesh_groups.begin(), mesh_groups.end(),
              [](const UrmaGroupInfo &a, const UrmaGroupInfo &b) { return a.total_eids > b.total_eids; });
    root_info.clos_pg_eids.push_back({mesh_groups[kSecondElementIndex - 1].pg_eid, mesh_groups[kSecondElementIndex - 1].die_id});
  }
}

int32_t BuildNpuRootInfo(int32_t npu_id, bool is_server, NpuRootInfo &root_info) {
  HIXL_LOGI("npu_id=%d, is_server=%d", npu_id, is_server);

  std::vector<UrmaDevice> urma_devices;
  int32_t ret = GetUrmaDeviceList(npu_id, urma_devices);
  if (ret != SUCCESS) {
    HIXL_LOGE(FAILED, "Failed to get urma devices, ret=%d", ret);
    return ret;
  }

  if (urma_devices.empty()) {
    HIXL_LOGE(FAILED, "No urma devices for npu_id=%d", npu_id);
    return FAILED;
  }

  HIXL_LOGI("Got %zu urma device(s)", urma_devices.size());
  for (size_t i = 0; i < urma_devices.size(); ++i) {
    HIXL_LOGI("urma_dev[%zu]: name=%s, eids=%zu", i, urma_devices[i].name.c_str(),
              urma_devices[i].eid_list.size());
    for (size_t j = 0; j < urma_devices[i].eid_list.size(); ++j) {
      HIXL_LOGI("eid[%zu]=%s", j, urma_devices[i].eid_list[j].c_str());
    }
  }

  int32_t mesh_die_id = GetMeshDieId(npu_id, is_server);
  HIXL_LOGI("Mesh die_id=%d", mesh_die_id);

  root_info.port_to_eid.clear();
  root_info.clos_pg_eids.clear();

  CollectMeshPorts(urma_devices, mesh_die_id, root_info);
  CollectClosPgEids(urma_devices, mesh_die_id, root_info);
  PrintRootInfo(root_info);

  if (root_info.port_to_eid.empty() || root_info.clos_pg_eids.empty()) {
    HIXL_LOGE(FAILED, "Incomplete root_info for npu_id=%d, ports=%zu, clos_pg=%zu", npu_id, root_info.port_to_eid.size(),
              root_info.clos_pg_eids.size());
    return FAILED;
  }

  return SUCCESS;
}

}  // namespace hixl
