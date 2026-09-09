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
 * @file root_info_builder.h
 * @brief RootInfo builder module
 *
 * Builds RootInfo from an NPU ID, including:
 * - Calling DCMI to obtain URMA device info
 * - Building port-to-EID maps from product form
 */

#ifndef CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H
#define CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include "hixl/hixl_types.h"
#include "proxy/dcmi_proxy.h"

namespace hixl {

// ============ Error codes ============
// Global status codes from hixl_types.h:
//   SUCCESS              — success
//   PARAM_INVALID        — invalid argument (file not found, JSON parse failure, etc.)
//   FAILED               — internal failure (DCMI API failure, no EID, etc.)

// ============ URMA Device data ============

/**
 * @brief URMA Device
 * Each URMA Device contains multiple EIDs; die_id is taken from the first EID
 */
struct UrmaDevice {
  std::string name;                   // Device name, e.g. "udma0"
  std::vector<std::string> eid_list;  // All EIDs on this device
};

// ============ RootInfo data ============

/**
 * @brief CLOS PG EID info
 */
struct ClosPgEidInfo {
  std::string eid;  // CLOS PG EID
  int32_t die_id;   // die_id of this PG EID
};

/**
 * @brief NPU serial-port to EID mapping
 * key: serial-port id "die_id/port"
 * value: corresponding EID string
 */
struct NpuRootInfo {
  std::map<std::string, std::string> port_to_eid;  // Mesh-layer serial-port to EID map
  std::vector<ClosPgEidInfo> clos_pg_eids;         // CLOS-layer PG EID list (may have more than one)
};

/**
 * @brief Parsed EID byte-6 fields
 */
struct EidByte6Info {
  uint8_t byte6;        // Raw byte-6 value
  uint8_t high_nibble;  // High 4 bits
  uint8_t low_nibble;   // Low 4 bits
  uint8_t die_id;       // die_id (0 or 1)
  bool is_pg_eid;       // Whether this is a PG EID (port group)
  int32_t port;         // port value (0-15)
};

// ============ EID parse helpers ============

/**
 * @brief Parse byte 6 of an EID
 * @param [in] eid EID string
 * @return Parsed EidByte6Info
 */
EidByte6Info ParseEidByte6(const std::string &eid);

// ============ RootInfo build APIs ============

/**
 * @brief Build RootInfo from an NPU ID
 * @param [in] npu_id NPU ID
 * @param [in] mesh_die_id Mesh-layer die_id (from topo, not product-form assumptions)
 * @param [in] clos_die_id CLOS-layer die_id (from topo, for logs)
 * @param [in] clos_port_keys CLOS serial-port keys ("die/port") of this NPU from topo
 * @param [out] root_info Output RootInfo
 * @return SUCCESS on success, other error codes on failure
 *
 * A URMA group is a CLOS PG group only when every physical port in the group is a
 * CLOS port in clos_port_keys. plane_pg_0 is the CLOS group with more CLOS ports;
 * plane_pg_1 is the group with fewer, when two groups exist.
 */
Status BuildNpuRootInfo(int32_t npu_id, int32_t mesh_die_id, int32_t clos_die_id,
                        const std::set<std::string> &clos_port_keys, NpuRootInfo &root_info);

/**
 * @brief Get URMA Device list
 * @param [in] npu_id NPU ID
 * @param [out] urma_devices URMA Device list
 * @return SUCCESS on success, other error codes on failure
 */
Status GetUrmaDeviceList(int32_t npu_id, std::vector<UrmaDevice> &urma_devices);

}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_ENGINE_HIXL_ROOTINFO_BUILDER_H
