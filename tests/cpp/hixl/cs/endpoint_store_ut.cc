/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <cstring>
#include "endpoint_store.h"

namespace hixl {
namespace {

constexpr uintptr_t kTestHandleSeed = 7U;
constexpr uint32_t kInvalidAddrType = 0xFFU;

EndpointDesc MakeUbEndpoint(CommProtocol protocol, const std::array<uint8_t, COMM_ADDR_EID_LEN> &eid) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.protocol = protocol;
  ep.commAddr.type = COMM_ADDR_TYPE_EID;
  auto ret = memcpy_s(ep.commAddr.eid, COMM_ADDR_EID_LEN, eid.data(), COMM_ADDR_EID_LEN);
  if (ret != EOK) {
    HIXL_LOGE(FAILED, "memcpy_s failed, ret=%d", ret);
  }
  return ep;
}

EndpointDesc MakeRoceIpv4Endpoint(const char *ip) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_HOST;
  ep.protocol = COMM_PROTOCOL_ROCE;
  ep.commAddr.type = COMM_ADDR_TYPE_IP_V4;
  (void)inet_pton(AF_INET, ip, &ep.commAddr.addr);
  return ep;
}

EndpointDesc MakeRoceIpv6Endpoint(const char *ip) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_HOST;
  ep.protocol = COMM_PROTOCOL_ROCE;
  ep.commAddr.type = COMM_ADDR_TYPE_IP_V6;
  (void)inet_pton(AF_INET6, ip, &ep.commAddr.addr6);
  return ep;
}

}  // namespace

TEST(EndpointStoreUt, MatchEndpointSucceedsForUbTpByEid) {
  EndpointStore store;
  const std::array<uint8_t, COMM_ADDR_EID_LEN> eid = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                                      0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  const EndpointDesc endpoint = MakeUbEndpoint(COMM_PROTOCOL_UBC_TP, eid);

  EndpointHandle created_handle = nullptr;
  ASSERT_EQ(store.CreateEndpoint(endpoint, created_handle), SUCCESS);
  ASSERT_NE(created_handle, nullptr);

  EndpointHandle matched_handle = nullptr;
  auto matched = store.MatchEndpoint(MakeUbEndpoint(COMM_PROTOCOL_UBC_TP, eid), matched_handle);
  ASSERT_NE(matched, nullptr);
  EXPECT_EQ(matched_handle, created_handle);

  EXPECT_EQ(store.Finalize(), SUCCESS);
}

TEST(EndpointStoreUt, MatchEndpointFailsForUbCtpWhenEidDiffers) {
  EndpointStore store;
  const std::array<uint8_t, COMM_ADDR_EID_LEN> eid = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                                      0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};
  std::array<uint8_t, COMM_ADDR_EID_LEN> different_eid = eid;
  different_eid[COMM_ADDR_EID_LEN - 1] ^= 0x01U;

  EndpointHandle created_handle = nullptr;
  ASSERT_EQ(store.CreateEndpoint(MakeUbEndpoint(COMM_PROTOCOL_UBC_CTP, eid), created_handle), SUCCESS);
  ASSERT_NE(created_handle, nullptr);

  EndpointHandle matched_handle = reinterpret_cast<EndpointHandle>(kTestHandleSeed);
  auto matched = store.MatchEndpoint(MakeUbEndpoint(COMM_PROTOCOL_UBC_CTP, different_eid), matched_handle);
  EXPECT_EQ(matched, nullptr);
  EXPECT_EQ(matched_handle, reinterpret_cast<EndpointHandle>(kTestHandleSeed));

  EXPECT_EQ(store.Finalize(), SUCCESS);
}

TEST(EndpointStoreUt, MatchEndpointSucceedsForRoceIpv4) {
  EndpointStore store;
  const EndpointDesc endpoint = MakeRoceIpv4Endpoint("127.0.0.1");

  EndpointHandle created_handle = nullptr;
  ASSERT_EQ(store.CreateEndpoint(endpoint, created_handle), SUCCESS);
  ASSERT_NE(created_handle, nullptr);

  EndpointHandle matched_handle = nullptr;
  auto matched = store.MatchEndpoint(MakeRoceIpv4Endpoint("127.0.0.1"), matched_handle);
  ASSERT_NE(matched, nullptr);
  EXPECT_EQ(matched_handle, created_handle);

  EXPECT_EQ(store.Finalize(), SUCCESS);
}

TEST(EndpointStoreUt, MatchEndpointSucceedsForRoceIpv6) {
  EndpointStore store;
  const EndpointDesc endpoint = MakeRoceIpv6Endpoint("2001:db8::1");

  EndpointHandle created_handle = nullptr;
  ASSERT_EQ(store.CreateEndpoint(endpoint, created_handle), SUCCESS);
  ASSERT_NE(created_handle, nullptr);

  EndpointHandle matched_handle = nullptr;
  auto matched = store.MatchEndpoint(MakeRoceIpv6Endpoint("2001:db8::1"), matched_handle);
  ASSERT_NE(matched, nullptr);
  EXPECT_EQ(matched_handle, created_handle);

  EXPECT_EQ(store.Finalize(), SUCCESS);
}

TEST(EndpointStoreUt, MatchEndpointFailsForRoceWhenAddrTypeDiffers) {
  EndpointStore store;

  EndpointHandle created_handle = nullptr;
  ASSERT_EQ(store.CreateEndpoint(MakeRoceIpv4Endpoint("127.0.0.1"), created_handle), SUCCESS);
  ASSERT_NE(created_handle, nullptr);

  EndpointHandle matched_handle = nullptr;
  auto matched = store.MatchEndpoint(MakeRoceIpv6Endpoint("::1"), matched_handle);
  EXPECT_EQ(matched, nullptr);
  EXPECT_EQ(matched_handle, nullptr);

  EXPECT_EQ(store.Finalize(), SUCCESS);
}

TEST(EndpointStoreUt, MatchEndpointFailsForRoceWhenAddrTypeInvalid) {
  EndpointStore store;
  EndpointDesc endpoint{};
  endpoint.loc.locType = ENDPOINT_LOC_TYPE_HOST;
  endpoint.protocol = COMM_PROTOCOL_ROCE;
  endpoint.commAddr.type = static_cast<CommAddrType>(kInvalidAddrType);

  EndpointHandle created_handle = nullptr;
  ASSERT_EQ(store.CreateEndpoint(endpoint, created_handle), SUCCESS);
  ASSERT_NE(created_handle, nullptr);

  EndpointHandle matched_handle = nullptr;
  auto matched = store.MatchEndpoint(endpoint, matched_handle);
  EXPECT_EQ(matched, nullptr);
  EXPECT_EQ(matched_handle, nullptr);

  EXPECT_EQ(store.Finalize(), SUCCESS);
}

}  // namespace hixl
