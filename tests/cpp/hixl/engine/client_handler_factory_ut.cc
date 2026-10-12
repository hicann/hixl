/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "ascendcl_stub.h"
#include "common/hixl_inner_types.h"
#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "depends/sys_api/src/sys_api_wrap.h"
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"
#include "engine/direct_client_handler.h"
#include "engine/direct_multi_channel_handler.h"
#include "engine/endpoint_test_utils.h"
#include "engine/test_mmpa_utils.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
EndpointConfig MakeUbMemEp() {
  EndpointConfig ep{};
  ep.protocol = kProtocolUbmem;
  ep.comm_id = "0";
  ep.placement = kPlacementDevice;
  ep.net_instance_id = "sp-ut";
  ep.device_info.phy_device_id = 0;
  ep.device_info.super_device_id = 9;
  ep.device_info.super_pod_id = 8;
  return ep;
}

HandlerCreateArgs MakeDirectArgs(uint32_t workers) {
  HandlerCreateArgs args{};
  args.server_ip = "127.0.0.1";
  args.server_port = 26662U;
  args.handler_type = HandlerCreateArgs::HandlerType::DIRECT;
  HandlerCreateArgs::EndpointPair pair{};
  pair.local = MakeUbMemEp();
  pair.remote = MakeUbMemEp();
  pair.type = CommType::COMM_TYPE_UBMEM;
  args.matched_pairs.push_back(pair);
  args.global_config.comm_resource_config.multi_worker_num = workers;
  return args;
}
}  // namespace

class ClientHandlerFactoryUbMemUt : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", 0, 0, 9, 8);
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    hixl_test::InstallSysApiHooks(std::make_shared<hixl::test::KernelJsonMmpaStub>());
    VirtualMemoryManager::GetInstance().Finalize();
  }

  void TearDown() override {
    VirtualMemoryManager::GetInstance().Finalize();
    hixl_test::ResetSysApiHooks();
    llm::AclRuntimeStub::Reset();
  }

  std::shared_ptr<endpoint_test::MockAclRuntimeStub> acl_stub_;
};

TEST_F(ClientHandlerFactoryUbMemUt, MemcpyModeKeepsSingleDirectHandlerWhenWorkersGreaterThanOne) {
  std::unique_ptr<IClientHandler> handler;
  auto args = MakeDirectArgs(2U);
  args.global_config.fabric_memory.enable_aicpu_unfold = false;
  ASSERT_EQ(ClientHandlerFactory::Create(args, handler), SUCCESS);
  ASSERT_NE(handler, nullptr);
  EXPECT_NE(dynamic_cast<DirectClientHandler *>(handler.get()), nullptr);
  EXPECT_EQ(dynamic_cast<DirectMultiChannelHandler *>(handler.get()), nullptr);
  EXPECT_EQ(handler->Finalize(), SUCCESS);
}

TEST_F(ClientHandlerFactoryUbMemUt, DeviceKeepsSingleDirectHandlerWhenWorkersGreaterThanOne) {
  std::unique_ptr<IClientHandler> handler;
  ASSERT_EQ(ClientHandlerFactory::Create(MakeDirectArgs(2U), handler), SUCCESS);
  ASSERT_NE(handler, nullptr);
  EXPECT_NE(dynamic_cast<DirectClientHandler *>(handler.get()), nullptr);
  EXPECT_EQ(dynamic_cast<DirectMultiChannelHandler *>(handler.get()), nullptr);
  EXPECT_EQ(handler->Finalize(), SUCCESS);
}
}  // namespace hixl
