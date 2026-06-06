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
#include <vector>
#include <cstdlib>
#include <thread>
#include <atomic>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "cs/hixl_cs.h"
#include "engine/endpoint_test_utils.h"
#define private public
#include "engine/hixl_client.h"
#include "engine/direct_client_handler.h"
#include "engine/ub_client_handler.h"
#undef private
#include "engine/endpoint_generator.h"
#include "engine/endpoint_matcher.h"
#include "common/hixl_inner_types.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "engine/test_mmpa_utils.h"
#include "common/hixl_utils.h"
#include "common/ctrl_msg_plugin.h"
#include "common/segment.h"

using namespace ::testing;

namespace hixl {
static constexpr uint32_t kServerPort = 26380;
static constexpr uint32_t kBackLog = 1024U;
static constexpr uint32_t kDefaultTimeoutMs = 5000;
static constexpr uint32_t kShortMs = 1;
static constexpr uint32_t kMilliSeconds1 = 1;
static constexpr uint32_t kSleepMs = 10;
static constexpr uint32_t kSleepLongTimeMs = 30000;
static constexpr uint32_t kMemNum = 100U;
static constexpr uint32_t kNum1 = 1;
static constexpr uint32_t kNum2 = 2;
static constexpr uint32_t default_list_num = 2;
static constexpr uint32_t list_num_4ub = 4;
static constexpr uint8_t kDefaultRdmaTc = 132;
static constexpr uint8_t kDefaultRdmaSl = 4;
static constexpr uint32_t kInflightPollIntervalMs = 1U;
static std::vector<uint32_t> kLocalMems(kMemNum, kNum1);
static std::vector<uint32_t> kRemoteMems(kMemNum, kNum2);
enum class MockHixlServerMode : uint32_t {
  k4UbNormal = 0,
  k2UbNormal,
  // GetEndpointInfoResp 相关异常
  kGetEndpointInfoResp_BadMagic,
  kGetEndpointInfoResp_BadMsgType,
  kGetEndpointInfoResp_BadBodySizeTooSmall,  // body_size <= sizeof(CtrlMsgType)
  kGetEndpointInfoResp_BadJson,
  kGetEndpointInfoResp_JsonIsNotArray,
  kGetEndpointInfoResp_MissingField,
};
// server 内存信息
static CommMem default_remote_mem_list[] = {{COMM_MEM_TYPE_HOST, &kRemoteMems[0], sizeof(uint32_t)},
                                            {COMM_MEM_TYPE_DEVICE, &kRemoteMems[2], sizeof(uint32_t)}};
static CommMem remote_mem_list_4ub[] = {{COMM_MEM_TYPE_HOST, &kRemoteMems[0], sizeof(uint32_t)},
                                        {COMM_MEM_TYPE_DEVICE, &kRemoteMems[2], sizeof(uint32_t)},
                                        {COMM_MEM_TYPE_HOST, &kRemoteMems[4], sizeof(uint32_t)},
                                        {COMM_MEM_TYPE_DEVICE, &kRemoteMems[6], sizeof(uint32_t)}};
// client 内存信息
static CommMem default_local_mem_list[] = {{COMM_MEM_TYPE_HOST, &kLocalMems[0], sizeof(uint32_t)},
                                           {COMM_MEM_TYPE_DEVICE, &kLocalMems[2], sizeof(uint32_t)}};
static CommMem local_mem_list_4ub[] = {{COMM_MEM_TYPE_DEVICE, &kLocalMems[0], sizeof(uint32_t)},
                                       {COMM_MEM_TYPE_DEVICE, &kLocalMems[2], sizeof(uint32_t)},
                                       {COMM_MEM_TYPE_HOST, &kLocalMems[4], sizeof(uint32_t)},
                                       {COMM_MEM_TYPE_HOST, &kLocalMems[6], sizeof(uint32_t)}};

class MockHixlServer {
 public:
  MockHixlServer() : mode_(MockHixlServerMode::k4UbNormal) {}
  ~MockHixlServer() {
    DestroyServerAndUnreg();
  }
  void SetMode(MockHixlServerMode m) {
    mode_ = m;
  }

  Status CreateServer(const std::vector<EndpointConfig> &remote_endpoint_list) {
    HixlServerConfig config{};
    std::vector<EndpointDesc> endpointInfoList;
    for (const auto &ep : remote_endpoint_list) {
      EndpointDesc endpointInfo;
      EndpointGenerator::ConvertToEndpointDesc(ep, endpointInfo);
      endpointInfoList.push_back(endpointInfo);
    }
    HixlServerDesc server_desc{};
    server_desc.server_ip = "127.0.0.1";
    server_desc.server_port = kServerPort;
    server_desc.endpoint_list = endpointInfoList.data();
    server_desc.endpoint_list_num = static_cast<uint32_t>(endpointInfoList.size());
    HixlStatus ret = HixlCSServerCreate(&server_desc, &config, &server_handle_);
    if (ret != HIXL_SUCCESS) {
      std::cerr << "Failed to create CsServer" << std::endl;
      return FAILED;
    }
    std::cout << "success to create CsServer" << std::endl;
    return SUCCESS;
  }

  void RegMem(CommMem *mem_list, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      MemHandle mem_handle = nullptr;
      HixlStatus ret = HixlCSServerRegMem(server_handle_, std::to_string(i).c_str(), &mem_list[i], &mem_handle);
      if (ret != HIXL_SUCCESS) {
        std::cerr << "CsServer failed to RegMem" << std::endl;
        return;
      }
      mem_handles_.emplace_back(mem_handle);
    }
  }

  void ListenServer() {
    MsgProcessor send_endpoint_cb = [this](int32_t fd, const char *msg, uint64_t msg_len) -> Status {
      (void)msg;
      (void)msg_len;
      conn_fd_ = fd;
      SendResponse();
      return SUCCESS;
    };
    HixlStatus ret = HixlCSServerRegProc(server_handle_, CtrlMsgType::kGetEndpointInfoReq, send_endpoint_cb);
    if (ret != HIXL_SUCCESS) {
      std::cerr << "Failed to reg proc CsServer" << std::endl;
      return;
    }

    ret = HixlCSServerListen(server_handle_, kBackLog);
    if (ret != HIXL_SUCCESS) {
      std::cerr << "Failed to listen CsServer" << std::endl;
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(kSleepMs));
  }

  void DestroyServerAndUnreg() {
    if (server_handle_ == nullptr) {
      return;
    }

    for (auto mem_handle : mem_handles_) {
      HixlStatus ret = HixlCSServerUnregMem(server_handle_, mem_handle);
      if (ret != HIXL_SUCCESS) {
        std::cerr << "CsServer failed to UnregMem" << std::endl;
        return;
      }
    }
    // 清空内存句柄列表
    mem_handles_.clear();

    // 销毁服务器
    HixlStatus ret = HixlCSServerDestroy(server_handle_);
    server_handle_ = nullptr;
    if (ret != HIXL_SUCCESS) {
      std::cerr << "CsServer failed to Destroy" << std::endl;
      return;
    }
  }

 private:
  HixlServerHandle server_handle_ = nullptr;
  std::vector<MemHandle> mem_handles_{};
  int32_t conn_fd_ = -1;
  MockHixlServerMode mode_;

  // JSON字符串常量
  static const std::string kErrorJson;
  static const std::string kRoceEndpointJson;
  static const std::string kUbCtpHostEndpointJson;
  static const std::string kUbCtpDeviceEndpointJson;
  static const std::string kUbCtpPlaneAEndpointJson;
  static const std::string kUbTpPlaneBEndpointJson;
  static const std::string k2UbJson;
  static const std::string k4UbJson;
  static const std::string kNotArrayJson;
  static const std::string kMissingFieldJson;

  // 通用响应发送方法
  void SendResponseImpl(uint32_t magic, CtrlMsgType msg_type, size_t body_size, const std::string &json_content) {
    // 发送头部
    CtrlMsgHeader header{};
    header.magic = magic;
    header.body_size = body_size;
    CtrlMsgPlugin::Send(conn_fd_, &header, sizeof(header));

    // 发送消息体
    CtrlMsgPlugin::Send(conn_fd_, &msg_type, sizeof(msg_type));
    if (!json_content.empty()) {
      CtrlMsgPlugin::Send(conn_fd_, json_content.data(), json_content.size());
    }
  }

  void SendResponse() {
    switch (mode_) {
      case MockHixlServerMode::k4UbNormal:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndpointInfoResp, sizeof(CtrlMsgType) + k4UbJson.size(),
                         k4UbJson);
        break;
      case MockHixlServerMode::k2UbNormal:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndpointInfoResp, sizeof(CtrlMsgType) + k2UbJson.size(),
                         k2UbJson);
        break;
      case MockHixlServerMode::kGetEndpointInfoResp_BadMagic:
        SendResponseImpl(0xDEADBEEF, CtrlMsgType::kGetEndpointInfoResp, sizeof(CtrlMsgType) + k4UbJson.size(),
                         k4UbJson);
        break;
      case MockHixlServerMode::kGetEndpointInfoResp_BadMsgType:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kCreateChannelReq, sizeof(CtrlMsgType) + k4UbJson.size(), k4UbJson);
        break;
      case MockHixlServerMode::kGetEndpointInfoResp_BadBodySizeTooSmall:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndpointInfoResp, sizeof(CtrlMsgType) - 1, "");
        break;
      case MockHixlServerMode::kGetEndpointInfoResp_BadJson:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndpointInfoResp, sizeof(CtrlMsgType) + kErrorJson.size(),
                         kErrorJson);
        break;
      case MockHixlServerMode::kGetEndpointInfoResp_JsonIsNotArray:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndpointInfoResp, sizeof(CtrlMsgType) + kNotArrayJson.size(),
                         kNotArrayJson);
        break;
      case MockHixlServerMode::kGetEndpointInfoResp_MissingField:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndpointInfoResp,
                         sizeof(CtrlMsgType) + kMissingFieldJson.size(), kMissingFieldJson);
        break;
      default:
        SendResponseImpl(kMagicNumber, CtrlMsgType::kGetEndpointInfoResp, sizeof(CtrlMsgType) + k4UbJson.size(),
                         k4UbJson);
        break;
    }
  }
};

const std::string MockHixlServer::kErrorJson = R"({ invalid json )";
const std::string MockHixlServer::kRoceEndpointJson = R"({
      "protocol": "roce",
      "comm_id": "127.0.0.1",
      "dst_eid": "",
      "plane": "",
      "placement": "host",
      "net_instance_id": "superpod1-1"
    })";

const std::string MockHixlServer::kUbCtpHostEndpointJson = R"({
      "protocol": "ub_ctp",
      "comm_id": "000000000000000000000000c0a80463",
      "dst_eid" : "000000000000000000000000c0a80563",
      "plane": "",
      "placement" : "host",
      "net_instance_id" : "superpod1-1"
    })";

const std::string MockHixlServer::kUbCtpDeviceEndpointJson = R"({
      "protocol": "ub_ctp",
      "comm_id": "000000000000000000000000c0a80663",
      "dst_eid" : "000000000000000000000000c0a80763",
      "plane": "",
      "placement" : "device",
      "net_instance_id" : "superpod1-1"
    })";

const std::string MockHixlServer::kUbCtpPlaneAEndpointJson = R"({
      "protocol": "ub_ctp",
      "comm_id": "000000000000000000000000c0a80063",
      "dst_eid": "",
      "plane" : "plane-a",
      "placement" : "device",
      "net_instance_id" : "superpod1-1"
    })";

const std::string MockHixlServer::kUbTpPlaneBEndpointJson = R"({
      "protocol": "ub_tp",
      "comm_id": "000000000000000000000000c0a80163",
      "dst_eid": "",
      "plane" : "plane-b",
      "placement" : "host",
      "net_instance_id" : "superpod1-1"
    })";

// 使用原始字符串字面量构建k2UbJson和k4UbJson
const std::string MockHixlServer::k2UbJson =
    R"([)" + kRoceEndpointJson + R"(,)" + kUbCtpHostEndpointJson + R"(,)" + kUbCtpDeviceEndpointJson + R"(])";

const std::string MockHixlServer::k4UbJson = R"([)" + kRoceEndpointJson + R"(,)" + kUbCtpHostEndpointJson + R"(,)" +
                                             kUbCtpDeviceEndpointJson + R"(,)" + kUbCtpPlaneAEndpointJson + R"(,)" +
                                             kUbTpPlaneBEndpointJson + R"(])";
const std::string MockHixlServer::kNotArrayJson = R"(
    {
      "protocol": "roce",
      "comm_id": "127.0.0.1",
      "dst_eid": "",
      "plane": "",
      "placement": "device",
      "net_instance_id": "superpod1-1"
    }
  )";
const std::string MockHixlServer::kMissingFieldJson = R"([
    {
      "comm_id": "127.0.0.1",
      "dst_eid": "",
      "plane": "",
      "placement": "device",
      "net_instance_id": "superpod1-1"
    }
  ])";

class EnvGuard {
 public:
  EnvGuard(const char *key, const char *value) : key_(key) {
    mmSetEnv(key, value, 1);
  }
  ~EnvGuard() {
    unsetenv(key_.c_str());
  }

 private:
  const std::string key_;
};

// Use common KernelJsonMmpaStub from test_mmpa_utils.h
using ClientMmpaStub = hixl::test::KernelJsonMmpaStub;

class HixlClientUTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // EnsureDeviceKernelLoadedLocked 现在在初始化阶段调用，需要提前设置 MmpaStub
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<ClientMmpaStub>());
    ClientConfig config{};
    config.rdma_tc = kDefaultRdmaTc;
    config.rdma_sl = kDefaultRdmaSl;
    server_ = MakeUnique<MockHixlServer>();
    client_ = MakeUnique<HixlClient>("127.0.0.1", kServerPort, config);
  }

  void TearDown() override {
    client_->Finalize();
    server_->DestroyServerAndUnreg();
    llm::MmpaStub::GetInstance().Reset();
  }

  void StartServer(MockHixlServerMode mode) {
    server_->SetMode(mode);
    auto st = server_->CreateServer(Make4UbRemoteEpList());
    ASSERT_EQ(st, SUCCESS) << "Failed to start mock server";
    server_->RegMem(default_remote_mem_list, default_list_num);
    server_->ListenServer();
  }

  void StartServerReg4Ub(MockHixlServerMode mode) {
    server_->SetMode(mode);
    auto st = server_->CreateServer(Make4UbRemoteEpList());
    ASSERT_EQ(st, SUCCESS) << "Failed to start mock server";
    server_->RegMem(remote_mem_list_4ub, list_num_4ub);
    server_->ListenServer();
  }

  std::unique_ptr<HixlClient> client_;
  std::unique_ptr<MockHixlServer> server_;

  EndpointConfig MakeRoceHostLocalEp() {
    EndpointConfig ep{};
    ep.protocol = "roce";
    ep.comm_id = "127.0.0.1";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeRoceHostRemoteEp() {
    EndpointConfig ep{};
    ep.protocol = "roce";
    ep.comm_id = "127.0.0.1";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbHostLocalEp1() {
    EndpointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "000000000000000000000000c0a80563";
    ep.dst_eid = "000000000000000000000000c0a80463";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbHostRemoteEp1() {
    EndpointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "000000000000000000000000c0a80463";
    ep.dst_eid = "000000000000000000000000c0a80563";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbHostLocalEp2() {
    EndpointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "000000000000000000000000c0a80763";
    ep.dst_eid = "000000000000000000000000c0a80663";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbDeviceRemoteEp2() {
    EndpointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "000000000000000000000000c0a80663";
    ep.dst_eid = "000000000000000000000000c0a80763";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbDeviceLocalEp3() {
    EndpointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "000000000000000000000000c0a80363";
    ep.plane = "plane-a";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbDeviceRemoteEp3() {
    EndpointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "000000000000000000000000c0a80063";
    ep.plane = "plane-a";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbDeviceLocalEp4() {
    EndpointConfig ep{};
    ep.protocol = "ub_tp";
    ep.comm_id = "000000000000000000000000c0a80263";
    ep.plane = "plane-b";
    ep.placement = "device";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbHostRemoteEp4() {
    EndpointConfig ep{};
    ep.protocol = "ub_tp";
    ep.comm_id = "000000000000000000000000c0a80163";
    ep.plane = "plane-b";
    ep.placement = "host";
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  EndpointConfig MakeUbDiffNetLocalEp1() {
    EndpointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "000000000000000000000000c0a80563";
    ep.dst_eid = "000000000000000000000000c0a80463";
    ep.placement = "host";
    ep.net_instance_id = "superpod2-2";
    return ep;
  }

  EndpointConfig MakeUbDiffNetLocalEp2() {
    EndpointConfig ep{};
    ep.protocol = "ub_ctp";
    ep.comm_id = "000000000000000000000000c0a80763";
    ep.dst_eid = "000000000000000000000000c0a80663";
    ep.placement = "host";
    ep.net_instance_id = "superpod2-2";
    return ep;
  }

  EndpointConfig MakeRoceDiffNetLocalEp() {
    EndpointConfig ep{};
    ep.protocol = "roce";
    ep.comm_id = "127.0.0.1";
    ep.placement = "host";
    ep.net_instance_id = "superpod2-2";
    return ep;
  }

  std::vector<EndpointConfig> Make4UbRemoteEpList() {
    std::vector<EndpointConfig> ep_list;
    ep_list.push_back(MakeRoceHostRemoteEp());
    ep_list.push_back(MakeUbHostRemoteEp1());
    ep_list.push_back(MakeUbDeviceRemoteEp2());
    ep_list.push_back(MakeUbDeviceRemoteEp3());
    ep_list.push_back(MakeUbHostRemoteEp4());
    return ep_list;
  }

  std::vector<MemInfo> MakeMemInfoList() {
    std::vector<MemInfo> mem_info_list;
    // 添加DEVICE类型内存
    MemInfo device_mem;
    device_mem.mem_handle = nullptr;
    device_mem.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
    device_mem.mem.len = sizeof(uint32_t);
    device_mem.type = MEM_DEVICE;
    mem_info_list.push_back(device_mem);

    // 添加HOST类型内存
    MemInfo host_mem;
    host_mem.mem_handle = nullptr;
    host_mem.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[2]);
    host_mem.mem.len = sizeof(uint32_t);
    host_mem.type = MEM_HOST;
    mem_info_list.push_back(host_mem);
    return mem_info_list;
  }

  std::vector<MemInfo> Make4UbMemInfoList() {
    std::vector<MemInfo> mem_info_list;
    MemInfo mem1;
    mem1.mem_handle = nullptr;
    mem1.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[0]);
    mem1.mem.len = sizeof(uint32_t);
    mem1.type = MEM_DEVICE;
    mem_info_list.push_back(mem1);

    MemInfo mem2;
    mem2.mem_handle = nullptr;
    mem2.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[2]);
    mem2.mem.len = sizeof(uint32_t);
    mem2.type = MEM_DEVICE;
    mem_info_list.push_back(mem2);

    MemInfo mem3;
    mem3.mem_handle = nullptr;
    mem3.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[4]);
    mem3.mem.len = sizeof(uint32_t);
    mem3.type = MEM_HOST;
    mem_info_list.push_back(mem3);

    MemInfo mem4;
    mem4.mem_handle = nullptr;
    mem4.mem.addr = reinterpret_cast<uintptr_t>(&kLocalMems[6]);
    mem4.mem.len = sizeof(uint32_t);
    mem4.type = MEM_HOST;
    mem_info_list.push_back(mem4);
    return mem_info_list;
  }

  void InitializeBadJson(MockHixlServerMode bad_json_mode) {
    StartServer(bad_json_mode);
    std::vector<EndpointConfig> local_endpoint_list;
    local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
    Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
    EXPECT_EQ(st, PARAM_INVALID);
    st = client_->Finalize();
    EXPECT_EQ(st, SUCCESS);
    server_->DestroyServerAndUnreg();
  }

  bool WaitForTransferSyncInflight(std::atomic<bool> &transfer_done, uint32_t timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (client_->batch_cs_sync_inflight_.load(std::memory_order_acquire) > 0) {
        return true;
      }
      if (transfer_done.load(std::memory_order_acquire)) {
        return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(kInflightPollIntervalMs));
    }
    return client_->batch_cs_sync_inflight_.load(std::memory_order_acquire) > 0;
  }

  void SetupTransferTest(bool use_4ub = false) {
    if (use_4ub) {
      StartServerReg4Ub(MockHixlServerMode::k4UbNormal);
    } else {
      StartServer(MockHixlServerMode::k4UbNormal);
    }

    std::vector<EndpointConfig> local_endpoint_list;
    if (use_4ub) {
      local_endpoint_list.push_back(MakeRoceHostLocalEp());
      local_endpoint_list.push_back(MakeUbHostLocalEp1());
      local_endpoint_list.push_back(MakeUbHostLocalEp2());
      local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
      local_endpoint_list.push_back(MakeUbDeviceLocalEp4());
    } else {
      local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
    }

    Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
    EXPECT_EQ(st, SUCCESS);

    st = client_->SetLocalMemInfo(use_4ub ? Make4UbMemInfoList() : MakeMemInfoList());
    EXPECT_EQ(st, SUCCESS);

    st = client_->Connect(kDefaultTimeoutMs);
    EXPECT_EQ(st, SUCCESS);
  }

  // 创建单个传输操作
  TransferOpDesc CreateTransferOp(uint32_t index = 0, uint32_t *local_mem = &kLocalMems[0],
                                  uint32_t *remote_mem = &kRemoteMems[0]) {
    TransferOpDesc op_desc;
    op_desc.local_addr = reinterpret_cast<uintptr_t>(local_mem + index);
    op_desc.remote_addr = reinterpret_cast<uintptr_t>(remote_mem + index);
    op_desc.len = sizeof(uint32_t);
    return op_desc;
  }

  // 创建多个传输操作
  std::vector<TransferOpDesc> CreateTransferOps(size_t count = 1, uint32_t *local_mem = &kLocalMems[0],
                                                uint32_t *remote_mem = &kRemoteMems[0]) {
    std::vector<TransferOpDesc> op_descs;
    for (size_t i = 0; i < count; ++i) {
      op_descs.push_back(CreateTransferOp(i * 2, local_mem, remote_mem));
    }
    return op_descs;
  }

  // 创建异步传输请求
  TransferReq CreateAsyncTransfer(const std::vector<TransferOpDesc> &op_descs, TransferOp operation) {
    TransferReq req = nullptr;
    Status st = client_->TransferAsync(op_descs, operation, req);
    EXPECT_EQ(st, SUCCESS);
    EXPECT_NE(req, nullptr);
    return req;
  }

  void SetupUbHandlerWithSegments(UbClientHandler &handler) {
    handler.handles_[CommType::COMM_TYPE_UB_D2D] = reinterpret_cast<HixlClientHandle>(0x1000);
    handler.handles_[CommType::COMM_TYPE_UB_D2H] = reinterpret_cast<HixlClientHandle>(0x2000);
    handler.handles_[CommType::COMM_TYPE_UB_H2D] = reinterpret_cast<HixlClientHandle>(0x3000);
    handler.handles_[CommType::COMM_TYPE_UB_H2H] = reinterpret_cast<HixlClientHandle>(0x4000);

    auto dev_seg = std::make_shared<Segment>(MEM_DEVICE);
    EXPECT_EQ(dev_seg->AddRange(0x1000, 0x2000), SUCCESS);
    handler.local_segments_.push_back(dev_seg);
    handler.remote_segments_.push_back(std::make_shared<Segment>(MEM_DEVICE));
    EXPECT_EQ(handler.remote_segments_[0]->AddRange(0x3000, 0x2000), SUCCESS);

    auto host_seg = std::make_shared<Segment>(MEM_HOST);
    EXPECT_EQ(host_seg->AddRange(0x5000, 0x2000), SUCCESS);
    handler.local_segments_.push_back(host_seg);
    handler.remote_segments_.push_back(std::make_shared<Segment>(MEM_HOST));
    EXPECT_EQ(handler.remote_segments_[1]->AddRange(0x7000, 0x2000), SUCCESS);
  }

  void VerifyClassifyResult(const std::map<CommType, std::vector<TransferOpDesc>> &table, CommType type,
                            uintptr_t expected_addr) {
    ASSERT_EQ(table.at(type).size(), 1U);
    EXPECT_EQ(table.at(type)[0].local_addr, expected_addr);
  }

  // EndpointMatcher helpers
  static EndpointConfig MakeUbEp(const std::string &comm_id, const std::string &dst_eid, const std::string &placement,
                                 const std::string &plane = "") {
    EndpointConfig ep;
    ep.protocol = "ub_ctp";
    ep.comm_id = comm_id;
    ep.dst_eid = dst_eid;
    ep.plane = plane;
    ep.placement = placement;
    ep.net_instance_id = "superpod1-1";
    return ep;
  }

  static EndpointConfig MakeDirectEp(const std::string &protocol, const std::string &placement,
                                     const std::string &net_instance_id = "superpod1-1") {
    EndpointConfig ep;
    ep.protocol = protocol;
    ep.comm_id = protocol + "-" + placement;
    ep.placement = placement;
    ep.net_instance_id = net_instance_id;
    return ep;
  }

  void MatchAndVerify(const std::vector<EndpointConfig> &local, const std::vector<EndpointConfig> &remote,
                      size_t expected_pair_count, HandlerCreateArgs::HandlerType expected_type) {
    std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
    HandlerCreateArgs::HandlerType handler_type;
    Status st = EndpointMatcher::MatchEndpoints(local, remote, matched_pairs, handler_type);
    EXPECT_EQ(st, SUCCESS);
    EXPECT_EQ(handler_type, expected_type);
    ASSERT_EQ(matched_pairs.size(), expected_pair_count);
  }

  HandlerCreateArgs::EndpointPair MatchSingleDirectAndVerify(const std::vector<EndpointConfig> &local,
                                                             const std::vector<EndpointConfig> &remote,
                                                             CommType expected_type) {
    std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
    HandlerCreateArgs::HandlerType handler_type;
    Status st = EndpointMatcher::MatchEndpoints(local, remote, matched_pairs, handler_type);
    EXPECT_EQ(st, SUCCESS);
    EXPECT_EQ(handler_type, HandlerCreateArgs::HandlerType::DIRECT);
    EXPECT_EQ(matched_pairs.size(), 1U);
    if (matched_pairs.empty()) {
      return {};
    }
    EXPECT_EQ(matched_pairs[0].type, expected_type);
    return matched_pairs[0];
  }
};

// Initialize 接口测试：正常场景 创建 ub 链路4条
TEST_F(HixlClientUTest, Initialize4UBTest) {
  // 启动模拟服务端
  StartServer(MockHixlServerMode::k4UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbHostLocalEp2());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp4());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_GE(client_->ctrl_socket_, 0);
  EXPECT_EQ(client_->Finalize(), SUCCESS);
}

TEST_F(HixlClientUTest, InitializeKeepsControlSocket) {
  StartServer(MockHixlServerMode::k4UbNormal);
  ClientConfig config{};
  config.rdma_tc = kDefaultRdmaTc;
  config.rdma_sl = kDefaultRdmaSl;
  HixlClient client("127.0.0.1", kServerPort, config);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbHostLocalEp2());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp4());

  Status st = client.Initialize(local_endpoint_list, kDefaultTimeoutMs);

  EXPECT_EQ(st, SUCCESS);
  EXPECT_GE(client.ctrl_socket_, 0);
  EXPECT_EQ(client.Finalize(), SUCCESS);
}

// Initialize 接口测试：正常场景 创建 ub 链路2条
TEST_F(HixlClientUTest, Initialize2UBTest) {
  // 启动模拟服务端
  StartServer(MockHixlServerMode::k4UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbHostLocalEp2());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
}

// Initialize 接口测试：正常场景 创建 ub 链路1条
TEST_F(HixlClientUTest, Initialize1UBTest) {
  // 启动模拟服务端
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
}

// Initialize 接口测试：正常场景 环境变量设为1，不影响endpoint匹配策略
TEST_F(HixlClientUTest, InitializeEnvTest) {
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceHostLocalEp());
  local_endpoint_list.push_back(MakeUbHostLocalEp1());
  local_endpoint_list.push_back(MakeUbHostLocalEp2());
  {
    EnvGuard env_guard("HCCL_INTRA_ROCE_ENABLE", "1");
    Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
    EXPECT_EQ(st, SUCCESS);
    st = client_->Finalize();
    EXPECT_EQ(st, SUCCESS);
    server_->DestroyServerAndUnreg();
  }
}

// Initialize 接口测试：正常场景 两端不在同一节点，创建ROCE链路
TEST_F(HixlClientUTest, InitializeDiffNetTest) {
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  local_endpoint_list.push_back(MakeUbDiffNetLocalEp1());
  local_endpoint_list.push_back(MakeUbDiffNetLocalEp2());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Initialize 接口测试：异常场景 没有roce但必需roce(两端不在同一节点)
TEST_F(HixlClientUTest, InitializeNoRoceTest) {
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeUbDiffNetLocalEp1());
  local_endpoint_list.push_back(MakeUbDiffNetLocalEp2());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);  // 重构后 handler 创建失败返回 PARAM_INVALID
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Initialize 接口测试：异常场景 没有可配对的 endpoint
TEST_F(HixlClientUTest, InitializeNoPairTest) {
  StartServer(MockHixlServerMode::k2UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeDirectEp(kProtocolRoce, kPlacementDevice));
  local_endpoint_list.push_back(MakeUbDeviceLocalEp3());
  local_endpoint_list.push_back(MakeUbDeviceLocalEp4());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);  // 重构后 handler 创建失败返回 PARAM_INVALID
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Initialize 接口测试：异常场景 错误的magic响应
TEST_F(HixlClientUTest, InitializeBadMagicTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndpointInfoResp_BadMagic);
}

// Initialize 接口测试：异常场景 错误的msg_type响应
TEST_F(HixlClientUTest, InitializeBadMsgTypeTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndpointInfoResp_BadMsgType);
}

// Initialize 接口测试：异常场景 错误的body_size响应
TEST_F(HixlClientUTest, InitializeBadBodySizeTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndpointInfoResp_BadBodySizeTooSmall);
}

// Initialize 接口测试：异常场景 错误的json响应
TEST_F(HixlClientUTest, InitializeBadJsonTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndpointInfoResp_BadJson);
}

// Initialize 接口测试：异常场景 json 不是数组
TEST_F(HixlClientUTest, InitializeJsonIsNotArrayTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndpointInfoResp_JsonIsNotArray);
}

// Initialize 接口测试：异常场景 json 缺少字段
TEST_F(HixlClientUTest, InitializeJsonMissingFieldTest) {
  InitializeBadJson(MockHixlServerMode::kGetEndpointInfoResp_MissingField);
}

// SetLocalMemInfo 接口测试：正常场景
TEST_F(HixlClientUTest, SetLocalMemInfoTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  std::vector<MemInfo> mem_info_list = MakeMemInfoList();
  st = client_->SetLocalMemInfo(mem_info_list);
  EXPECT_EQ(st, SUCCESS);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Connect 接口测试：正常场景 - 成功连接并获取远程内存信息
TEST_F(HixlClientUTest, ConnectSuccessTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 初始化 roce 链路
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  // 调用 Connect 方法
  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// Connect 接口测试：异常场景 - 未初始化
TEST_F(HixlClientUTest, ConnectNotInitializedTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  // 未初始化客户端
  Status st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, FAILED);
  st = client_->Finalize();
  EXPECT_EQ(st, SUCCESS);
  server_->DestroyServerAndUnreg();
}

// TransferSync 接口测试：正常场景 - roce
TEST_F(HixlClientUTest, TransferSyncSuccessTest) {
  SetupTransferTest();
  auto op_descs = CreateTransferOps();
  Status st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  std::cout << kRemoteMems[0] << std::endl;

  st = client_->TransferSync(op_descs, READ, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
  std::cout << kLocalMems[0] << std::endl;
}

// TransferSync 接口测试：正常场景 - UB协议传输 (D2D, D2H, H2D, H2H)
TEST_F(HixlClientUTest, TransferSyncSuccessWithUbTest) {
  SetupTransferTest(true);  // use_4ub = true, 启用 UB 协议传输
  auto op_descs = CreateTransferOps(4, &kLocalMems[0], &kRemoteMems[0]);

  // WRITE 操作：测试 UB 协议的 D2D, D2H, H2D, H2H 传输
  Status st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);

  // READ 操作：测试 UB 协议的反向传输
  st = client_->TransferSync(op_descs, READ, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);
}

// TransferAsync 接口测试：正常场景 - UB协议异步传输
TEST_F(HixlClientUTest, TransferAsyncSuccessWithUbTest) {
  SetupTransferTest(true);
  auto op_descs = CreateTransferOps(4, &kLocalMems[0], &kRemoteMems[0]);
  CreateAsyncTransfer(op_descs, WRITE);
}

// TransferSync 接口测试：异常场景 - 未建链
TEST_F(HixlClientUTest, TransferSyncNoConnectTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);

  st = client_->SetLocalMemInfo(MakeMemInfoList());
  EXPECT_EQ(st, SUCCESS);

  auto op_descs = CreateTransferOps();
  st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, NOT_CONNECTED);
}

// TransferSync 接口测试：异常场景 - 未SetLocalMemInfo
TEST_F(HixlClientUTest, TransferSyncNoSetLocalMemInfoTest) {
  StartServer(MockHixlServerMode::k4UbNormal);
  std::vector<EndpointConfig> local_endpoint_list;
  local_endpoint_list.push_back(MakeRoceDiffNetLocalEp());
  Status st = client_->Initialize(local_endpoint_list, kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);

  st = client_->Connect(kDefaultTimeoutMs);
  EXPECT_EQ(st, SUCCESS);

  auto op_descs = CreateTransferOps();
  st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);
}

// TransferSync 接口测试：异常场景 - 空的op_descs列表
TEST_F(HixlClientUTest, TransferSyncEmptyOpDescsTest) {
  SetupTransferTest();
  std::vector<TransferOpDesc> op_descs;
  Status st = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);
}

// TransferSync 接口测试：异常场景 - 内存未注册
TEST_F(HixlClientUTest, TransferSyncMemMismatchTest) {
  SetupTransferTest();
  std::vector<TransferOpDesc> op_descs1;
  uint32_t tmp = 10;
  auto op_desc = CreateTransferOp(0, &tmp, &kRemoteMems[0]);
  op_descs1.push_back(op_desc);

  Status st = client_->TransferSync(op_descs1, WRITE, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);

  std::vector<TransferOpDesc> op_descs2;
  op_desc = CreateTransferOp(0, &kLocalMems[0], &tmp);
  op_descs2.push_back(op_desc);

  st = client_->TransferSync(op_descs2, READ, kDefaultTimeoutMs);
  EXPECT_EQ(st, PARAM_INVALID);
}

// TransferSync 接口测试：异常场景 - 传输超时
TEST_F(HixlClientUTest, TransferSyncTimeoutTest) {
  SetupTransferTest();

  auto op_descs = CreateTransferOps();
  // 0ms 预算：BatchTransferSync 在发起传输前即判定超时，避免桩快速完成导致无法覆盖 TIMEOUT 路径
  Status st = client_->TransferSync(op_descs, WRITE, 0U);
  EXPECT_EQ(st, TIMEOUT);
}

// TransferSync 接口测试：Finalize 在同步传输未结束时调用会阻塞，直至传输完成后再断链销毁
TEST_F(HixlClientUTest, TransferSyncFinalizeWaitsForSyncCompleteTest) {
  SetupTransferTest();
  auto op_descs = CreateTransferOps();

  std::atomic<Status> transfer_status = SUCCESS;
  std::atomic<bool> transfer_done = false;
  std::thread transfer_thread([&]() {
    transfer_status = client_->TransferSync(op_descs, WRITE, kDefaultTimeoutMs);
    transfer_done.store(true, std::memory_order_release);
  });

  const bool sync_inflight = WaitForTransferSyncInflight(transfer_done, kDefaultTimeoutMs);
  if (sync_inflight) {
    EXPECT_EQ(client_->Finalize(), SUCCESS);
  }

  if (transfer_thread.joinable()) {
    transfer_thread.join();
  }

  EXPECT_EQ(transfer_status.load(), SUCCESS);
  if (!sync_inflight) {
    EXPECT_EQ(client_->Finalize(), SUCCESS);
  }
}

// TransferAsync 接口测试：正常场景
TEST_F(HixlClientUTest, TransferAsyncSuccessTest) {
  SetupTransferTest();
  auto op_descs = CreateTransferOps();
  CreateAsyncTransfer(op_descs, WRITE);
}

// GetTransferStatus 接口测试：正常场景 - WAITING 和 COMPLETED
TEST_F(HixlClientUTest, GetTransferStatusSuccessTest) {
  SetupTransferTest();
  auto op_descs = CreateTransferOps();
  auto req = CreateAsyncTransfer(op_descs, WRITE);
  TransferStatus status = TransferStatus::TIMEOUT;
  Status st = client_->GetTransferStatus(req, status);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(status, TransferStatus::COMPLETED);
  std::cout << "TransferStatus: " << static_cast<int>(status) << std::endl;
}

// GetTransferStatus 接口测试：异常场景 - 未传输
TEST_F(HixlClientUTest, GetTransferStatusNoTransferTest) {
  SetupTransferTest();
  TransferReq req = nullptr;
  TransferStatus status;
  Status st = client_->GetTransferStatus(req, status);
  EXPECT_EQ(st, FAILED);
}

// GetTransferStatus 接口测试：异常场景 - req不对
TEST_F(HixlClientUTest, GetTransferStatusReqInvalidTest) {
  SetupTransferTest();
  auto op_descs = CreateTransferOps();
  auto req = CreateAsyncTransfer(op_descs, WRITE);
  TransferStatus status = TransferStatus::TIMEOUT;
  Status st = client_->GetTransferStatus(static_cast<void *>(static_cast<char *>(req) + 1), status);
  EXPECT_EQ(st, PARAM_INVALID);
  std::cout << "TransferStatus: " << static_cast<int>(status) << std::endl;
}

// Segment::AddRange 函数测试：正常场景 - 添加单个内存范围
TEST_F(HixlClientUTest, SegmentAddRangeSuccessTest) {
  Segment segment(MemType::MEM_DEVICE);

  // 测试添加单个内存范围
  Status st = segment.AddRange(0x1000, 0x100);
  EXPECT_EQ(st, SUCCESS);

  // 验证范围是否被正确添加
  EXPECT_TRUE(segment.Contains(0x1000, 0x1100));
}

// Segment::AddRange 函数测试：异常场景 - 地址溢出
TEST_F(HixlClientUTest, SegmentAddRangeOverflowTest) {
  Segment segment(MemType::MEM_DEVICE);

  // 测试添加导致地址溢出的内存范围
  uint64_t start = UINT64_MAX - 0x100;
  uint64_t len = 0x200;
  Status st = segment.AddRange(start, len);
  EXPECT_EQ(st, PARAM_INVALID);
}

TEST_F(HixlClientUTest, DeserializePreservesDeviceInfoAfterSerializeRoundTrip) {
  std::vector<EndpointConfig> input_list = {endpoint_test::BuildSampleDeviceRoceEndpoint()};

  std::string msg_str;
  EXPECT_EQ(EndpointGenerator::SerializeEndpointConfigList(input_list, msg_str), SUCCESS);

  std::vector<EndpointConfig> output_list;
  EXPECT_EQ(EndpointGenerator::DeserializeEndpointConfigList(msg_str, output_list), SUCCESS);
  ASSERT_EQ(output_list.size(), 1U);

  const auto &out = output_list[0];
  EXPECT_EQ(out.protocol, kProtocolRoce);
  EXPECT_EQ(out.comm_id, "127.0.0.1");
  EXPECT_EQ(out.placement, kPlacementDevice);
  EXPECT_EQ(out.plane, "plane-a");
  EXPECT_EQ(out.dst_eid, "00010002000300040005000600070008");
  EXPECT_EQ(out.net_instance_id, "superpod_1");
  EXPECT_EQ(out.device_info.phy_device_id, 3);
  EXPECT_EQ(out.device_info.super_device_id, 7);
  EXPECT_EQ(out.device_info.super_pod_id, 9);
}

TEST_F(HixlClientUTest, DeserializeOldFormatWithoutDeviceInfoSuccess) {
  const std::string json_str = endpoint_test::BuildLegacyEndpointListJson();

  std::vector<EndpointConfig> endpoint_list;
  EXPECT_EQ(EndpointGenerator::DeserializeEndpointConfigList(json_str, endpoint_list), SUCCESS);
  ASSERT_EQ(endpoint_list.size(), 1U);

  const auto &ep = endpoint_list[0];
  EXPECT_EQ(ep.protocol, kProtocolRoce);
  EXPECT_EQ(ep.comm_id, "127.0.0.1");
  EXPECT_EQ(ep.placement, kPlacementDevice);
  EXPECT_EQ(ep.plane, "plane-a");
  EXPECT_EQ(ep.dst_eid, "00010002000300040005000600070008");
  EXPECT_EQ(ep.net_instance_id, "superpod_legacy");
  EXPECT_EQ(ep.device_info.phy_device_id, -1);
  EXPECT_EQ(ep.device_info.super_device_id, -1);
  EXPECT_EQ(ep.device_info.super_pod_id, -1);
}

TEST_F(HixlClientUTest, DirectClientHandlerSingleHandle) {
  auto handle = reinterpret_cast<HixlClientHandle>(0x1234);
  DirectClientHandler handler(handle);
  EXPECT_NE(&handler, nullptr);
}

TEST_F(HixlClientUTest, UbHandlerClassifyD2D) {
  UbClientHandler handler({});
  SetupUbHandlerWithSegments(handler);

  TransferOpDesc op{0x1100, 0x3100, 0x100};
  std::map<CommType, std::vector<TransferOpDesc>> table;
  EXPECT_EQ(handler.ClassifyTransfers({op}, table), SUCCESS);
  VerifyClassifyResult(table, CommType::COMM_TYPE_UB_D2D, 0x1100U);

  handler.local_segments_.clear();
  handler.remote_segments_.clear();
  handler.handles_.clear();
}

TEST_F(HixlClientUTest, UbHandlerClassifyD2H) {
  UbClientHandler handler({});
  SetupUbHandlerWithSegments(handler);

  TransferOpDesc op{0x1100, 0x7100, 0x100};
  std::map<CommType, std::vector<TransferOpDesc>> table;
  EXPECT_EQ(handler.ClassifyTransfers({op}, table), SUCCESS);
  VerifyClassifyResult(table, CommType::COMM_TYPE_UB_D2H, 0x1100U);

  handler.local_segments_.clear();
  handler.remote_segments_.clear();
  handler.handles_.clear();
}

TEST_F(HixlClientUTest, UbHandlerClassifyH2D) {
  UbClientHandler handler({});
  SetupUbHandlerWithSegments(handler);

  TransferOpDesc op{0x5100, 0x3100, 0x100};
  std::map<CommType, std::vector<TransferOpDesc>> table;
  EXPECT_EQ(handler.ClassifyTransfers({op}, table), SUCCESS);
  VerifyClassifyResult(table, CommType::COMM_TYPE_UB_H2D, 0x5100U);

  handler.local_segments_.clear();
  handler.remote_segments_.clear();
  handler.handles_.clear();
}

TEST_F(HixlClientUTest, UbHandlerClassifyH2H) {
  UbClientHandler handler({});
  SetupUbHandlerWithSegments(handler);

  TransferOpDesc op{0x5100, 0x7100, 0x100};
  std::map<CommType, std::vector<TransferOpDesc>> table;
  EXPECT_EQ(handler.ClassifyTransfers({op}, table), SUCCESS);
  VerifyClassifyResult(table, CommType::COMM_TYPE_UB_H2H, 0x5100U);

  handler.local_segments_.clear();
  handler.remote_segments_.clear();
  handler.handles_.clear();
}

// 排序优先级：dst_eid非空的local先于空的尝试匹配，防止通配endpoint占用特定endpoint的匹配槽位
TEST_F(HixlClientUTest, EndpointMatcherDstEidPriorityTest) {
  std::vector<EndpointConfig> remote = {MakeUbEp("remote_1", "eid_x", "device"),
                                        MakeUbEp("remote_2", "", "device", "default")};
  // 通配(L2)在前, 精确(L1)在后, 验证sort将其纠正为精确endpoint优先
  std::vector<EndpointConfig> local = {MakeUbEp("wildcard", "", "device", "default"),
                                       MakeUbEp("eid_x", "remote_1", "device")};

  std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
  HandlerCreateArgs::HandlerType handler_type;
  Status st = EndpointMatcher::MatchEndpoints(local, remote, matched_pairs, handler_type);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(handler_type, HandlerCreateArgs::HandlerType::UB);
  // L1(dst_eid非空)匹配R1, D2D槽位被占用后L2无法再匹配; 若排序失效L2会先抢占导致L1落空
  ASSERT_EQ(matched_pairs.size(), 1U);
  EXPECT_EQ(matched_pairs[0].local.comm_id, "eid_x");
  EXPECT_EQ(matched_pairs[0].local.dst_eid, "remote_1");
  EXPECT_EQ(matched_pairs[0].remote.comm_id, "remote_1");
  EXPECT_EQ(matched_pairs[0].remote.dst_eid, "eid_x");
  EXPECT_EQ(matched_pairs[0].type, CommType::COMM_TYPE_UB_D2D);
}

// 所有local dst_eid为空时排序是no-op，匹配不受影响
TEST_F(HixlClientUTest, EndpointMatcherAllDstEidEmptyTest) {
  std::vector<EndpointConfig> remote = {MakeUbEp("remote_1", "", "device", "default"),
                                        MakeUbEp("remote_2", "", "host", "default")};
  std::vector<EndpointConfig> local = {MakeUbEp("local_1", "", "device", "default"),
                                       MakeUbEp("local_2", "", "host", "default")};
  MatchAndVerify(local, remote, 4U, HandlerCreateArgs::HandlerType::UB);
}

// 所有local dst_eid都非空时排序也不影响匹配结果
TEST_F(HixlClientUTest, EndpointMatcherAllDstEidNonEmptyTest) {
  std::vector<EndpointConfig> remote = {MakeUbEp("remote_1", "l1_eid", "device"),
                                        MakeUbEp("remote_2", "l2_eid", "host")};
  std::vector<EndpointConfig> local = {MakeUbEp("l1_eid", "remote_1", "device"),
                                       MakeUbEp("l2_eid", "remote_2", "host")};
  MatchAndVerify(local, remote, 2U, HandlerCreateArgs::HandlerType::UB);
}

TEST_F(HixlClientUTest, EndpointMatcherCrossInstancePrefersDeviceUboe) {
  std::vector<EndpointConfig> local = {MakeDirectEp(kProtocolRoce, kPlacementDevice, "superpod2-2"),
                                       MakeDirectEp(kProtocolUboe, kPlacementDevice, "superpod2-2"),
                                       MakeUbEp("local_1", "", "device", "default")};
  std::vector<EndpointConfig> remote = {MakeDirectEp(kProtocolRoce, kPlacementDevice),
                                        MakeUbEp("remote_1", "", "device", "default"),
                                        MakeDirectEp(kProtocolUboe, kPlacementDevice)};

  std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
  HandlerCreateArgs::HandlerType handler_type;
  Status st = EndpointMatcher::MatchEndpoints(local, remote, matched_pairs, handler_type);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(handler_type, HandlerCreateArgs::HandlerType::DIRECT);
  ASSERT_EQ(matched_pairs.size(), 1U);
  EXPECT_EQ(matched_pairs[0].type, CommType::COMM_TYPE_UBOE);
  EXPECT_EQ(matched_pairs[0].local.protocol, kProtocolUboe);
  EXPECT_EQ(matched_pairs[0].local.placement, kPlacementDevice);
}

TEST_F(HixlClientUTest, EndpointMatcherCrossInstanceFallsBackToDeviceRoce) {
  std::vector<EndpointConfig> local = {MakeDirectEp(kProtocolRoce, kPlacementDevice, "superpod2-2"),
                                       MakeDirectEp(kProtocolRoce, kPlacementHost, "superpod2-2")};
  std::vector<EndpointConfig> remote = {MakeDirectEp(kProtocolRoce, kPlacementDevice),
                                        MakeDirectEp(kProtocolRoce, kPlacementHost)};

  auto matched_pair = MatchSingleDirectAndVerify(local, remote, CommType::COMM_TYPE_ROCE);
  EXPECT_EQ(matched_pair.local.placement, kPlacementDevice);
}

TEST_F(HixlClientUTest, EndpointMatcherCrossInstanceFallsBackToHostRoce) {
  std::vector<EndpointConfig> local = {MakeDirectEp(kProtocolRoce, kPlacementHost, "superpod2-2")};
  std::vector<EndpointConfig> remote = {MakeDirectEp(kProtocolRoce, kPlacementHost)};

  auto matched_pair = MatchSingleDirectAndVerify(local, remote, CommType::COMM_TYPE_ROCE);
  EXPECT_EQ(matched_pair.local.placement, kPlacementHost);
}

TEST_F(HixlClientUTest, EndpointMatcherSameInstanceUbPreemptsDirectPriority) {
  std::vector<EndpointConfig> local = {MakeUbEp("local_1", "", "device", "default"),
                                       MakeUbEp("local_2", "", "host", "default"),
                                       MakeDirectEp(kProtocolHccs, kPlacementDevice),
                                       MakeDirectEp(kProtocolUboe, kPlacementDevice)};
  std::vector<EndpointConfig> remote = {MakeUbEp("remote_1", "", "device", "default"),
                                        MakeUbEp("remote_2", "", "host", "default"),
                                        MakeDirectEp(kProtocolHccs, kPlacementDevice),
                                        MakeDirectEp(kProtocolUboe, kPlacementDevice)};

  std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
  HandlerCreateArgs::HandlerType handler_type;
  Status st = EndpointMatcher::MatchEndpoints(local, remote, matched_pairs, handler_type);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(handler_type, HandlerCreateArgs::HandlerType::UB);
  ASSERT_EQ(matched_pairs.size(), 4U);
}

TEST_F(HixlClientUTest, EndpointMatcherSameInstanceDirectPriorityStartsFromDeviceHccs) {
  std::vector<EndpointConfig> local = {MakeDirectEp(kProtocolHccs, kPlacementDevice),
                                       MakeDirectEp(kProtocolUboe, kPlacementDevice),
                                       MakeDirectEp(kProtocolRoce, kPlacementDevice)};
  std::vector<EndpointConfig> remote = {MakeDirectEp(kProtocolHccs, kPlacementDevice),
                                        MakeDirectEp(kProtocolUboe, kPlacementDevice),
                                        MakeDirectEp(kProtocolRoce, kPlacementDevice)};

  (void)MatchSingleDirectAndVerify(local, remote, CommType::COMM_TYPE_HCCS);
}

TEST_F(HixlClientUTest, EndpointMatcherSameInstanceDirectPriorityFallsBackToDeviceUboe) {
  std::vector<EndpointConfig> local = {MakeDirectEp(kProtocolUboe, kPlacementDevice),
                                       MakeDirectEp(kProtocolRoce, kPlacementDevice)};
  std::vector<EndpointConfig> remote = {MakeDirectEp(kProtocolUboe, kPlacementDevice),
                                        MakeDirectEp(kProtocolRoce, kPlacementDevice)};

  (void)MatchSingleDirectAndVerify(local, remote, CommType::COMM_TYPE_UBOE);
}

TEST_F(HixlClientUTest, EndpointMatcherSameInstanceDirectPriorityFallsBackToDeviceRoce) {
  std::vector<EndpointConfig> local = {MakeDirectEp(kProtocolRoce, kPlacementDevice),
                                       MakeDirectEp(kProtocolRoce, kPlacementHost)};
  std::vector<EndpointConfig> remote = {MakeDirectEp(kProtocolRoce, kPlacementDevice),
                                        MakeDirectEp(kProtocolRoce, kPlacementHost)};

  auto matched_pair = MatchSingleDirectAndVerify(local, remote, CommType::COMM_TYPE_ROCE);
  EXPECT_EQ(matched_pair.local.placement, kPlacementDevice);
}

TEST_F(HixlClientUTest, EndpointMatcherSameInstanceDirectPriorityFallsBackToHostRoce) {
  std::vector<EndpointConfig> local = {MakeDirectEp(kProtocolRoce, kPlacementHost)};
  std::vector<EndpointConfig> remote = {MakeDirectEp(kProtocolRoce, kPlacementHost)};

  auto matched_pair = MatchSingleDirectAndVerify(local, remote, CommType::COMM_TYPE_ROCE);
  EXPECT_EQ(matched_pair.local.placement, kPlacementHost);
}

TEST_F(HixlClientUTest, EndpointMatcherDirectMatchRequiresSamePlacement) {
  std::vector<EndpointConfig> local = {MakeDirectEp(kProtocolRoce, kPlacementDevice)};
  std::vector<EndpointConfig> remote = {MakeDirectEp(kProtocolRoce, kPlacementHost)};

  std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
  HandlerCreateArgs::HandlerType handler_type;
  Status st = EndpointMatcher::MatchEndpoints(local, remote, matched_pairs, handler_type);
  EXPECT_EQ(st, PARAM_INVALID);
  EXPECT_TRUE(matched_pairs.empty());
}

TEST_F(HixlClientUTest, EndpointMatcherIgnoresIntraRoceEnv) {
  std::vector<EndpointConfig> local = {MakeUbEp("local_1", "", "device", "default"),
                                       MakeDirectEp(kProtocolRoce, kPlacementDevice)};
  std::vector<EndpointConfig> remote = {MakeUbEp("remote_1", "", "device", "default"),
                                        MakeDirectEp(kProtocolRoce, kPlacementDevice)};

  EnvGuard env_guard("HCCL_INTRA_ROCE_ENABLE", "1");
  std::vector<HandlerCreateArgs::EndpointPair> matched_pairs;
  HandlerCreateArgs::HandlerType handler_type;
  Status st = EndpointMatcher::MatchEndpoints(local, remote, matched_pairs, handler_type);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(handler_type, HandlerCreateArgs::HandlerType::UB);
  ASSERT_EQ(matched_pairs.size(), 1U);
  EXPECT_EQ(matched_pairs[0].type, CommType::COMM_TYPE_UB_D2D);
}

TEST_F(HixlClientUTest, CheckAliveWritesControlSocket) {
  ClientConfig config{};
  config.remote_engine = "127.0.0.1:16001";
  HixlClient client("127.0.0.1", kServerPort, config);
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  client.ctrl_socket_ = fds[0];

  Status ret = client.CheckAlive();
  EXPECT_EQ(ret, SUCCESS);

  CtrlMsgHeader header{};
  ASSERT_EQ(read(fds[1], &header, sizeof(header)), static_cast<ssize_t>(sizeof(header)));
  EXPECT_EQ(header.magic, kMagicNumber);
  EXPECT_EQ(header.body_size, sizeof(CtrlMsgType));
  CtrlMsgType msg_type{};
  ASSERT_EQ(read(fds[1], &msg_type, sizeof(msg_type)), static_cast<ssize_t>(sizeof(msg_type)));
  EXPECT_EQ(msg_type, CtrlMsgType::kHeartBeat);

  EXPECT_EQ(client.Finalize(), SUCCESS);
  close(fds[1]);
}

TEST_F(HixlClientUTest, CheckAliveBrokenPipeReturnsFailed) {
  CtrlMsgPlugin::Initialize();
  ClientConfig config{};
  config.remote_engine = "127.0.0.1:16001";
  HixlClient client("127.0.0.1", kServerPort, config);
  int32_t fds[2] = {-1, -1};
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  client.ctrl_socket_ = fds[0];
  close(fds[1]);

  Status ret = client.CheckAlive();
  EXPECT_EQ(ret, FAILED);
  EXPECT_EQ(client.ctrl_socket_, -1);
}

TEST_F(HixlClientUTest, CheckAliveInvalidControlSocketFails) {
  ClientConfig config{};
  config.remote_engine = "127.0.0.1:16001";
  HixlClient client("127.0.0.1", kServerPort, config);

  Status ret = client.CheckAlive();
  EXPECT_EQ(ret, FAILED);
}

}  // namespace hixl
