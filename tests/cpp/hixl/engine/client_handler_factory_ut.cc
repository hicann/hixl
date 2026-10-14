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

#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

#include "ascendcl_stub.h"
#include "common/ctrl_msg.h"
#include "common/ctrl_msg_plugin.h"
#include "common/hixl_inner_types.h"
#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "depends/sys_api/src/sys_api_wrap.h"
#include "engine/client_handler.h"
#include "engine/client_handler_factory.h"
#include "engine/direct_client_handler.h"
#include "engine/direct_multi_channel_handler.h"
#include "engine/ub_client_handler.h"
#include "engine/ub_multi_channel_handler.h"
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

namespace {
EndpointConfig MakeUbCtpEp() {
  EndpointConfig ep{};
  ep.protocol = "ub_ctp";
  ep.comm_id = "000000000000000000000000c0a80563";
  ep.placement = kPlacementDevice;
  ep.net_instance_id = "sp-ut";
  ep.device_info.phy_device_id = 0;
  ep.device_info.super_device_id = 9;
  ep.device_info.super_pod_id = 8;
  return ep;
}

// 应答一次 GetMemInfoReq：返回空内存清单（空 JSON 数组）
void ServeGetMemInfo(int32_t fd) {
  CtrlMsgHeader req_header{};
  if (recv(fd, &req_header, sizeof(req_header), 0) != static_cast<ssize_t>(sizeof(req_header))) {
    return;
  }
  CtrlMsgType req_type{};
  if (recv(fd, &req_type, sizeof(req_type), 0) != static_cast<ssize_t>(sizeof(req_type))) {
    return;
  }
  const std::string json_str = "[]";
  const uint64_t body_size = sizeof(CtrlMsgType) + json_str.size();
  CtrlMsgHeader resp_header{};
  resp_header.magic = kMagicNumber;
  resp_header.body_size = body_size;
  CtrlMsgType resp_type = CtrlMsgType::kGetMemInfoResp;
  (void)send(fd, &resp_header, sizeof(resp_header), 0);
  (void)send(fd, &resp_type, sizeof(resp_type), 0);
  (void)send(fd, json_str.data(), json_str.size(), 0);
}
}  // namespace

class ClientHandlerFactoryUbUt : public ::testing::Test {
 private:
  // 仿 hixl_engine_unittest：mock libra.so 的 RaRdevGetHandle/RaGetNotifyBaseAddr，
  // 使 TransferPool 通知地址解析在 UT 环境可用
  class HccpSysApiStub : public hixl::test::KernelJsonMmpaStub {
   public:
    void *DlOpen(const char *file_name, int32_t mode) override {
      if (file_name != nullptr && std::strcmp(file_name, "libra.so") == 0) {
        return hixl_test::RealDlOpen("libdl.so.2", mode);
      }
      return hixl_test::SysApiPassSentinelPtr();
    }

    void *DlSym(void *handle, const char *func_name) override {
      (void)handle;
      if (func_name != nullptr && std::strcmp(func_name, "RaRdevGetHandle") == 0) {
        return reinterpret_cast<void *>(&MockRaRdevGetHandle);
      }
      if (func_name != nullptr && std::strcmp(func_name, "RaGetNotifyBaseAddr") == 0) {
        return reinterpret_cast<void *>(&MockRaGetNotifyBaseAddr);
      }
      return hixl_test::SysApiPassSentinelPtr();
    }

   private:
    static int MockRaRdevGetHandle(unsigned int phy_id, void **out_handle) {
      (void)phy_id;
      static int dummy_handle = 0;
      if (out_handle != nullptr) {
        *out_handle = &dummy_handle;
      }
      return 0;
    }

    static int MockRaGetNotifyBaseAddr(void *handle, unsigned long long *addr, unsigned long long *size) {
      (void)handle;
      if (addr != nullptr) {
        *addr = 0x1000ULL;
      }
      if (size != nullptr) {
        *size = 0x10000ULL;
      }
      return 0;
    }
  };

 protected:
  void SetUp() override {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", 0, 0, 9, 8);
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    hixl_test::InstallSysApiHooks(std::make_shared<HccpSysApiStub>());
  }

  void TearDown() override {
    hixl_test::ResetSysApiHooks();
    llm::AclRuntimeStub::Reset();
  }

  HandlerCreateArgs MakeUbArgs(uint32_t workers) {
    HandlerCreateArgs args{};
    args.server_ip = "127.0.0.1";
    args.server_port = 26663U;
    args.handler_type = HandlerCreateArgs::HandlerType::UB;
    HandlerCreateArgs::EndpointPair pair{};
    pair.local = MakeUbCtpEp();
    pair.remote = MakeUbCtpEp();
    pair.type = CommType::COMM_TYPE_UB_D2D;
    args.matched_pairs.push_back(pair);
    args.global_config.comm_resource_config.multi_worker_num = workers;
    args.timeout_ms = 5000U;
    return args;
  }

  std::shared_ptr<endpoint_test::MockAclRuntimeStub> acl_stub_;
};

TEST_F(ClientHandlerFactoryUbUt, WorkersGreaterThanOneCreatesUbMultiChannelHandler) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::thread server([fd = fds[1]]() { ServeGetMemInfo(fd); });

  std::unique_ptr<IClientHandler> handler;
  auto args = MakeUbArgs(2U);
  args.ctrl_socket = fds[0];
  const Status create_ret = ClientHandlerFactory::Create(args, handler);
  server.join();
  ASSERT_EQ(create_ret, SUCCESS);
  ASSERT_NE(handler, nullptr);
  EXPECT_NE(dynamic_cast<UbMultiChannelHandler *>(handler.get()), nullptr);
  EXPECT_EQ(handler->Finalize(), SUCCESS);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(ClientHandlerFactoryUbUt, SingleWorkerFallsBackToUbClientHandler) {
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  std::thread server([fd = fds[1]]() { ServeGetMemInfo(fd); });

  std::unique_ptr<IClientHandler> handler;
  auto args = MakeUbArgs(1U);
  args.ctrl_socket = fds[0];
  const Status create_ret = ClientHandlerFactory::Create(args, handler);
  server.join();
  ASSERT_EQ(create_ret, SUCCESS);
  ASSERT_NE(handler, nullptr);
  EXPECT_EQ(dynamic_cast<UbMultiChannelHandler *>(handler.get()), nullptr);
  EXPECT_NE(dynamic_cast<UbClientHandler *>(handler.get()), nullptr);
  EXPECT_EQ(handler->Finalize(), SUCCESS);

  close(fds[0]);
  close(fds[1]);
}
}  // namespace hixl
