/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <memory>

#include "gtest/gtest.h"
#include "cs/channel_factory.h"
#include "cs/endpoint.h"
#include "cs/hcomm_device_channel.h"
#include "cs/hcomm_host_channel.h"
#include "cs/ubmem/ubmem_channel.h"

namespace hixl {
namespace {

EndpointDesc MakeEndpoint(EndpointLocType loc, CommProtocol protocol) {
  EndpointDesc ep{};
  ep.loc.locType = loc;
  ep.protocol = protocol;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = 1U;
  return ep;
}

bool IsUbMemChannel(const ChannelPtr &channel) {
  return std::dynamic_pointer_cast<UbMemChannel>(channel) != nullptr;
}

bool IsHostChannel(const ChannelPtr &channel) {
  return std::dynamic_pointer_cast<HcommHostChannel>(channel) != nullptr;
}

bool IsHccsDeviceChannel(const ChannelPtr &channel) {
  return std::dynamic_pointer_cast<HccsDeviceChannel>(channel) != nullptr;
}

bool IsRoceDeviceChannel(const ChannelPtr &channel) {
  return std::dynamic_pointer_cast<RoceDeviceChannel>(channel) != nullptr;
}

bool IsUbDeviceChannel(const ChannelPtr &channel) {
  return std::dynamic_pointer_cast<UbDeviceChannel>(channel) != nullptr;
}

TEST(ChannelFactoryUt, SelectsUbMemChannelForUbMemProtocol) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_DEVICE, COMM_PROTOCOL_UB_MEM));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsUbMemChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, SelectsHostChannelForHostEndpoint) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_HOST, COMM_PROTOCOL_ROCE));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsHostChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, SelectsUbMemChannelForHostUbMemProtocol) {
  // Protocol comes first: a host-located UB_MEM endpoint stays the logical channel, not the host channel.
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_HOST, COMM_PROTOCOL_UB_MEM));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsUbMemChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, SelectsHccsDeviceChannelForHccs) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_DEVICE, COMM_PROTOCOL_HCCS));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsHccsDeviceChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, SelectsRoceDeviceChannelForDeviceRoce) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_DEVICE, COMM_PROTOCOL_ROCE));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsRoceDeviceChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, SelectsUbDeviceChannelForUbCtp) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_DEVICE, COMM_PROTOCOL_UBC_CTP));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsUbDeviceChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, SelectsUbDeviceChannelForUbTp) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_DEVICE, COMM_PROTOCOL_UBC_TP));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsUbDeviceChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, SelectsUbDeviceChannelForUboe) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_DEVICE, COMM_PROTOCOL_UBOE));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsUbDeviceChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, SelectsUbDeviceChannelForUbg) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_DEVICE, COMM_PROTOCOL_UBG));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_TRUE(IsUbDeviceChannel(ChannelFactory::Create(*endpoint)));
}

TEST(ChannelFactoryUt, RejectsUnsupportedDeviceProtocol) {
  auto endpoint = Endpoint::Create(MakeEndpoint(ENDPOINT_LOC_TYPE_DEVICE, COMM_PROTOCOL_PCIE));
  ASSERT_NE(endpoint, nullptr);
  EXPECT_EQ(ChannelFactory::Create(*endpoint), nullptr);
}

}  // namespace
}  // namespace hixl
