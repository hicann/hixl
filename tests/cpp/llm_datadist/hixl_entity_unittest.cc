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
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "cache_mgr/comm_mem_manager.h"
#include "common/ctrl_msg.h"
#include "engine/engine.h"
#include "hixl/hixl_types.h"
#include "link_mgr/hixl_entity.h"
#if __has_include("common/tls/tls_manager.h")
#include "common/tls/tls_manager.h"
#define HIXL_ENTITY_TEST_HAS_TLS_MANAGER 1
#endif

namespace llm {
namespace {
class MockMemTransferEngine : public TransferEngine {
 public:
  MockMemTransferEngine() : TransferEngine(0U) {}

  ge::Status Initialize(const std::map<ge::AscendString, ge::AscendString> &) override {
    return ge::SUCCESS;
  }

  void Finalize() override {}

  ge::Status RegisterMem(void *addr, uint64_t, CommMemType, void *&handle) override {
    handle = addr;
    return ge::SUCCESS;
  }

  ge::Status UnregisterMem(void *) override {
    return ge::SUCCESS;
  }

  ge::Status LinkClusters(const std::vector<ClusterInfo> &, std::vector<ge::Status> &, int32_t) override {
    return ge::SUCCESS;
  }

  ge::Status UnlinkClusters(const std::vector<ClusterInfo> &, std::vector<ge::Status> &, int32_t, bool) override {
    return ge::SUCCESS;
  }

  ge::Status Link(std::string &, const std::map<uint64_t, uint32_t> &, std::string &, uint64_t &) override {
    return ge::SUCCESS;
  }

  ge::Status Unlink(uint64_t) override {
    return ge::SUCCESS;
  }

  void UnlinkAllClusters() override {}

  ge::Status QueryRegisterMemStatus(uint64_t, RegisterMemoryStatus &) override {
    return ge::SUCCESS;
  }

  ge::Status SwitchRole(const std::string &, const std::map<std::string, std::string> &) override {
    return ge::SUCCESS;
  }
};

class MockHixlEngine : public hixl::Engine {
 public:
  MockHixlEngine() : hixl::Engine(hixl::AscendString("127.0.0.1")) {}

  hixl::Status Initialize(const hixl::HixlOptions &) override {
    return hixl::SUCCESS;
  }

  void Finalize() override {}

  bool IsInitialized() const override {
    return true;
  }

  hixl::Status RegisterMem(const hixl::MemDesc &mem, hixl::MemType, hixl::MemHandle &mem_handle) override {
    mem_handle = reinterpret_cast<hixl::MemHandle>(mem.addr);
    return hixl::SUCCESS;
  }

  hixl::Status DeregisterMem(hixl::MemHandle) override {
    return hixl::SUCCESS;
  }

  hixl::Status Connect(const hixl::AscendString &, int32_t) override {
    connected_ = true;
    return hixl::SUCCESS;
  }

  hixl::Status Disconnect(const hixl::AscendString &, int32_t) override {
    connected_ = false;
    ++disconnect_count_;
    return hixl::SUCCESS;
  }

  void Disconnect() override {
    connected_ = false;
  }

  hixl::Status TransferSync(const hixl::AscendString &, hixl::TransferOp, const std::vector<hixl::TransferOpDesc> &,
                            int32_t) override {
    return hixl::SUCCESS;
  }

  hixl::Status TransferAsync(const hixl::AscendString &, hixl::TransferOp, const std::vector<hixl::TransferOpDesc> &,
                             const hixl::TransferArgs &, hixl::TransferReq &req) override {
    req = nullptr;
    return hixl::SUCCESS;
  }

  hixl::Status GetTransferStatus(const hixl::TransferReq &, hixl::TransferStatus &status) override {
    status = hixl::TransferStatus::COMPLETED;
    return hixl::SUCCESS;
  }

  hixl::Status GetTransferStatus(const hixl::GetTransferStatusArgs &,
                                 std::vector<hixl::TransferResult> &results) override {
    results.clear();
    return hixl::SUCCESS;
  }

  hixl::Status SendNotify(const hixl::AscendString &, const hixl::NotifyDesc &, int32_t) override {
    return hixl::SUCCESS;
  }

  hixl::Status GetNotifies(std::vector<hixl::NotifyDesc> &notifies) override {
    notifies.clear();
    return hixl::SUCCESS;
  }

  hixl::Status RegisterCallbackProcessor(int32_t, hixl::CallbackProcessor) override {
    return hixl::SUCCESS;
  }

  bool IsConnected() const {
    return connected_;
  }

  int32_t DisconnectCount() const {
    return disconnect_count_.load();
  }

 private:
  std::atomic<bool> connected_{false};
  std::atomic<int32_t> disconnect_count_{0};
};

class FailingControlServer {
 public:
  explicit FailingControlServer(bool respond_successfully = false) : respond_successfully_(respond_successfully) {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      return;
    }
    int32_t reuse = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0U);
    if (bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(listen_fd_, 1) != 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }
    socklen_t address_size = sizeof(address);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr *>(&address), &address_size) != 0) {
      close(listen_fd_);
      listen_fd_ = -1;
      return;
    }
    port_ = ntohs(address.sin_port);
    timeval timeout{2, 0};
    (void)setsockopt(listen_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    thread_ = std::thread([this]() {
      const int32_t client_fd = accept(listen_fd_, nullptr, nullptr);
      if (client_fd >= 0) {
        if (respond_successfully_) {
          hixl::CtrlMsgHeader request_header{};
          hixl::CtrlMsgType request_type{};
          if (Receive(client_fd, &request_header, sizeof(request_header)) &&
              Receive(client_fd, &request_type, sizeof(request_type))) {
            hixl::CtrlMsgHeader response_header{};
            response_header.magic = hixl::kMagicNumber;
            response_header.body_size = sizeof(hixl::CtrlMsgType) + sizeof(CacheTableInfo);
            const auto response_type = hixl::CtrlMsgType::kGetCacheTableResp;
            CacheTableInfo cache_table_info{};
            (void)Send(client_fd, &response_header, sizeof(response_header));
            (void)Send(client_fd, &response_type, sizeof(response_type));
            (void)Send(client_fd, &cache_table_info, sizeof(cache_table_info));
          }
        } else {
          (void)shutdown(client_fd, SHUT_RDWR);
        }
        close(client_fd);
      }
    });
  }

  ~FailingControlServer() {
    if (listen_fd_ >= 0) {
      shutdown(listen_fd_, SHUT_RDWR);
      close(listen_fd_);
    }
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  bool IsReady() const {
    return listen_fd_ >= 0;
  }

  uint32_t Port() const {
    return port_;
  }

 private:
  static bool Receive(int32_t fd, void *buffer, size_t size) {
    auto *data = static_cast<char *>(buffer);
    size_t received = 0U;
    while (received < size) {
      const auto count = recv(fd, data + received, size - received, 0);
      if (count <= 0) {
        return false;
      }
      received += static_cast<size_t>(count);
    }
    return true;
  }

  static bool Send(int32_t fd, const void *buffer, size_t size) {
    const auto *data = static_cast<const char *>(buffer);
    size_t sent = 0U;
    while (sent < size) {
      const auto count = send(fd, data + sent, size - sent, 0);
      if (count <= 0) {
        return false;
      }
      sent += static_cast<size_t>(count);
    }
    return true;
  }

  int32_t listen_fd_ = -1;
  uint32_t port_ = 0U;
  bool respond_successfully_ = false;
  std::thread thread_;
};
}  // namespace

TEST(HixlEntityTest, DisconnectsEngineWhenControlHandshakeFails) {
#if HIXL_ENTITY_TEST_HAS_TLS_MANAGER
  ASSERT_EQ(hixl::TlsManager::GetInstance().Initialize(hixl::TlsConfig{}), hixl::SUCCESS);
#endif
  MockMemTransferEngine mem_transfer_engine;
  ASSERT_EQ(GlobalMemManager::GetInstance().Initialize(&mem_transfer_engine), ge::SUCCESS);

  FailingControlServer server;
  ASSERT_TRUE(server.IsReady());
  MockHixlEngine engine;
  {
    HixlEntity entity("127.0.0.1", server.Port(), &engine);
    EXPECT_NE(entity.Initialize(1000), ge::SUCCESS);
  }

  EXPECT_EQ(engine.DisconnectCount(), 1);
  EXPECT_FALSE(engine.IsConnected());
  GlobalMemManager::GetInstance().Finalize();
#if HIXL_ENTITY_TEST_HAS_TLS_MANAGER
  hixl::TlsManager::GetInstance().Finalize();
#endif
}

TEST(HixlEntityTest, KeepsEngineConnectedWhenControlHandshakeSucceeds) {
#if !HIXL_ENTITY_TEST_HAS_TLS_MANAGER
  GTEST_SKIP() << "Control-plane TLS manager is unavailable in this API version";
#else
  ASSERT_EQ(hixl::TlsManager::GetInstance().Initialize(hixl::TlsConfig{}), hixl::SUCCESS);
  MockMemTransferEngine mem_transfer_engine;
  ASSERT_EQ(GlobalMemManager::GetInstance().Initialize(&mem_transfer_engine), ge::SUCCESS);

  FailingControlServer server(true);
  ASSERT_TRUE(server.IsReady());
  MockHixlEngine engine;
  {
    HixlEntity entity("127.0.0.1", server.Port(), &engine);
    ASSERT_EQ(entity.Initialize(1000), ge::SUCCESS);
  }

  EXPECT_EQ(engine.DisconnectCount(), 0);
  EXPECT_TRUE(engine.IsConnected());
  GlobalMemManager::GetInstance().Finalize();
  hixl::TlsManager::GetInstance().Finalize();
#endif
}
}  // namespace llm
