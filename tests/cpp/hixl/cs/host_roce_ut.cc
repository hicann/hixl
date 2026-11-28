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
#include <future>
#include <stdlib.h>
#include <unistd.h>
#include <thread>
#include "nlohmann/json.hpp"
#include "ascendcl_stub.h"
#include "cs/conn_msg_handler.h"
#include "cs/host_roce/host_roce_channel.h"
#include "cs/host_roce/host_roce_handshake.h"
#include "cs/host_roce/host_roce_qos.h"
#include "cs/hixl_cs_server.h"
#include "cs/hixl_cs_client.h"
#include "common/scope_guard.h"
#include "depends/host_roce_test_helper.h"
#include "depends/ctrl_msg_test_helper.h"
#include "depends/hccl/src/hccl_stub.h"
#include "slog_stub.h"

namespace hixl {
namespace {
constexpr const char *kRoceHixlBackendJson = R"({"roce_config.hixl_backend":true})";
GlobalConfig NativeConfig() {
  GlobalConfig config;
  EXPECT_EQ(GlobalConfig::Parse(kRoceHixlBackendJson, config), SUCCESS);
  return config;
}
EndpointDesc NativeEndpoint(const char *ip = "127.0.0.1") {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_ROCE;
  ep.loc.locType = ENDPOINT_LOC_TYPE_HOST;
  ep.commAddr.type = COMM_ADDR_TYPE_IP_V4;
  inet_pton(AF_INET, ip, &ep.commAddr.addr);
  return ep;
}
class HostRoceUt : public test::PlainTlsTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(test::PlainTlsTest::SetUp());
    verbs = std::make_shared<test::FakeHostRoceVerbs>();
    SetHostRoceVerbsForTest(verbs);
    transfer_config = NativeConfig();
    transfer_flag = 1ULL;
    transfer_flag_imported = false;
    ep = Endpoint::Create(NativeEndpoint(), NativeConfig());
    ASSERT_EQ(ep->Initialize(), SUCCESS);
  }
  void TearDown() override {
    SetHostRoceQosCfgPathForTest("");
    EXPECT_EQ(ep->Finalize(), SUCCESS);
    SetHostRoceVerbsForTest(nullptr);
  }
  std::shared_ptr<HostRoceChannel> NewChannel() {
    ChannelDesc desc{};
    desc.remote_endpoint = NativeEndpoint("127.0.0.2");
    ChannelHandle handle = 0;
    if (ep->CreateChannel(desc, handle, 1000) != SUCCESS) return nullptr;
    return std::dynamic_pointer_cast<HostRoceChannel>(ep->GetChannel(handle));
  }
  void Ready(const std::shared_ptr<HostRoceChannel> &channel) {
    ASSERT_NE(channel, nullptr);
    auto peer = channel->LocalPeer();
    peer.qpn += 100;
    peer.gid[15] = 2;
    ASSERT_EQ(channel->SetPeer(peer), SUCCESS);
    peer.plane = channel->LocalPeer().plane;
    ASSERT_EQ(channel->Negotiate(peer), SUCCESS);
    ASSERT_EQ(channel->ConnectPeer(), SUCCESS);
    ASSERT_EQ(channel->MarkReady(), SUCCESS);
  }
  std::shared_ptr<HostRoceChannel> NewTransferChannel() {
    auto channel = std::make_shared<HostRoceChannel>(*ep);
    const ChannelContext context{ep.get(), -1, &transfer_config, &transfer_store, true};
    if (channel->Initialize(context) != SUCCESS) return nullptr;
    ChannelDesc desc{};
    desc.remote_endpoint = NativeEndpoint("127.0.0.2");
    desc.tc = 132;
    desc.sl = 4;
    desc.retry_cnt = 7;
    desc.retry_interval = 20;
    if (channel->Connect(desc, 1000) != SUCCESS) return nullptr;
    Ready(channel);
    if (!transfer_flag_imported) {
      const auto wire = EncodeHostRoceMem(
          {reinterpret_cast<uint64_t>(&transfer_flag), sizeof(transfer_flag), 99, 100, COMM_MEM_TYPE_DEVICE});
      CommMem imported{};
      if (ep->ImportMem(wire.data(), wire.size(), imported) != SUCCESS) return nullptr;
      transfer_flag_imported = true;
    }
    CommMem flag_mem{COMM_MEM_TYPE_DEVICE, &transfer_flag, sizeof(transfer_flag)};
    if (channel->SetRemoteMemInfo(flag_mem) != SUCCESS) return nullptr;
    return channel;
  }
  void Memory(char *local, char *remote, uint64_t size, MemHandle &handle, CommMemType type = COMM_MEM_TYPE_HOST) {
    CommMem mem{};
    mem.type = type;
    mem.addr = local;
    mem.size = size;
    ASSERT_EQ(ep->RegisterMem("local", mem, handle), SUCCESS);
    ASSERT_EQ(transfer_store.RecordMemory(false, local, size, true), SUCCESS);
    const auto wire = EncodeHostRoceMem({reinterpret_cast<uint64_t>(remote), size, 19, 20, COMM_MEM_TYPE_HOST});
    CommMem imported{};
    ASSERT_EQ(ep->ImportMem(wire.data(), wire.size(), imported), SUCCESS);
    ASSERT_EQ(transfer_store.RecordMemory(true, remote, size, true), SUCCESS);
  }
  Status WaitChannelAsync(const std::shared_ptr<HostRoceChannel> &channel, void *handle, HixlCompleteStatus &status,
                          uint32_t timeout_ms = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
      status = HIXL_COMPLETE_STATUS_WAITING;
      const Status ret = channel->QueryCompleteStatus(handle, &status);
      if (ret != SUCCESS || status != HIXL_COMPLETE_STATUS_WAITING) {
        return ret;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return TIMEOUT;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
  void ConnectPair(HixlCSServer &server, HixlCSClient &client, uint32_t port, const char *json) {
    const auto endpoint = NativeEndpoint("127.0.0.2");
    ASSERT_EQ(server.Initialize(&endpoint, 1), SUCCESS);
    ASSERT_EQ(server.Listen(8), SUCCESS);
    const auto local_endpoint = NativeEndpoint();
    HixlClientDesc desc{};
    desc.local_endpoint = &local_endpoint;
    desc.remote_endpoint = &endpoint;
    desc.server_ip = "127.0.0.1";
    desc.server_port = port;
    desc.tc = 132;
    desc.sl = 4;
    HixlClientConfig config{};
    auto parsed = json == nullptr ? nlohmann::json::object() : nlohmann::json::parse(json);
    parsed["roce_config.hixl_backend"] = true;
    const std::string client_json = parsed.dump();
    config.global_resource_config = client_json.c_str();
    ASSERT_EQ(client.Create(&desc, &config), SUCCESS);
    ASSERT_EQ(client.Connect(3000), SUCCESS);
  }
  void RegisterPairMemory(HixlCSServer &server, HixlCSClient &client, char *local, char *remote, uint64_t size,
                          CommMemType local_type = COMM_MEM_TYPE_HOST, CommMemType remote_type = COMM_MEM_TYPE_HOST) {
    CommMem memory{};
    memory.type = remote_type;
    memory.addr = remote;
    memory.size = size;
    MemHandle mem = nullptr;
    ASSERT_EQ(server.RegisterMem("data", &memory, &mem), SUCCESS);
    CommMem *mems = nullptr;
    char **tags = nullptr;
    uint32_t count = 0;
    ASSERT_EQ(client.GetRemoteMem(&mems, &tags, &count, 1000), SUCCESS);
    memory.addr = local;
    memory.type = local_type;
    ASSERT_EQ(client.RegMem("local", &memory, &mem), SUCCESS);
  }
  Status WaitAsyncStatus(HixlCSClient &client, void *handle, HixlCompleteStatus &status, uint32_t timeout_ms = 2000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
      status = HIXL_COMPLETE_STATUS_WAITING;
      const Status ret = client.QueryCompleteStatus(handle, &status);
      if (ret != SUCCESS || status != HIXL_COMPLETE_STATUS_WAITING) {
        return ret;
      }
      if (std::chrono::steady_clock::now() >= deadline) {
        return TIMEOUT;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
  }
  EndpointPtr ep;
  std::shared_ptr<test::FakeHostRoceVerbs> verbs;
  GlobalConfig transfer_config;
  HixlMemStore transfer_store;
  uint64_t transfer_flag{1ULL};
  bool transfer_flag_imported{false};
};
TEST(HostRoceWireUt, VersionAndBoundsAreStrict) {
  HostRoceHandshake in;
  in.step = HostRoceHandshakeStep::kPrepare;
  in.index = 0x0102030405060708ULL;
  in.peer.mtu = 6;
  auto bytes = EncodeHostRoceHandshake(in);
  ASSERT_EQ(bytes.size(), 128U);
  EXPECT_EQ(bytes[12], 1);
  EXPECT_EQ(bytes[19], 8);
  HostRoceHandshake out;
  ASSERT_EQ(DecodeHostRoceHandshake(bytes.data(), bytes.size(), out), SUCCESS);
  EXPECT_EQ(out.step, HostRoceHandshakeStep::kPrepare);
  EXPECT_EQ(out.index, in.index);
  EXPECT_EQ(out.peer.mtu, 6U);
  in.step = HostRoceHandshakeStep::kReady;
  in.peer.has_feature = true;
  in.peer.more = true;
  bytes = EncodeHostRoceHandshake(in);
  ASSERT_EQ(DecodeHostRoceHandshake(bytes.data(), bytes.size(), out), SUCCESS);
  EXPECT_EQ(out.step, HostRoceHandshakeStep::kReady);
  EXPECT_TRUE(out.peer.has_feature);
  EXPECT_TRUE(out.peer.more);
  bytes[7] = 2;
  EXPECT_EQ(DecodeHostRoceHandshake(bytes.data(), bytes.size(), out), PARAM_INVALID);
  bytes = EncodeHostRoceHandshake(in);
  bytes[11] = 9;
  EXPECT_EQ(DecodeHostRoceHandshake(bytes.data(), bytes.size(), out), PARAM_INVALID);
  EXPECT_EQ(DecodeHostRoceHandshake(nullptr, 128U, out), PARAM_INVALID);
  EXPECT_FALSE(HostRoceContains(UINT64_MAX - 1, 8, UINT64_MAX - 1, 1));
  EXPECT_FALSE(HostRoceContains(100, 10, 109, 2));
  EXPECT_TRUE(HostRoceContains(100, 10, 110, 0));
  HostRoceMemDesc mem;
  bytes = EncodeHostRoceMem({100, 10, 1, 2, COMM_MEM_TYPE_HOST});
  EXPECT_EQ(bytes.size(), 44);
  EXPECT_EQ(DecodeHostRoceMem(bytes.data(), bytes.size(), mem), SUCCESS);
  EXPECT_EQ(DecodeHostRoceMem(bytes.data(), bytes.size() - 1, mem), PARAM_INVALID);
}
TEST_F(HostRoceUt, OneQpPerChannelAndPlaneStaysFixed) {
  EXPECT_NE(dynamic_cast<HostRoceEndpoint *>(ep.get()), nullptr);
  for (uint32_t i = 0; i < 9; ++i) {
    auto channel = NewChannel();
    Ready(channel);
    ASSERT_NE(channel, nullptr);
    EXPECT_EQ(channel->LocalPeer().plane, i % 4);
    EXPECT_EQ(channel->SetPeer(channel->LocalPeer()), PARAM_INVALID);
    EXPECT_EQ(channel->LocalPeer().plane, i % 4);
    EXPECT_EQ(ep->DestroyChannel(channel->GetHandle()), SUCCESS);
  }
  EXPECT_EQ(verbs->created, 9);
  EXPECT_EQ(verbs->stopped, 9);
}
TEST_F(HostRoceUt, FailedQpSetupRetainsTeardownError) {
  HostRoceChannel channel(*ep);
  const ChannelContext context{ep.get(), -1, &transfer_config, &transfer_store, true};
  ASSERT_EQ(channel.Initialize(context), SUCCESS);
  ChannelDesc desc{};
  desc.remote_endpoint = NativeEndpoint("127.0.0.2");
  verbs->create_qp_result = FAILED;
  verbs->stop_result = UNSUPPORTED;
  EXPECT_EQ(channel.Connect(desc, 1000), FAILED);
  EXPECT_EQ(channel.Disconnect(), UNSUPPORTED);
  EXPECT_EQ(verbs->created.load(), 1U);
  EXPECT_EQ(verbs->stopped.load(), 0U);
}
TEST_F(HostRoceUt, EndpointPassesNumericIpv4AndIpv6AddressesToVerbs) {
  const auto ipv4 = NativeEndpoint("10.1.0.122");
  auto ipv6 = NativeEndpoint();
  ipv6.commAddr.type = COMM_ADDR_TYPE_IP_V6;
  ASSERT_EQ(inet_pton(AF_INET6, "2001:0db8:0:0:0:0:0:0122", &ipv6.commAddr.addr6), 1);
  for (const auto &desc : {ipv4, ipv6}) {
    auto endpoint = Endpoint::Create(desc, NativeConfig());
    ASSERT_NE(endpoint, nullptr);
    ASSERT_EQ(endpoint->Initialize(), SUCCESS);
    EXPECT_EQ(endpoint->Finalize(), SUCCESS);
  }
  EXPECT_EQ(verbs->devices.count("10.1.0.122"), 1);
  EXPECT_EQ(verbs->devices.count("2001:db8::122"), 1);
}
#if defined(__linux__)
TEST_F(HostRoceUt, NativeVerbsRejectsDisplayAddressesBeforeDeviceDiscovery) {
  SetHostRoceVerbsForTest(nullptr);
  ScopeGuard restore([&] { SetHostRoceVerbsForTest(verbs); });
  auto native = GetHostRoceVerbs();
  std::shared_ptr<HostRoceDevice> device;
  for (const char *ip : {"IPv4:10.1.0.122", "IPv6:2001:db8::122", "10.1.0.1.122", ""})
    EXPECT_EQ(native->Open(ip, device), PARAM_INVALID);
  EXPECT_EQ(device, nullptr);
}
#endif
TEST_F(HostRoceUt, StandardNicAndPlaneCapabilityMismatch) {
  auto channel = NewChannel();
  ASSERT_NE(channel, nullptr);
  auto peer = channel->LocalPeer();
  peer.plane_count = 2;
  EXPECT_EQ(channel->SetPeer(peer), PARAM_INVALID);
  EXPECT_EQ(channel->MarkReady(), FAILED);
  verbs->planes = 0;
  auto standard = Endpoint::Create(NativeEndpoint("127.0.0.3"), NativeConfig());
  ASSERT_EQ(standard->Initialize(), SUCCESS);
  ChannelDesc desc{};
  desc.remote_endpoint = NativeEndpoint();
  ChannelHandle handle = 0;
  ASSERT_EQ(standard->CreateChannel(desc, handle, 1000), SUCCESS);
  auto q = std::dynamic_pointer_cast<HostRoceChannel>(standard->GetChannel(handle));
  Ready(q);
  EXPECT_EQ(q->LocalPeer().plane, kHostRoceNoPlane);
  EXPECT_EQ(standard->Finalize(), SUCCESS);
}
TEST_F(HostRoceUt, SegmentsAndPartialPostingResumeWithoutDuplicates) {
  verbs->qp_capacity = 8;
  verbs->hold_completions = true;
  verbs->reject_once = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[] = "abcdefghijklmno";
  char remote[sizeof(local)]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  void *request = nullptr;
  ASSERT_EQ(channel->BatchPutAsync(&op, 1, &request), SUCCESS);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (verbs->PostCount() < 4U) {
    ASSERT_LT(std::chrono::steady_clock::now(), deadline);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  EXPECT_EQ(verbs->PostCount(), 4U);
  EXPECT_NE(ep->DeregisterMem(mem), SUCCESS);
  verbs->hold_completions = false;
  HixlCompleteStatus status{};
  ASSERT_EQ(WaitChannelAsync(channel, request, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_STREQ(local, remote);
  const auto posts = verbs->PostsSnapshot();
  ASSERT_EQ(posts.size(), 6U);
  for (size_t i = 0; i < 4U; ++i) {
    EXPECT_EQ(posts[i].local, reinterpret_cast<uint64_t>(local) + i * 4);
    EXPECT_EQ(posts[i].id, i + 1);
  }
  EXPECT_EQ(ep->DeregisterMem(mem), SUCCESS);
  EXPECT_EQ(channel->QueryCompleteStatus(request, &status), PARAM_INVALID);
}
TEST_F(HostRoceUt, IndependentReadsUseBatchFenceAndWritesNeedNoDescriptorFence) {
  verbs->qp_capacity = 128;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[] = "abcdefghijklmno";
  char remote[sizeof(local)]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  const HixlOneSideOpDesc ops[] = {{remote, local, 7}, {remote + 7, local + 7, 5}};
  for (const bool read : {false, true}) {
    const auto start = verbs->PostCount();
    ASSERT_EQ(read ? channel->BatchGetSync(ops, 2, 2000) : channel->BatchPutSync(ops, 2, 2000), SUCCESS);
    const auto posts = verbs->PostsSnapshot();
    ASSERT_EQ(posts.size(), start + 6U);
    for (size_t i = 0; i < 4U; ++i) {
      EXPECT_EQ(posts[start + i].fence, read && i == 3U);
      EXPECT_TRUE(posts[start + i].signaled);
    }
    EXPECT_TRUE(posts[start + 4U].read);
    EXPECT_TRUE(posts[start + 4U].fence);
    EXPECT_TRUE(posts[start + 5U].read);
    EXPECT_TRUE(posts[start + 5U].fence);
  }
  const HixlOneSideOpDesc empty_op{nullptr, nullptr, 0};
  EXPECT_EQ(channel->BatchPutSync(&empty_op, 1, 1000), PARAM_INVALID);
}
TEST_F(HostRoceUt, OversizedDescriptorUsesBoundedSqSegmentsWithoutDuplicatePosts) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  verbs->defer_copies = true;
  char local[] = "abcdefghijklmnopqrs";
  char remote[sizeof(local)]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(channel->BatchPutSync(&op, 1, 2000), SUCCESS);
  const auto posts = verbs->PostsSnapshot();
  ASSERT_EQ(posts.size(), 7U);
  EXPECT_STREQ(local, remote);
  for (size_t i = 0; i < 5U; ++i) {
    EXPECT_TRUE(posts[i].signaled);
    EXPECT_EQ(posts[i].pending_before, i % 2);
    EXPECT_EQ(posts[i].local, reinterpret_cast<uint64_t>(local) + i * 4);
  }
  EXPECT_EQ(verbs->batch_sizes, (std::vector<size_t>{2, 2, 1, 2}));
}
TEST_F(HostRoceUt, DefaultSizedBatchUsesOnePostAndOneCompletionPerWr) {
  verbs->qp_capacity = 2048;
  verbs->defer_copies = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[4] = "abc";
  char remote[4]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  std::vector<HixlOneSideOpDesc> ops(1920, {remote, local, sizeof(local)});
  for (const bool read : {false, true}) {
    const auto first = verbs->PostCount();
    const auto completions = verbs->generated_completions.load();
    if (read) std::memset(local, 0, sizeof(local));
    ASSERT_EQ(read ? channel->BatchGetSync(ops.data(), ops.size(), 5000)
                   : channel->BatchPutSync(ops.data(), ops.size(), 5000),
              SUCCESS);
    const auto posts = verbs->PostsSnapshot();
    EXPECT_EQ(posts.size(), first + ops.size() + 2U);
    EXPECT_EQ(verbs->generated_completions.load(), completions + ops.size() + 2U);
    for (size_t i = 0; i < ops.size() + 2U; ++i) EXPECT_TRUE(posts[first + i].signaled);
  }
  EXPECT_EQ(verbs->batch_sizes, (std::vector<size_t>{1920, 2, 1920, 2}));
  EXPECT_EQ(verbs->batch_posted, verbs->batch_sizes);
  EXPECT_STREQ(local, remote);
}
TEST_F(HostRoceUt, SparsePageReadsUseTheQpReadWindow) {
  constexpr size_t pages = 16;
  constexpr size_t page_bytes = 144 * 1024;
  constexpr size_t stride = page_bytes + 64 * 1024;
  verbs->model_read_pipeline = true;
  verbs->qp_capacity = 128;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  dynamic_cast<HostRoceEndpoint *>(ep.get())->Device()->max_message_size = page_bytes;
  std::vector<char> local(pages * stride, 0), remote(pages * stride, 'x');
  MemHandle mem = nullptr;
  Memory(local.data(), remote.data(), local.size(), mem);
  std::vector<HixlOneSideOpDesc> ops;
  for (size_t i = 0; i < pages; ++i) ops.push_back({remote.data() + i * stride, local.data() + i * stride, page_bytes});
  ASSERT_EQ(channel->BatchGetSync(ops.data(), ops.size(), 5000), SUCCESS);
  for (size_t i = 0; i < pages; ++i) {
    EXPECT_EQ(std::memcmp(local.data() + i * stride, remote.data() + i * stride, page_bytes), 0);
    EXPECT_EQ(local[i * stride + page_bytes], 0);
  }
  EXPECT_GE(verbs->max_inflight_reads, 4U);
  EXPECT_LT(verbs->read_waves, pages / 2);
  EXPECT_EQ(verbs->generated_completions.load(), pages + 1U);
  const auto posts = verbs->PostsSnapshot();
  ASSERT_EQ(posts.size(), pages + 1U);
  const auto data_fences = std::count_if(posts.begin(), posts.begin() + pages, [](const auto &wr) { return wr.fence; });
  EXPECT_EQ(data_fences, 1);
  EXPECT_TRUE(posts[pages - 1U].fence);
  EXPECT_TRUE(posts[pages].fence);
  RecordProperty("max_inflight_reads", static_cast<int>(verbs->max_inflight_reads));
  RecordProperty("read_waves", verbs->read_waves);
  RecordProperty("fenced_wrs", static_cast<int>(data_fences));
}
TEST_F(HostRoceUt, OverlappingReadDestinationsKeepDescriptorOrder) {
  verbs->model_read_pipeline = true;
  verbs->qp_capacity = 128;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[17]{};
  char remote[] = "AAAABBBBCCCCDDDD";
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  const HixlOneSideOpDesc ops[] = {
      {remote, local, 4}, {remote + 4, local, 4}, {remote + 8, local + 4, 4}, {remote + 12, local + 8, 4}};
  ASSERT_EQ(channel->BatchGetSync(ops, 4, 2000), SUCCESS);
  EXPECT_EQ(std::memcmp(local, "BBBBCCCCDDDD", 12), 0);
}
TEST_F(HostRoceUt, AliasedReadSourcesAndDestinationsKeepDescriptorOrder) {
  verbs->model_read_pipeline = true;
  verbs->qp_capacity = 128;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char data[] = "AAAABBBBCCCCDDDD";
  MemHandle mem = nullptr;
  Memory(data, data, sizeof(data), mem);
  const HixlOneSideOpDesc ops[] = {{data, data + 4, 4},
                                   {data + 4, data + 8, 4},
                                   {data + 12, data + 4, 4},
                                   {data, data + 12, 4},
                                   {data + 8, data, 4}};
  ASSERT_EQ(channel->BatchGetSync(ops, 5, 2000), SUCCESS);
  EXPECT_EQ(std::memcmp(data, "AAAADDDDAAAAAAAA", 16), 0);
}
TEST_F(HostRoceUt, PendingReadRequestsKeepCrossRequestDependencies) {
  verbs->model_read_pipeline = true;
  verbs->qp_capacity = 128;
  verbs->hold_completions = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char data[] = "AAAABBBBCCCCDDDD";
  MemHandle mem = nullptr;
  Memory(data, data, sizeof(data), mem);
  const HixlOneSideOpDesc first{data, data + 4, 4};
  const HixlOneSideOpDesc second[] = {{data + 4, data + 8, 4}, {data, data + 12, 4}};
  void *first_handle = nullptr, *second_handle = nullptr;
  ASSERT_EQ(channel->BatchGetAsync(&first, 1, &first_handle), SUCCESS);
  ASSERT_EQ(channel->BatchGetAsync(second, 2, &second_handle), SUCCESS);
  verbs->hold_completions = false;
  HixlCompleteStatus status{};
  ASSERT_EQ(WaitChannelAsync(channel, second_handle, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  ASSERT_EQ(WaitChannelAsync(channel, first_handle, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_EQ(std::memcmp(data, "AAAAAAAAAAAAAAAA", 16), 0);
}
TEST_F(HostRoceUt, PartialBatchPostResumesFromFirstRejectedWrWithoutResendingData) {
  verbs->qp_capacity = 4;
  verbs->defer_copies = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[] = "abcdefghijklmno";
  char remote[sizeof(local)]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  verbs->reject_id_once = 3;
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(channel->BatchPutSync(&op, 1, 2000), SUCCESS);
  EXPECT_STREQ(local, remote);
  EXPECT_EQ(verbs->batch_sizes, (std::vector<size_t>{4, 2, 2}));
  EXPECT_EQ(verbs->batch_posted, (std::vector<size_t>{2, 2, 2}));
  const auto posts = verbs->PostsSnapshot();
  ASSERT_EQ(posts.size(), 6U);
  EXPECT_EQ(verbs->generated_completions.load(), 6U);
  for (size_t slice = 0; slice < 4U; ++slice) {
    EXPECT_EQ(posts[slice].size, 4U);
    EXPECT_TRUE(posts[slice].signaled);
    EXPECT_EQ(posts[slice].local, reinterpret_cast<uint64_t>(local) + slice * 4);
  }
  EXPECT_EQ(verbs->stopped.load(), 0U);
}
TEST_F(HostRoceUt, FatalPartialBatchPostStopsQpAndRetainsNoUserHandle) {
  verbs->qp_capacity = 8;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[] = "abcdefghijklmno";
  char remote[sizeof(local)]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  verbs->fail_id_once = 3;
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  void *handle = nullptr;
  EXPECT_EQ(channel->BatchPutAsync(&op, 1, &handle), FAILED);
  EXPECT_EQ(handle, nullptr);
  EXPECT_EQ(verbs->PostCount(), 2U);
  EXPECT_EQ(verbs->stopped.load(), 1U);
  EXPECT_EQ(channel->BatchPutAsync(&op, 1, &handle), FAILED);
  EXPECT_EQ(ep->DeregisterMem(mem), SUCCESS);
}
TEST_F(HostRoceUt, BatchLargerThanSqReusesEachCompletionWithoutDuplicatePosts) {
  verbs->qp_capacity = 3;
  verbs->poll_limit = 1;
  verbs->defer_copies = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[] = "abcdefghijklmnopqrstuvwxyz12345";
  char remote[sizeof(local)]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  std::vector<HixlOneSideOpDesc> ops;
  for (size_t i = 0; i < 8; ++i) ops.push_back({remote + i * 4, local + i * 4, 4});
  void *handle = nullptr;
  ASSERT_EQ(channel->BatchPutAsync(ops.data(), ops.size(), &handle), SUCCESS);
  EXPECT_EQ(verbs->PostCount(), ops.size());
  EXPECT_GE(verbs->polled_completions.load(), ops.size() - verbs->qp_capacity);
  HixlCompleteStatus status{};
  ASSERT_EQ(WaitChannelAsync(channel, handle, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_STREQ(local, remote);
  EXPECT_EQ(verbs->batch_sizes, (std::vector<size_t>{3, 1, 1, 1, 1, 1, 2}));
  EXPECT_EQ(verbs->generated_completions.load(), 10U);
  const auto posts = verbs->PostsSnapshot();
  ASSERT_EQ(posts.size(), 10U);
  for (size_t i = 0; i < 8U; ++i) {
    EXPECT_TRUE(posts[i].signaled);
    EXPECT_LT(posts[i].pending_before, verbs->qp_capacity);
    EXPECT_FALSE(posts[i].fence);
    EXPECT_EQ(posts[i].local, reinterpret_cast<uint64_t>(local) + i * 4);
  }
}
TEST_F(HostRoceUt, OutOfOrderReadCqesReclaimOnlyTheirOwnWrAndKeepMemoryPinned) {
  verbs->qp_capacity = 128;
  verbs->model_read_pipeline = true;
  verbs->poll_limit = 1;
  verbs->hold_completions = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[36]{};
  char remote[] = "AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHHIIII";
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  std::vector<HixlOneSideOpDesc> ops;
  for (size_t i = 0; i < 9; ++i) ops.push_back({remote + i * 4, local + i * 4, 4});
  void *request = nullptr;
  ASSERT_EQ(channel->BatchGetAsync(ops.data(), ops.size(), &request), SUCCESS);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (verbs->PostCount() < ops.size()) {
    ASSERT_LT(std::chrono::steady_clock::now(), deadline);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  HixlCompleteStatus status{};
  ASSERT_EQ(channel->QueryCompleteStatus(request, &status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_WAITING);
  EXPECT_NE(ep->DeregisterMem(mem), SUCCESS);
  verbs->hold_completions = false;
  ASSERT_EQ(WaitChannelAsync(channel, request, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_EQ(std::memcmp(local, remote, sizeof(local)), 0);
  EXPECT_EQ(verbs->generated_completions.load(), ops.size() + 2U);
  EXPECT_EQ(verbs->stopped.load(), 0U);
  EXPECT_EQ(ep->DeregisterMem(mem), SUCCESS);
}
TEST_F(HostRoceUt, ZeroTimeoutDrainsMoreThanTwoPollBatches) {
  verbs->qp_capacity = 128;
  verbs->hold_completions = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[4]{};
  char remote[4]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  std::vector<void *> handles(65);
  for (auto &handle : handles) ASSERT_EQ(channel->BatchPutAsync(&op, 1, &handle), SUCCESS);
  verbs->hold_completions = false;
  HixlCompleteStatus status{};
  EXPECT_EQ(WaitChannelAsync(channel, handles.back(), status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  for (size_t i = 0; i + 1 < handles.size(); ++i) {
    EXPECT_EQ(WaitChannelAsync(channel, handles[i], status), SUCCESS);
    EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  }
  EXPECT_EQ(verbs->stopped.load(), 0U);
}

TEST_F(HostRoceUt, IgnoresHcclExecTimeout) {
  ScopeGuard restore([] { unsetenv("HCCL_EXEC_TIMEOUT"); });
  for (const char *value : {"-1", "invalid", "1.234", "2147483648", "0"}) {
    ASSERT_EQ(setenv("HCCL_EXEC_TIMEOUT", value, 1), 0);
    auto channel = NewChannel();
    Ready(channel);
  }
}

TEST_F(HostRoceUt, NativeHostRocePublishesHbmFlagWithoutTransferPool) {
  HixlCSServer server("127.0.0.1", 26642, NativeConfig());
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  const auto desc = NativeEndpoint();
  ASSERT_EQ(server.Initialize(&desc, 1), SUCCESS);
  const auto handles = server.endpoint_store_.GetAllEndpointHandles();
  ASSERT_EQ(handles.size(), 1U);
  const auto endpoint = server.endpoint_store_.GetEndpoint(handles[0]);
  ASSERT_NE(endpoint, nullptr);
  EXPECT_NE(dynamic_cast<HostRoceEndpoint *>(endpoint.get()), nullptr);
  EXPECT_NE(server.dev_trans_flag_, nullptr);
  EXPECT_EQ(server.host_trans_flag_, nullptr);
  EXPECT_EQ(server.device_id_, -1);
}

TEST_F(HostRoceUt, HcommHostRoceKeepsDefaultHostFlagWhenHixlBackendIsDisabled) {
  ResetTransferCounter();
  ResetMemRegRecord();
  ResetChannelCreateRecord();
  HixlCSServer server("127.0.0.1", 26643, GlobalConfig{});
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  const auto remote_ep = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&remote_ep, 1), SUCCESS);
  char remote[8]{};
  CommMem remote_mem{COMM_MEM_TYPE_HOST, remote, sizeof(remote)};
  MemHandle server_mem = nullptr;
  ASSERT_EQ(server.RegisterMem("data", &remote_mem, &server_mem), SUCCESS);
  ASSERT_EQ(server.Listen(8), SUCCESS);

  HixlCSClient client;
  ScopeGuard destroy([&] { (void)client.Destroy(); });
  const auto local_ep = NativeEndpoint();
  HixlClientDesc desc{};
  desc.local_endpoint = &local_ep;
  desc.remote_endpoint = &remote_ep;
  desc.server_ip = "127.0.0.1";
  desc.server_port = 26643;
  HixlClientConfig config{};
  ASSERT_EQ(client.Create(&desc, &config), SUCCESS);
  ASSERT_EQ(client.Connect(3000), SUCCESS);

  CommMem *mems = nullptr;
  char **tags = nullptr;
  uint32_t count = 0;
  ASSERT_EQ(client.GetRemoteMem(&mems, &tags, &count, 1000), SUCCESS);
  bool saw_host_flag = false;
  for (uint32_t i = 0; i < count; ++i) {
    if (std::string(tags[i]) == "_hixl_builtin_host_trans_flag") {
      EXPECT_EQ(mems[i].type, COMM_MEM_TYPE_HOST);
      saw_host_flag = true;
    }
  }
  EXPECT_TRUE(saw_host_flag);

  char local[8] = "hcomm";
  CommMem local_mem{COMM_MEM_TYPE_HOST, local, sizeof(local)};
  MemHandle client_mem = nullptr;
  ASSERT_EQ(client.RegMem("local", &local_mem, &client_mem), SUCCESS);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  void *handle = nullptr;
  ASSERT_EQ(client.BatchPutAsync(&op, 1, &handle), SUCCESS);
  EXPECT_NE(handle, nullptr);
  EXPECT_EQ(verbs->created, 0U);
  EXPECT_EQ(GetNbiCallCount(), 2U);
  EXPECT_EQ(GetFenceCallCount(), 1U);
}

// 26641 is reserved for the native Host RoCE HBM flag exchange test.
TEST_F(HostRoceUt, HostRocePutReadsTheServersDeviceFlag) {
  HixlCSServer server("127.0.0.1", 26641, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, 26641, nullptr));
  char local[8] = "put";
  char remote[8]{};
  ASSERT_NO_FATAL_FAILURE(
      RegisterPairMemory(server, client, local, remote, sizeof(local), COMM_MEM_TYPE_HOST, COMM_MEM_TYPE_DEVICE));
  CommMem *mems = nullptr;
  char **tags = nullptr;
  uint32_t count = 0;
  ASSERT_EQ(client.GetRemoteMem(&mems, &tags, &count, 1000), SUCCESS);
  void *remote_flag = nullptr;
  for (uint32_t i = 0; i < count; ++i) {
    EXPECT_STRNE(tags[i], "_hixl_builtin_host_trans_flag");
    if (std::string(tags[i]) == "_hixl_builtin_dev_trans_flag") {
      EXPECT_EQ(mems[i].type, COMM_MEM_TYPE_DEVICE);
      remote_flag = mems[i].addr;
    }
  }
  ASSERT_NE(remote_flag, nullptr);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(client.BatchPutSync(&op, 1, 1000), SUCCESS);
  ASSERT_FALSE(verbs->posts.empty());
  EXPECT_TRUE(verbs->posts.back().read);
  const auto flag_base = reinterpret_cast<uint64_t>(remote_flag);
  uint64_t flag_bytes = 0;
  for (const auto &post : verbs->posts) {
    if (!post.read) continue;
    EXPECT_GE(post.remote, flag_base);
    EXPECT_LE(post.remote + post.size, flag_base + sizeof(uint64_t));
    flag_bytes += post.size;
  }
  EXPECT_EQ(flag_bytes, sizeof(uint64_t));
  EXPECT_STREQ(remote, local);
}

TEST_F(HostRoceUt, RemoteReadAfterWriteFlagMustBeDeviceMemory) {
  HixlMemStore store;
  GlobalConfig config;
  auto channel = std::make_shared<HostRoceChannel>(*ep);
  const ChannelContext context{ep.get(), -1, &config, &store, true};
  ASSERT_EQ(channel->Initialize(context), SUCCESS);
  uint64_t value = 1;
  CommMem flag{};
  flag.addr = &value;
  flag.size = sizeof(value);
  flag.type = COMM_MEM_TYPE_HOST;
  EXPECT_EQ(channel->SetRemoteMemInfo(flag), PARAM_INVALID);
  flag.type = COMM_MEM_TYPE_DEVICE;
  EXPECT_EQ(channel->SetRemoteMemInfo(flag), SUCCESS);
  EXPECT_EQ(channel->SetRemoteMemInfo(CommMem{}), SUCCESS);
}

TEST_F(HostRoceUt, ReadAndLogicalRangeValidation) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[16]{};
  char remote[] = "ABCDEFGHIJKLMNO";
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  HixlOneSideOpDesc op{remote + 2, local + 1, 7};
  ASSERT_EQ(channel->BatchGetSync(&op, 1, 2000), SUCCESS);
  EXPECT_EQ(std::memcmp(local + 1, remote + 2, 7), 0);
  op.len = 16;
  EXPECT_EQ(channel->BatchGetSync(&op, 1, 2000), PARAM_INVALID);
}
TEST_F(HostRoceUt, CompletionErrorAndTimeoutStopQpBeforeReleasingMemory) {
  const char *completion_error_log = "wc_status=12, vendor_err=17";
  const char *timeout_log = "[HostRoce] Transfer deadline expired";
  auto logs = std::make_shared<llm::LogCaptureStub>();
  logs->AddCapturePattern(completion_error_log);
  logs->AddCapturePattern(timeout_log);
  llm::SlogStub::SetInstance(logs);
  ScopeGuard restore_logs([] { llm::SlogStub::SetInstance(nullptr); });

  char local[16]{};
  char remote[16]{};
  MemHandle mem = nullptr;
  Memory(local, remote, 16, mem);
  for (bool timeout : {false, true}) {
    auto channel = NewTransferChannel();
    ASSERT_NE(channel, nullptr);
    verbs->completion_error = timeout ? 0 : 12;  // IBV_WC_RETRY_EXC_ERR
    verbs->hold_completions = timeout;
    HixlOneSideOpDesc op{remote, local, 4};
    EXPECT_EQ(channel->BatchPutSync(&op, 1, 10), timeout ? TIMEOUT : FAILED);
    EXPECT_EQ(channel->BatchPutSync(&op, 1, 10), FAILED);
  }
  EXPECT_TRUE(logs->IsPatternCaptured(completion_error_log));
  EXPECT_TRUE(logs->IsPatternCaptured(timeout_log));
  EXPECT_EQ(verbs->stopped.load(), 2U);
  EXPECT_EQ(ep->DeregisterMem(mem), SUCCESS);
}

TEST_F(HostRoceUt, SyncDeadlinePollsQueuedDataCompletionError) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[4]{};
  char remote[4]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  verbs->completion_error = 12;  // IBV_WC_RETRY_EXC_ERR
  verbs->hold_completions = true;
  void *request = nullptr;
  ASSERT_EQ(channel->BatchPutAsync(&op, 1, &request), SUCCESS);
  ASSERT_EQ(verbs->polled_completions.load(), 0U);
  verbs->hold_completions = false;

  EXPECT_EQ(channel->BatchPutSync(&op, 1, 0), FAILED);
  EXPECT_EQ(verbs->polled_completions.load(), 1U);
  HixlCompleteStatus status{};
  EXPECT_EQ(channel->QueryCompleteStatus(request, &status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_FAILED);
  EXPECT_EQ(verbs->stopped.load(), 1U);
  EXPECT_EQ(ep->DeregisterMem(mem), SUCCESS);
}

TEST_F(HostRoceUt, DuplicateLogicalRegistrationsShareMrAndImportReferenceCounts) {
  char local[16]{};
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = local;
  mem.size = sizeof(local);
  MemHandle first = nullptr, second = nullptr;
  ASSERT_EQ(ep->RegisterMem("one", mem, first), SUCCESS);
  ASSERT_EQ(ep->RegisterMem("two", mem, second), SUCCESS);
  EXPECT_EQ(verbs->registrations, 1);
  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(ep->ExportMem(exported), SUCCESS);
  ASSERT_EQ(exported.size(), 2);
  CommMem imported{};
  for (int i = 0; i < 2; ++i)
    ASSERT_EQ(ep->ImportMem(exported[0].export_desc, exported[0].export_len, imported), SUCCESS);
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  HostRoceMemDesc found;
  EXPECT_EQ(ep->UnimportMem(exported[0].export_desc, exported[0].export_len), SUCCESS);
  EXPECT_EQ(native->FindRemote(reinterpret_cast<uint64_t>(local), sizeof(local), found), SUCCESS);
  EXPECT_EQ(ep->UnimportMem(exported[0].export_desc, exported[0].export_len), SUCCESS);
  EXPECT_EQ(native->FindRemote(reinterpret_cast<uint64_t>(local), sizeof(local), found), PARAM_INVALID);
  EXPECT_EQ(ep->DeregisterMem(first), SUCCESS);
  std::shared_ptr<HostRoceLocalMemory> remaining;
  EXPECT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(local), sizeof(local), remaining), SUCCESS);
  remaining.reset();
  EXPECT_EQ(ep->DeregisterMem(second), SUCCESS);
  EXPECT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(local), sizeof(local), remaining), PARAM_INVALID);
}
TEST_F(HostRoceUt, LocalAddressLookupPreservesNestedRangesAndRemovesUnregisteredEntries) {
  char local[128]{};
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  CommMem memory{COMM_MEM_TYPE_HOST, local, 96};
  MemHandle outer = nullptr, inner = nullptr, same_start = nullptr, later = nullptr;
  ASSERT_EQ(ep->RegisterMem("outer", memory, outer), SUCCESS);
  memory = {COMM_MEM_TYPE_HOST, local + 16, 8};
  ASSERT_EQ(ep->RegisterMem("inner", memory, inner), SUCCESS);
  memory = {COMM_MEM_TYPE_HOST, local, 8};
  ASSERT_EQ(ep->RegisterMem("same-start", memory, same_start), SUCCESS);
  memory = {COMM_MEM_TYPE_HOST, local + 64, 8};
  ASSERT_EQ(ep->RegisterMem("later", memory, later), SUCCESS);
  std::shared_ptr<HostRoceLocalMemory> found;
  ASSERT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(local + 20), 60, found), SUCCESS);
  EXPECT_EQ(found->memory.addr, local);
  EXPECT_EQ(found->memory.size, 96);
  found.reset();
  ASSERT_EQ(ep->DeregisterMem(outer), SUCCESS);
  EXPECT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(local + 20), 60, found), PARAM_INVALID);
  ASSERT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(local + 20), 4, found), SUCCESS);
  EXPECT_EQ(found->memory.addr, local + 16);
  ASSERT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(local), 8, found), SUCCESS);
  EXPECT_EQ(found->memory.size, 8);
}
TEST_F(HostRoceUt, RemoteAddressLookupPreservesNestedRangesRefcountsAndAddressReuse) {
  char remote[128]{};
  const auto base = reinterpret_cast<uint64_t>(remote);
  const auto outer = EncodeHostRoceMem({base, 96, 900, 901, COMM_MEM_TYPE_HOST});
  const auto inner = EncodeHostRoceMem({base + 16, 8, 100, 101, COMM_MEM_TYPE_HOST});
  const auto same_start = EncodeHostRoceMem({base, 8, 800, 801, COMM_MEM_TYPE_HOST});
  const auto later = EncodeHostRoceMem({base + 64, 8, 50, 51, COMM_MEM_TYPE_HOST});
  CommMem imported{};
  for (const auto *wire : {&outer, &inner, &same_start, &later, &outer})
    ASSERT_EQ(ep->ImportMem(wire->data(), wire->size(), imported), SUCCESS);
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  HostRoceMemDesc found;
  ASSERT_EQ(native->FindRemote(base + 20, 60, found), SUCCESS);
  EXPECT_EQ(found.rkey, 901);
  ASSERT_EQ(ep->UnimportMem(outer.data(), outer.size()), SUCCESS);
  EXPECT_EQ(native->FindRemote(base + 20, 60, found), SUCCESS);
  ASSERT_EQ(ep->UnimportMem(outer.data(), outer.size()), SUCCESS);
  EXPECT_EQ(native->FindRemote(base + 20, 60, found), PARAM_INVALID);
  ASSERT_EQ(native->FindRemote(base + 20, 4, found), SUCCESS);
  EXPECT_EQ(found.rkey, 101);
  ASSERT_EQ(native->FindRemote(base, 8, found), SUCCESS);
  EXPECT_EQ(found.rkey, 801);
  const auto replacement = EncodeHostRoceMem({base, 96, 700, 701, COMM_MEM_TYPE_HOST});
  ASSERT_EQ(ep->ImportMem(replacement.data(), replacement.size(), imported), SUCCESS);
  ASSERT_EQ(native->FindRemote(base + 20, 60, found), SUCCESS);
  EXPECT_EQ(found.rkey, 701);
}
TEST_F(HostRoceUt, AddressLookupRejectsGapsAdjacentRangeCrossingsAndOverflow) {
  char memory[32]{};
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  const auto base = reinterpret_cast<uint64_t>(memory);
  for (uint64_t offset : {0U, 8U, 24U}) {
    CommMem local{COMM_MEM_TYPE_HOST, memory + offset, 8};
    MemHandle handle = nullptr;
    ASSERT_EQ(ep->RegisterMem("range", local, handle), SUCCESS);
    const auto wire = EncodeHostRoceMem({base + offset, 8, offset + 1, 20, COMM_MEM_TYPE_HOST});
    CommMem imported{};
    ASSERT_EQ(ep->ImportMem(wire.data(), wire.size(), imported), SUCCESS);
  }
  std::shared_ptr<HostRoceLocalMemory> local;
  HostRoceMemDesc remote;
  EXPECT_EQ(native->FindLocal(base + 8, 8, local), SUCCESS);
  EXPECT_EQ(native->FindRemote(base + 8, 8, remote), SUCCESS);
  for (const auto address : {base - 1, base + 4, base + 16, base + 32, UINT64_MAX - 1}) {
    EXPECT_EQ(native->FindLocal(address, 8, local), PARAM_INVALID);
    EXPECT_EQ(native->FindRemote(address, 8, remote), PARAM_INVALID);
  }
}
TEST_F(HostRoceUt, ConcurrentChannelsShareNicRoundRobin) {
  std::vector<uint32_t> planes(32);
  std::vector<std::thread> threads;
  for (size_t i = 0; i < planes.size(); ++i) {
    threads.emplace_back([&, i] {
      auto channel = NewChannel();
      ASSERT_NE(channel, nullptr);
      planes[i] = channel->LocalPeer().plane;
    });
  }
  for (auto &thread : threads) thread.join();
  std::sort(planes.begin(), planes.end());
  for (size_t i = 0; i < planes.size(); ++i) EXPECT_EQ(planes[i], i / 8);
  EXPECT_EQ(verbs->created, planes.size());
}
TEST_F(HostRoceUt, OutOfOrderQueriesKeepIndependentRequestsAndMemoryReferences) {
  verbs->qp_capacity = 128;
  verbs->hold_completions = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[16] = "independent-ops";
  char remote[16]{};
  MemHandle mem = nullptr;
  Memory(local, remote, 16, mem);
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  void *first = nullptr, *second = nullptr;
  ASSERT_EQ(channel->BatchPutAsync(&op, 1, &first), SUCCESS);
  ASSERT_EQ(channel->BatchGetAsync(&op, 1, &second), SUCCESS);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (verbs->PostCount() < 4U) {
    ASSERT_LT(std::chrono::steady_clock::now(), deadline);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  EXPECT_NE(ep->DeregisterMem(mem), SUCCESS);
  verbs->hold_completions = false;
  HixlCompleteStatus status{};
  ASSERT_EQ(WaitChannelAsync(channel, second, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  ASSERT_EQ(WaitChannelAsync(channel, first, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_EQ(channel->QueryCompleteStatus(first, &status), PARAM_INVALID);
  EXPECT_EQ(ep->DeregisterMem(mem), SUCCESS);
}
class AllocationRangeStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtPointerGetAttributes(const void *, aclrtPtrAttributes *attributes) override {
    ++attr_calls;
    if (attr_result != ACL_SUCCESS) {
      return attr_result;
    }
    attributes->location.type = location_type;
    attributes->location.id = location_id;
    return ACL_SUCCESS;
  }
  aclError aclrtGetDevice(int32_t *device_id) override {
    ++device_calls;
    if (device_result != ACL_SUCCESS) {
      return device_result;
    }
    *device_id = current_device_id;
    return ACL_SUCCESS;
  }
  aclError aclrtMemGetAddressRange(void *, void **base, size_t *size) override {
    ++range_calls;
    if (range_result != ACL_SUCCESS) {
      return range_result;
    }
    *base = allocation;
    *size = bytes;
    return ACL_SUCCESS;
  }
  void *allocation{nullptr};
  size_t bytes{0};
  aclrtMemLocationType location_type{ACL_MEM_LOCATION_TYPE_DEVICE};
  uint32_t location_id{0U};
  int32_t current_device_id{0};
  aclError attr_result{ACL_SUCCESS};
  aclError device_result{ACL_SUCCESS};
  aclError range_result{ACL_SUCCESS};
  int attr_calls{0};
  int device_calls{0};
  int range_calls{0};
};
TEST_F(HostRoceUt, DeviceRegistrationUsesDeclaredSpanWithoutAddressRange) {
  char allocation[64]{};
  auto runtime = std::make_shared<AllocationRangeStub>();
  runtime->allocation = allocation;
  runtime->bytes = sizeof(allocation);
  runtime->location_id = 3U;
  runtime->range_result = ACL_ERROR_RT_INTERNAL_ERROR;
  llm::AclRuntimeStub::SetInstance(runtime);
  ScopeGuard reset([] { llm::AclRuntimeStub::Reset(); });
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  native->Device()->resolve_device_memory = true;
  CommMem memory{COMM_MEM_TYPE_DEVICE, allocation + 4, 8};
  MemHandle first = nullptr, duplicate = nullptr, second = nullptr;
  ASSERT_EQ(ep->RegisterMem("a", memory, first), SUCCESS);
  EXPECT_EQ(runtime->attr_calls, 1);
  EXPECT_EQ(runtime->range_calls, 0);
  EXPECT_TRUE(verbs->last_registered_device_memory.load());
  EXPECT_EQ(verbs->last_registered_memory_device.load(), 3U);
  ASSERT_EQ(ep->RegisterMem("a_dup", memory, duplicate), SUCCESS);
  EXPECT_EQ(verbs->registrations, 1);
  memory.addr = allocation + 16;
  ASSERT_EQ(ep->RegisterMem("b", memory, second), SUCCESS);
  EXPECT_EQ(runtime->range_calls, 0);
  EXPECT_EQ(verbs->registrations, 2);
  std::shared_ptr<HostRoceLocalMemory> found;
  ASSERT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(allocation + 4), 8, found), SUCCESS);
  EXPECT_EQ(found->mr->base, reinterpret_cast<uint64_t>(allocation + 4));
  EXPECT_EQ(found->mr->size, 8U);
  EXPECT_EQ(found->mr->memory_device, 3U);
  ASSERT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(allocation + 16), 8, found), SUCCESS);
  EXPECT_EQ(found->mr->base, reinterpret_cast<uint64_t>(allocation + 16));
  EXPECT_EQ(found->mr->size, 8U);
  EXPECT_EQ(found->mr->memory_device, 3U);
  EXPECT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(allocation + 12), 1, found), PARAM_INVALID);
  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(ep->ExportMem(exported), SUCCESS);
  for (const auto &wire : exported) {
    HostRoceMemDesc desc;
    ASSERT_EQ(DecodeHostRoceMem(wire.export_desc, wire.export_len, desc), SUCCESS);
    EXPECT_EQ(desc.size, 8U);
  }
}
TEST_F(HostRoceUt, RegistrationRangeSkipsAddressRangeAndUbSegmentForNonDeviceAttributes) {
  char buf[64]{};
  auto runtime = std::make_shared<AllocationRangeStub>();
  runtime->location_type = ACL_MEM_LOCATION_TYPE_HOST;
  runtime->location_id = 2U;
  llm::AclRuntimeStub::SetInstance(runtime);
  ScopeGuard reset([] { llm::AclRuntimeStub::Reset(); });
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  native->Device()->resolve_device_memory = true;
  CommMem first_mem{COMM_MEM_TYPE_DEVICE, buf + 4, 8};
  MemHandle first = nullptr;
  ASSERT_EQ(ep->RegisterMem("host_reg", first_mem, first), SUCCESS);
  EXPECT_EQ(runtime->attr_calls, 1);
  EXPECT_EQ(runtime->range_calls, 0);
  EXPECT_FALSE(verbs->last_registered_device_memory.load());
  EXPECT_EQ(verbs->last_registered_memory_device.load(), UINT32_MAX);
  std::shared_ptr<HostRoceLocalMemory> found;
  ASSERT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(buf + 4), 8, found), SUCCESS);
  EXPECT_EQ(found->memory.type, COMM_MEM_TYPE_HOST);
  EXPECT_EQ(found->mr->base, reinterpret_cast<uint64_t>(buf + 4));
  EXPECT_EQ(found->mr->size, 8U);
  EXPECT_EQ(found->mr->memory_device, UINT32_MAX);

  runtime->location_type = ACL_MEM_LOCATION_TYPE_UNREGISTERED;
  CommMem second_mem{COMM_MEM_TYPE_DEVICE, buf + 20, 12};
  MemHandle second = nullptr;
  ASSERT_EQ(ep->RegisterMem("unreg", second_mem, second), SUCCESS);
  EXPECT_EQ(runtime->attr_calls, 2);
  EXPECT_EQ(runtime->range_calls, 0);
  EXPECT_FALSE(verbs->last_registered_device_memory.load());
  EXPECT_EQ(verbs->last_registered_memory_device.load(), UINT32_MAX);
  ASSERT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(buf + 20), 12, found), SUCCESS);
  EXPECT_EQ(found->memory.type, COMM_MEM_TYPE_HOST);
  EXPECT_EQ(found->mr->base, reinterpret_cast<uint64_t>(buf + 20));
  EXPECT_EQ(found->mr->size, 12U);
  EXPECT_EQ(found->mr->memory_device, UINT32_MAX);

  found.reset();
  ASSERT_EQ(ep->DeregisterMem(first), SUCCESS);
  ASSERT_EQ(ep->DeregisterMem(second), SUCCESS);
  {
    std::lock_guard<std::mutex> lock(native->Device()->memory_mutex);
    EXPECT_TRUE(native->Device()->registrations.empty());
  }
}
TEST_F(HostRoceUt, RegistrationRangeFallsBackToDeclaredDeviceMemoryOnAttributesFailure) {
  auto runtime = std::make_shared<AllocationRangeStub>();
  runtime->attr_result = ACL_ERROR_RT_INTERNAL_ERROR;
  runtime->current_device_id = 4;
  llm::AclRuntimeStub::SetInstance(runtime);
  ScopeGuard reset([] { llm::AclRuntimeStub::Reset(); });
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  native->Device()->resolve_device_memory = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);

  char allocation[64]{};
  runtime->allocation = allocation;
  runtime->bytes = sizeof(allocation);
  char remote_a[16] = "attr-fail-dev1";
  MemHandle handle_a = nullptr;
  Memory(allocation + 4, remote_a, sizeof(remote_a), handle_a, COMM_MEM_TYPE_DEVICE);
  EXPECT_EQ(runtime->attr_calls, 1);
  EXPECT_EQ(runtime->device_calls, 1);
  EXPECT_EQ(runtime->range_calls, 0);
  EXPECT_TRUE(verbs->last_registered_device_memory.load());
  EXPECT_EQ(verbs->last_registered_memory_device.load(), 4U);

  std::shared_ptr<HostRoceLocalMemory> found;
  ASSERT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(allocation + 4), sizeof(remote_a), found), SUCCESS);
  EXPECT_EQ(found->memory.type, COMM_MEM_TYPE_DEVICE);
  EXPECT_EQ(found->mr->base, reinterpret_cast<uint64_t>(allocation + 4));
  EXPECT_EQ(found->mr->size, sizeof(remote_a));
  EXPECT_EQ(found->mr->memory_device, 4U);

  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(ep->ExportMem(exported), SUCCESS);
  bool saw_local_export = false;
  for (const auto &desc : exported) {
    if (desc.tag == "local") {
      HostRoceMemDesc wire{};
      ASSERT_EQ(DecodeHostRoceMem(desc.export_desc, desc.export_len, wire), SUCCESS);
      EXPECT_EQ(wire.type, static_cast<uint32_t>(COMM_MEM_TYPE_DEVICE));
      saw_local_export = true;
    }
  }
  EXPECT_TRUE(saw_local_export);

  const HixlOneSideOpDesc op_a{remote_a, allocation + 4, sizeof(remote_a)};
  ASSERT_EQ(channel->BatchGetSync(&op_a, 1, 2000), SUCCESS);
  EXPECT_STREQ(allocation + 4, remote_a);
  EXPECT_EQ(verbs->flushed_devices, (std::vector<uint32_t>{4U}));

  // If aclrtGetDevice fails in the fallback path, registration fails without creating an MR.
  runtime->device_result = ACL_ERROR_RT_INTERNAL_ERROR;
  char local_c[16]{};
  CommMem bad_mem{COMM_MEM_TYPE_DEVICE, local_c, sizeof(local_c)};
  MemHandle bad_handle = nullptr;
  EXPECT_NE(ep->RegisterMem("dev_fail", bad_mem, bad_handle), SUCCESS);

  found.reset();
  ASSERT_EQ(ep->DeregisterMem(handle_a), SUCCESS);
  ASSERT_EQ(channel->Destroy(), SUCCESS);
  {
    std::lock_guard<std::mutex> lock(native->Device()->memory_mutex);
    EXPECT_TRUE(native->Device()->registrations.empty());
  }
}
TEST_F(HostRoceUt, ExpiredPartialHandshakeDestroysPreparedQp) {
  HixlCSServer server("127.0.0.1", 26597, NativeConfig());
  auto desc = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&desc, 1), SUCCESS);
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  ASSERT_EQ(server.Listen(8), SUCCESS);
  auto connection = std::make_shared<CtrlMsgPlugin>();
  ASSERT_EQ(connection->Initialize(), SUCCESS);
  ASSERT_EQ(connection->Connect("127.0.0.1", 26597, 1000), SUCCESS);
  ASSERT_EQ(ConnMsgHandler::SendMatchEndpointRequest(connection, desc), SUCCESS);
  uint64_t remote_ep_handle = 0U;
  uint32_t remote_port = 0U;
  uint64_t channel_index = 0U;
  ASSERT_EQ(ConnMsgHandler::RecvMatchEndpointResponse(connection, remote_ep_handle, remote_port, channel_index, 1000),
            SUCCESS);
  CreateChannelReq create_req{desc, remote_ep_handle, 0U, 0U, 0U, 0U, channel_index, kQosUnset, 1000U};
  ASSERT_EQ(ConnMsgHandler::SendCreateChannelRequest(connection, create_req), SUCCESS);
  ASSERT_EQ(ConnMsgHandler::RecvCreateChannelResponse(connection, 1000), SUCCESS);
  {
    std::lock_guard<std::mutex> lock(server.chn_mutex_);
    ASSERT_EQ(server.channel_handshake_deadlines_.size(), 1U);
    EXPECT_EQ(server.pending_channel_handshakes_.load(), 1U);
    server.channel_handshake_deadlines_.begin()->second = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  }
  server.ExpireHandshakes();
  EXPECT_EQ(server.pending_channel_handshakes_.load(), 0U);
  for (int i = 0; i < 100 && verbs->stopped == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  EXPECT_EQ(verbs->stopped, 1);
}
TEST_F(HostRoceUt, DuplicateCreateChannelRejectedBeforeCreatingQp) {
  constexpr uint32_t kPort = 26671;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  auto desc = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&desc, 1), SUCCESS);
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  ASSERT_EQ(server.Listen(8), SUCCESS);
  auto connection = std::make_shared<CtrlMsgPlugin>();
  ASSERT_EQ(connection->Initialize(), SUCCESS);
  ASSERT_EQ(connection->Connect("127.0.0.1", kPort, 1000), SUCCESS);
  ASSERT_EQ(ConnMsgHandler::SendMatchEndpointRequest(connection, desc), SUCCESS);
  uint64_t remote_ep_handle = 0U;
  uint32_t remote_port = 0U;
  uint64_t channel_index = 0U;
  ASSERT_EQ(ConnMsgHandler::RecvMatchEndpointResponse(connection, remote_ep_handle, remote_port, channel_index, 1000),
            SUCCESS);
  CreateChannelReq create_req{desc, remote_ep_handle, 0U, 0U, 0U, 0U, channel_index, kQosUnset, 1000U};
  ASSERT_EQ(ConnMsgHandler::SendCreateChannelRequest(connection, create_req), SUCCESS);
  ASSERT_EQ(ConnMsgHandler::RecvCreateChannelResponse(connection, 1000), SUCCESS);
  const uint32_t created_after_first = verbs->created.load();
  EXPECT_EQ(created_after_first, 1U);
  ASSERT_EQ(ConnMsgHandler::SendCreateChannelRequest(connection, create_req), SUCCESS);
  EXPECT_NE(ConnMsgHandler::RecvCreateChannelResponse(connection, 1000), SUCCESS);
  EXPECT_EQ(verbs->created.load(), created_after_first);
}
TEST_F(HostRoceUt, CreateChannelAtHandshakeLimitRejectedBeforeCreatingQp) {
  constexpr uint32_t kPort = 26672;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  auto desc = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&desc, 1), SUCCESS);
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  ASSERT_EQ(server.Listen(8), SUCCESS);
  auto connection = std::make_shared<CtrlMsgPlugin>();
  ASSERT_EQ(connection->Initialize(), SUCCESS);
  ASSERT_EQ(connection->Connect("127.0.0.1", kPort, 1000), SUCCESS);
  ASSERT_EQ(ConnMsgHandler::SendMatchEndpointRequest(connection, desc), SUCCESS);
  uint64_t remote_ep_handle = 0U;
  uint32_t remote_port = 0U;
  uint64_t channel_index = 0U;
  ASSERT_EQ(ConnMsgHandler::RecvMatchEndpointResponse(connection, remote_ep_handle, remote_port, channel_index, 1000),
            SUCCESS);
  {
    std::lock_guard<std::mutex> lock(server.chn_mutex_);
    const auto future = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    for (size_t i = 0; i < HixlCSServer::kMaxPendingHandshakes; ++i) {
      server.channel_handshake_deadlines_[std::make_shared<CtrlMsgPlugin>()] = future;
    }
    server.pending_channel_handshakes_.store(server.channel_handshake_deadlines_.size(), std::memory_order_release);
  }
  CreateChannelReq create_req{desc, remote_ep_handle, 0U, 0U, 0U, 0U, channel_index, kQosUnset, 1000U};
  ASSERT_EQ(ConnMsgHandler::SendCreateChannelRequest(connection, create_req), SUCCESS);
  EXPECT_NE(ConnMsgHandler::RecvCreateChannelResponse(connection, 1000), SUCCESS);
  EXPECT_EQ(verbs->created.load(), 0U);
}
TEST_F(HostRoceUt, CreateChannelFailureRollsBackReservedHandshakeSlot) {
  constexpr uint32_t kPort = 26673;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  auto desc = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&desc, 1), SUCCESS);
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  ASSERT_EQ(server.Listen(8), SUCCESS);
  auto connection = std::make_shared<CtrlMsgPlugin>();
  ASSERT_EQ(connection->Initialize(), SUCCESS);
  ASSERT_EQ(connection->Connect("127.0.0.1", kPort, 1000), SUCCESS);
  ASSERT_EQ(ConnMsgHandler::SendMatchEndpointRequest(connection, desc), SUCCESS);
  uint64_t remote_ep_handle = 0U;
  uint32_t remote_port = 0U;
  uint64_t channel_index = 0U;
  ASSERT_EQ(ConnMsgHandler::RecvMatchEndpointResponse(connection, remote_ep_handle, remote_port, channel_index, 1000),
            SUCCESS);
  verbs->create_qp_result = FAILED;
  CreateChannelReq create_req{desc, remote_ep_handle, 0U, 0U, 0U, 0U, channel_index, kQosUnset, 1000U};
  ASSERT_EQ(ConnMsgHandler::SendCreateChannelRequest(connection, create_req), SUCCESS);
  EXPECT_NE(ConnMsgHandler::RecvCreateChannelResponse(connection, 1000), SUCCESS);
  std::lock_guard<std::mutex> lock(server.chn_mutex_);
  EXPECT_TRUE(server.channel_handshake_deadlines_.empty());
  EXPECT_EQ(server.pending_channel_handshakes_.load(), 0U);
}
TEST_F(HostRoceUt, HandshakeBeforeCreateChannelIsRejected) {
  HixlCSServer server("127.0.0.1", 26598, NativeConfig());
  auto desc = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&desc, 1), SUCCESS);
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  ASSERT_EQ(server.Listen(8), SUCCESS);
  auto conn = std::make_shared<CtrlMsgPlugin>();
  ASSERT_EQ(conn->Initialize(), SUCCESS);
  ASSERT_EQ(conn->Connect("127.0.0.1", 26598, 1000), SUCCESS);
  HostRoceHandshake wire;
  wire.step = HostRoceHandshakeStep::kPrepare;
  ASSERT_EQ(ConnMsgHandler::SendChannelHandshakeRequest(conn, EncodeHostRoceHandshake(wire)), SUCCESS);
  std::vector<uint8_t> resp_payload;
  bool server_done = false;
  EXPECT_EQ(ConnMsgHandler::RecvChannelHandshakeResponse(conn, 1000, resp_payload, server_done), PARAM_INVALID);
}
TEST_F(HostRoceUt, FailedDeregistrationKeepsLogicalRegistrationForRetry) {
  char local[16]{};
  CommMem memory{COMM_MEM_TYPE_HOST, local, sizeof(local)};
  MemHandle handle = nullptr;
  ASSERT_EQ(ep->RegisterMem("data", memory, handle), SUCCESS);
  verbs->deregister_result = FAILED;
  EXPECT_NE(ep->DeregisterMem(handle), SUCCESS);
  HixlMemDesc desc;
  EXPECT_EQ(ep->GetMemDesc(handle, desc), SUCCESS);
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  std::shared_ptr<HostRoceLocalMemory> found;
  EXPECT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(local), sizeof(local), found), SUCCESS);
  found.reset();
  verbs->deregister_result = SUCCESS;
  EXPECT_EQ(ep->DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(native->FindLocal(reinterpret_cast<uint64_t>(local), sizeof(local), found), PARAM_INVALID);
}
TEST_F(HostRoceUt, NativeTimeoutRejectsValuesOutsideVerbsEncoding) {
  ASSERT_EQ(setenv("HCCL_RDMA_TIMEOUT", "100", 1), 0);
  ScopeGuard restore([] { unsetenv("HCCL_RDMA_TIMEOUT"); });
  HixlCSClient client;
  ScopeGuard destroy([&] { (void)client.Destroy(); });
  const auto endpoint = NativeEndpoint();
  HixlClientDesc desc{};
  desc.local_endpoint = &endpoint;
  desc.remote_endpoint = &endpoint;
  desc.server_ip = "127.0.0.1";
  desc.server_port = 26597;
  HixlClientConfig config{};
  config.global_resource_config = kRoceHixlBackendJson;
  EXPECT_EQ(client.Create(&desc, &config), PARAM_INVALID);
}
TEST_F(HostRoceUt, FullClientServerControlAndReadWriteUseNativeBackend) {
  verbs->more_rounds = 2;
  ResetMemRegRecord();
  ResetTransferCounter();
  ResetChannelCreateRecord();
  HixlCSServer server("127.0.0.1", 26597, NativeConfig());
  const auto endpoint = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&endpoint, 1), SUCCESS);
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  char remote[16]{};
  CommMem memory{};
  memory.type = COMM_MEM_TYPE_HOST;
  memory.addr = remote;
  memory.size = sizeof(remote);
  MemHandle mem = nullptr;
  ASSERT_EQ(server.RegisterMem("data", &memory, &mem), SUCCESS);
  ASSERT_EQ(server.Listen(8), SUCCESS);
  HixlCSClient client;
  ScopeGuard destroy([&] { (void)client.Destroy(); });
  const auto local_endpoint = NativeEndpoint();
  HixlClientDesc desc{};
  desc.local_endpoint = &local_endpoint;
  desc.remote_endpoint = &endpoint;
  desc.server_ip = "127.0.0.1";
  desc.server_port = 26597;
  desc.tc = 132;
  desc.sl = 4;
  HixlClientConfig config{};
  config.global_resource_config = kRoceHixlBackendJson;
  ASSERT_EQ(client.Create(&desc, &config), SUCCESS);
  ASSERT_EQ(client.Connect(3000), SUCCESS);
  char local[] = "0123456789abcde";
  memory.addr = local;
  ASSERT_EQ(client.RegMem("local", &memory, &mem), SUCCESS);
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(client.BatchPutSync(&op, 1, 1000), SUCCESS);
  EXPECT_STREQ(local, remote);
  std::memset(local, 0, sizeof(local));
  ASSERT_EQ(client.BatchGetSync(&op, 1, 1000), SUCCESS);
  EXPECT_STREQ(local, remote);
  EXPECT_EQ(GetMemRegCallCount(), 0);
  EXPECT_EQ(GetNbiCallCount(), 0);
  EXPECT_EQ(GetFenceCallCount(), 0);
  HcommChannelDesc recorded{};
  EXPECT_FALSE(GetLastChannelCreateDesc(&recorded));
  EXPECT_EQ(verbs->created, 2);
  ASSERT_EQ(verbs->created_configs.size(), 2);
  EXPECT_EQ(verbs->created_configs[0].tc, 132);
  EXPECT_EQ(verbs->created_configs[0].sl, 4);
  EXPECT_EQ(verbs->created_configs[1].tc, 132);
  EXPECT_EQ(verbs->created_configs[1].sl, 4);
  ASSERT_EQ(verbs->connected_planes.size(), 2);
  EXPECT_EQ(verbs->connected_planes[0], verbs->connected_planes[1]);
  uint8_t server_first_input = 0;
  bool saw_server = false;
  for (const auto &input : verbs->nego_inputs) {
    if (input.local_qpn != 2) continue;
    server_first_input = input.feature1;
    saw_server = true;
    break;
  }
  ASSERT_TRUE(saw_server);
  EXPECT_EQ(server_first_input, 0xA5);
}
TEST_F(HostRoceUt, ProviderMtuSixCompletesHandshakeAndReadWrite) {
  verbs->mtu = 6;
  verbs->more_rounds = 2;
  dynamic_cast<HostRoceEndpoint *>(ep.get())->Device()->local.mtu = 6;
  HixlCSServer server("127.0.0.1", 26630, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, 26630, nullptr));
  char local[] = "provider mtu 6";
  char remote[sizeof(local)]{};
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local)));
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(client.BatchPutSync(&op, 1, 1000), SUCCESS);
  EXPECT_STREQ(local, remote);
  std::memset(local, 0, sizeof(local));
  ASSERT_EQ(client.BatchGetSync(&op, 1, 1000), SUCCESS);
  EXPECT_STREQ(local, remote);
  ASSERT_EQ(verbs->connected_mtus.size(), 2);
  for (const auto &mtu : verbs->connected_mtus) {
    EXPECT_EQ(mtu.first, 6U);
    EXPECT_EQ(mtu.second, 6U);
  }
}
TEST_F(HostRoceUt, ZeroPeerMtuIsRejectedWithoutConsumingPeerState) {
  auto channel = NewChannel();
  ASSERT_NE(channel, nullptr);
  auto peer = channel->LocalPeer();
  peer.qpn += 100;
  peer.mtu = 0;
  EXPECT_EQ(channel->SetPeer(peer), PARAM_INVALID);
  EXPECT_TRUE(verbs->connected_mtus.empty());
  peer.mtu = channel->LocalPeer().mtu;
  EXPECT_EQ(channel->SetPeer(peer), SUCCESS);
}
TEST_F(HostRoceUt, SplitBatchesDrainBeforeHostFlagReadAndQueryRequiresFlagValue) {
  verbs->qp_capacity = 128;
  verbs->defer_copies = true;
  HixlCSServer server("127.0.0.1", 26624, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, 26624, "{\"transfer_config.max_transfer_count_per_batch\":2}"));
  char local[] = "0123456789abcdefghijklmnopqrstuvwxyz123";
  char remote[sizeof(local)]{};
  CommMem memory{};
  memory.type = COMM_MEM_TYPE_HOST;
  memory.addr = remote;
  memory.size = sizeof(remote);
  MemHandle mem = nullptr;
  ASSERT_EQ(server.RegisterMem("data", &memory, &mem), SUCCESS);
  CommMem *mems = nullptr;
  char **tags = nullptr;
  uint32_t count = 0;
  ASSERT_EQ(client.GetRemoteMem(&mems, &tags, &count, 1000), SUCCESS);
  memory.addr = local;
  ASSERT_EQ(client.RegMem("local", &memory, &mem), SUCCESS);
  std::vector<HixlOneSideOpDesc> ops;
  for (size_t i = 0; i < 5; ++i) ops.push_back({remote + i * 8, local + i * 8, 8});
  ASSERT_NE(server.dev_trans_flag_, nullptr);
  EXPECT_EQ(*reinterpret_cast<uint64_t *>(server.dev_trans_flag_), 1ULL);
  // Keep the remote flag at 0 to distinguish the trailing CQE from completion.
  *reinterpret_cast<uint64_t *>(server.dev_trans_flag_) = 0ULL;
  void *handle = nullptr;
  ASSERT_EQ(client.BatchPutAsync(ops.data(), ops.size(), &handle), SUCCESS);
  EXPECT_EQ(verbs->PostCount(), 10U);
  EXPECT_EQ(verbs->polled_completions.load(), 0U);
  HixlCompleteStatus status{};
  ASSERT_EQ(client.QueryCompleteStatus(handle, &status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_WAITING);
  EXPECT_EQ(verbs->PostCount(), 12U);
  ASSERT_EQ(client.QueryCompleteStatus(handle, &status), SUCCESS);
  EXPECT_EQ(verbs->polled_completions.load(), 12U);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_WAITING);
  EXPECT_EQ(std::memcmp(local, remote, 40), 0);
  const auto posts = verbs->PostsSnapshot();
  ASSERT_EQ(posts.size(), 12U);
  {
    std::lock_guard<std::mutex> lock(verbs->mutex);
    EXPECT_EQ(verbs->batch_sizes, (std::vector<size_t>{4, 4, 2, 2}));
  }
  EXPECT_EQ(verbs->generated_completions.load(), 12U);
  // Intra-request data batches pipeline continuously into the SQ without draining or mid-request fences.
  for (size_t i = 0; i < 10U; ++i) {
    EXPECT_FALSE(posts[i].fence);
    EXPECT_TRUE(posts[i].signaled);
  }
  EXPECT_EQ(posts[4].pending_before, 4U);
  EXPECT_EQ(posts[8].pending_before, 8U);
  // Only the trailing HBM completion-flag read waits for all data CQEs to drain and sets IBV_SEND_FENCE.
  EXPECT_EQ(posts[10].pending_before, 0U);
  EXPECT_TRUE(posts[10].fence);
  EXPECT_TRUE(posts[10].signaled);
  EXPECT_TRUE(posts[10].read);
  EXPECT_TRUE(posts[11].read);
  EXPECT_TRUE(posts[11].fence);
  auto *flag_address = reinterpret_cast<uint64_t *>(posts[10].local);
  EXPECT_EQ(__atomic_load_n(flag_address, __ATOMIC_ACQUIRE), 0ULL);
  __atomic_store_n(flag_address, 1ULL, __ATOMIC_RELEASE);
  EXPECT_EQ(WaitAsyncStatus(client, handle, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_EQ(client.QueryCompleteStatus(handle, &status), PARAM_INVALID);
}
TEST_F(HostRoceUt, HostFlagHandlesCanBeQueriedOutOfOrder) {
  verbs->qp_capacity = 128;
  verbs->defer_copies = true;
  HixlCSServer server("127.0.0.1", 26625, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, 26625, nullptr));
  char local[8] = "abc";
  char remote[8]{};
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local)));
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  void *first = nullptr, *second = nullptr;
  ASSERT_EQ(client.BatchPutAsync(&op, 1, &first), SUCCESS);
  ASSERT_EQ(client.BatchPutAsync(&op, 1, &second), SUCCESS);
  HixlCompleteStatus status{};
  EXPECT_EQ(WaitAsyncStatus(client, second, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_EQ(WaitAsyncStatus(client, first, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  void *next = nullptr;
  ASSERT_EQ(client.BatchPutAsync(&op, 1, &next), SUCCESS);
  EXPECT_NE(first, next);
  EXPECT_EQ(client.QueryCompleteStatus(first, &status), PARAM_INVALID);
  EXPECT_EQ(WaitAsyncStatus(client, next, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_STREQ(local, remote);
}
TEST_F(HostRoceUt, HostFlagReadErrorFailsSyncAndLatchesClientFailure) {
  verbs->qp_capacity = 128;
  HixlCSServer server("127.0.0.1", 26626, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, 26626, nullptr));
  char local[8] = "abc";
  char remote[8]{};
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local)));
  verbs->read_completion_error = 10;
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  EXPECT_EQ(client.BatchPutSync(&op, 1, 1000), FAILED);
  EXPECT_EQ(verbs->stopped, 1);
  EXPECT_EQ(client.BatchPutSync(&op, 1, 1000), FAILED);
}
TEST_F(HostRoceUt, SyncDeadlineCoversDataFenceAndHostFlagSubmission) {
  verbs->qp_capacity = 128;
  HixlCSServer server("127.0.0.1", 26627, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, 26627, nullptr));
  char local[8] = "abc";
  char remote[8]{};
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local)));
  verbs->delay_poll_ms = 20;
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  EXPECT_EQ(client.BatchPutSync(&op, 1, 5), TIMEOUT);
  EXPECT_EQ(verbs->stopped, 1);
  EXPECT_EQ(client.BatchPutSync(&op, 1, 1000), FAILED);
}
TEST_F(HostRoceUt, QueuedHostFlagCompletionWinsOverDeadline) {
  verbs->qp_capacity = 128;
  HixlCSServer server("127.0.0.1", 26628, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, 26628, nullptr));
  char local[8] = "abc";
  char remote[8]{};
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local)));
  // The flag read is accepted and its CQE is queued before the post call returns late.
  verbs->read_post_delay_ms = 20;
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  EXPECT_EQ(client.BatchPutSync(&op, 1, 5), SUCCESS);
  EXPECT_EQ(verbs->stopped, 0);
}
TEST_F(HostRoceUt, UnknownHandleQueryIsRejectedWithoutKillingQp) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[4] = "abc";
  char remote[4]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(channel->BatchPutSync(&op, 1, 2000), SUCCESS);
  EXPECT_EQ(verbs->stopped.load(), 0U);
  HixlCompleteStatus status{};
  EXPECT_EQ(channel->QueryCompleteStatus(reinterpret_cast<void *>(0x1), &status), PARAM_INVALID);
  EXPECT_EQ(verbs->stopped.load(), 0U);
  ASSERT_EQ(channel->BatchPutSync(&op, 1, 2000), SUCCESS);
  EXPECT_STREQ(local, remote);
  EXPECT_EQ(ep->DeregisterMem(mem), SUCCESS);
}

TEST_F(HostRoceUt, DisconnectKeepsChannelContextAndRegistrationForRetry) {
  HixlMemStore store;
  GlobalConfig config;
  auto channel = std::make_shared<HostRoceChannel>(*ep);
  const ChannelContext context{ep.get(), -1, &config, &store, true};
  ASSERT_EQ(channel->Initialize(context), SUCCESS);
  ChannelDesc desc{};
  desc.remote_endpoint = NativeEndpoint();
  desc.tc = 132;
  desc.sl = 4;
  desc.retry_cnt = 7;
  desc.retry_interval = 20;
  ASSERT_EQ(channel->Connect(desc, 1000), SUCCESS);
  Ready(channel);
  EXPECT_EQ(channel->Disconnect(), SUCCESS);
  char local[8]{};
  CommMem memory{};
  memory.type = COMM_MEM_TYPE_HOST;
  memory.addr = local;
  memory.size = sizeof(local);
  MemHandle handle = nullptr;
  ASSERT_EQ(channel->RegMem("retry", &memory, &handle), SUCCESS);
  ASSERT_EQ(channel->Connect(desc, 1000), SUCCESS);
  Ready(channel);
  EXPECT_EQ(channel->UnregMem(handle), SUCCESS);
  EXPECT_EQ(channel->SetRemoteMemInfo(CommMem{}), SUCCESS);
  EXPECT_EQ(channel->Destroy(), SUCCESS);
  EXPECT_EQ(channel->RegMem("destroyed", &memory, &handle), FAILED);
  EXPECT_EQ(channel->Destroy(), SUCCESS);
}
TEST_F(HostRoceUt, DelayedPollAsyncTransferCompletesWithoutKillingQp) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  verbs->delay_poll_ms = 20;
  char local[4] = "xyz";
  char remote[4]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  void *request = nullptr;
  ASSERT_EQ(channel->BatchPutAsync(&op, 1, &request), SUCCESS);
  ASSERT_NE(request, nullptr);
  HixlCompleteStatus status{};
  EXPECT_EQ(WaitChannelAsync(channel, request, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_EQ(verbs->stopped.load(), 0U);
  EXPECT_STREQ(local, remote);
  EXPECT_EQ(ep->DeregisterMem(mem), SUCCESS);
}
TEST_F(HostRoceUt, ZeroMaxMessageSizeFailsInsteadOfSpinning) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  native->Device()->max_message_size = 0;
  char local[4]{};
  char remote[4]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  EXPECT_EQ(channel->BatchPutSync(&op, 1, 1000), PARAM_INVALID);
  EXPECT_EQ(verbs->PostCount(), 0U);
  EXPECT_EQ(verbs->stopped.load(), 1U);
}

std::string WriteQosCfg(const std::string &body) {
  char name[] = "/tmp/hixl_host_roce_qos_XXXXXX";
  const int fd = mkstemp(name);
  EXPECT_GE(fd, 0);
  if (fd < 0) return {};
  const ssize_t wrote = write(fd, body.data(), body.size());
  EXPECT_EQ(wrote, static_cast<ssize_t>(body.size()));
  EXPECT_EQ(close(fd), 0);
  return name;
}

void ExpectSlTc(uint32_t qos, uint32_t phy_id, const std::string &path, uint8_t sl, uint8_t tc) {
  uint8_t actual_tc = 0xFF;
  uint8_t actual_sl = 0xFF;
  EXPECT_EQ(ResolveHostRoceQos(qos, phy_id, path, actual_tc, actual_sl), SUCCESS);
  EXPECT_EQ(actual_sl, sl);
  EXPECT_EQ(actual_tc, tc);
}

TEST(HostRoceQosUt, QosDscpMapsBusinessQosToSlAndTc) {
  const std::string path = WriteQosCfg(
      "# comment\n"
      "qos_dscp_0=0:33,1:65\n"
      "qos_dscp_2=1:no\n"
      "qos_dscp_3=\n"
      "qos_dscp_4=0:1,1:2,2:3,3:4,4:5,5:6,6:7,7:8,0:9\n"
      "qos_dscp_5=0:33, 1:65\n"
      "qos_dscp_6=1:40\r\n"
      "qos_dscp_7=0:33,\n"
      "qos_dscp_8=8:1\n"
      "qos_dscp_9=0:256\n"
      "qos_dscp_10=0:1,0:2,0:3,0:4,0:5,0:6,0:7,0:8,1:65\n");
  ASSERT_FALSE(path.empty());
  ScopeGuard remove([&] { unlink(path.c_str()); });
  ExpectSlTc(1, 0, path, 1, static_cast<uint8_t>(65U << 2));
  ExpectSlTc(2, 0, path, 2, 132);
  ExpectSlTc(1, 1, path, 1, 132);
  ExpectSlTc(1, 3, path, 1, 132);
  ExpectSlTc(0, 4, path, 0, 4);
  uint8_t tc = 0;
  uint8_t sl = 0;
  ExpectSlTc(1, 6, path, 1, static_cast<uint8_t>(40U << 2));
  ExpectSlTc(1, 10, path, 1, 132);
  EXPECT_EQ(ResolveHostRoceQos(1, 2, path, tc, sl), PARAM_INVALID);
  EXPECT_EQ(ResolveHostRoceQos(1, 5, path, tc, sl), PARAM_INVALID);
  EXPECT_EQ(ResolveHostRoceQos(1, 7, path, tc, sl), PARAM_INVALID);
  EXPECT_EQ(ResolveHostRoceQos(1, 8, path, tc, sl), PARAM_INVALID);
  EXPECT_EQ(ResolveHostRoceQos(1, 9, path, tc, sl), PARAM_INVALID);
  EXPECT_EQ(ResolveHostRoceQos(8, 0, path, tc, sl), PARAM_INVALID);
  EXPECT_EQ(ResolveHostRoceQos(1, 0, "/tmp/hixl_host_roce_qos_missing", tc, sl), SUCCESS);
  EXPECT_EQ(sl, 1);
  EXPECT_EQ(tc, 132);
  EXPECT_EQ(ResolveHostRoceQos(1, 0, "/tmp", tc, sl), SUCCESS);
  EXPECT_EQ(sl, 1);
  EXPECT_EQ(tc, 132);
  const std::string huge = WriteQosCfg("qos_dscp_0=" + std::string(2200, '1') + "\n");
  ASSERT_FALSE(huge.empty());
  ScopeGuard remove_huge([&] { unlink(huge.c_str()); });
  EXPECT_EQ(ResolveHostRoceQos(1, 0, huge, tc, sl), PARAM_INVALID);
}

class PhyIdFailStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetPhyDevIdByUserDevId(const int32_t, int32_t *const) override {
    return ACL_ERROR_RT_INTERNAL_ERROR;
  }
};

TEST(HostRoceQosUt, DeviceLookupFailureKeepsDefaultDscp) {
  uint8_t tc = 9;
  uint8_t sl = 3;
  EXPECT_EQ(ApplyHostRoceQos(kHostRoceQosNotSet, tc, sl), SUCCESS);
  EXPECT_EQ(tc, 9);
  EXPECT_EQ(sl, 3);
  llm::GetAclStubMock() = "aclrtGetDevice";
  ScopeGuard clear_mock([] { llm::GetAclStubMock().clear(); });
  EXPECT_EQ(ApplyHostRoceQos(1, tc, sl), SUCCESS);
  EXPECT_EQ(sl, 1);
  EXPECT_EQ(tc, 132);
  EXPECT_EQ(ApplyHostRoceQos(8, tc, sl), PARAM_INVALID);
  clear_mock.Dismiss();
  llm::GetAclStubMock().clear();
  auto runtime = std::make_shared<PhyIdFailStub>();
  llm::AclRuntimeStub::SetInstance(runtime);
  ScopeGuard reset([] { llm::AclRuntimeStub::Reset(); });
  EXPECT_EQ(ApplyHostRoceQos(2, tc, sl), SUCCESS);
  EXPECT_EQ(sl, 2);
  EXPECT_EQ(tc, 132);
}

TEST_F(HostRoceUt, ConfiguredQosReplacesTcSlFromLocalQosDscp) {
  const std::string path = WriteQosCfg("qos_dscp_0=1:65\n");
  ASSERT_FALSE(path.empty());
  ScopeGuard remove([&] {
    SetHostRoceQosCfgPathForTest("");
    unlink(path.c_str());
  });
  SetHostRoceQosCfgPathForTest(path);
  ChannelDesc kept{};
  kept.remote_endpoint = NativeEndpoint("127.0.0.2");
  kept.tc = 132;
  kept.sl = 4;
  ChannelHandle kept_handle = 0;
  ASSERT_EQ(ep->CreateChannel(kept, kept_handle, 1000), SUCCESS);
  ChannelDesc mapped{};
  mapped.remote_endpoint = NativeEndpoint("127.0.0.3");
  mapped.tc = 132;
  mapped.sl = 4;
  mapped.qos = 1;
  ChannelHandle mapped_handle = 0;
  ASSERT_EQ(ep->CreateChannel(mapped, mapped_handle, 1000), SUCCESS);
  ASSERT_EQ(verbs->created_configs.size(), 2);
  EXPECT_EQ(verbs->created_configs[0].tc, 132);
  EXPECT_EQ(verbs->created_configs[0].sl, 4);
  EXPECT_EQ(verbs->created_configs[1].tc, static_cast<uint8_t>(65U << 2));
  EXPECT_EQ(verbs->created_configs[1].sl, 1);
  SetHostRoceQosCfgPathForTest("");
  const std::string invalid = WriteQosCfg("qos_dscp_0=1:xx\n");
  ASSERT_FALSE(invalid.empty());
  ScopeGuard remove_invalid([&] { unlink(invalid.c_str()); });
  SetHostRoceQosCfgPathForTest(invalid);
  ChannelDesc bad = mapped;
  bad.remote_endpoint = NativeEndpoint("127.0.0.4");
  ChannelHandle bad_handle = 0;
  EXPECT_EQ(ep->CreateChannel(bad, bad_handle, 1000), PARAM_INVALID);
  EXPECT_EQ(verbs->created, 2);
}

TEST_F(HostRoceUt, BothPeersMapBusinessQosWithLocalQosDscp) {
  const std::string path = WriteQosCfg("qos_dscp_0=1:65\nqos_dscp_1=1:33\n");
  ASSERT_FALSE(path.empty());
  ScopeGuard remove([&] {
    SetHostRoceQosCfgPathForTest("");
    unlink(path.c_str());
  });
  SetHostRoceQosCfgPathForTest(path);
  constexpr uint32_t kPort = 26623;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  const auto endpoint = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&endpoint, 1), SUCCESS);
  ScopeGuard cleanup([&] { (void)server.Finalize(); });
  char remote[16]{};
  CommMem memory{};
  memory.type = COMM_MEM_TYPE_HOST;
  memory.addr = remote;
  memory.size = sizeof(remote);
  MemHandle mem = nullptr;
  ASSERT_EQ(server.RegisterMem("data", &memory, &mem), SUCCESS);
  ASSERT_EQ(server.Listen(8), SUCCESS);
  HixlCSClient client;
  ScopeGuard destroy([&] { (void)client.Destroy(); });
  const auto local_endpoint = NativeEndpoint();
  HixlClientDesc desc{};
  desc.local_endpoint = &local_endpoint;
  desc.remote_endpoint = &endpoint;
  desc.server_ip = "127.0.0.1";
  desc.server_port = kPort;
  desc.tc = 132;
  desc.sl = 4;
  const char *qos_json = "{\"comm_resource_config.qos\":1,\"roce_config.hixl_backend\":true}";
  HixlClientConfig config{};
  config.global_resource_config = qos_json;
  ASSERT_EQ(client.Create(&desc, &config), SUCCESS);
  ASSERT_EQ(client.Connect(3000), SUCCESS);
  ASSERT_EQ(verbs->created_configs.size(), 2);
  for (const auto &created : verbs->created_configs) {
    EXPECT_EQ(created.sl, 1);
    EXPECT_EQ(created.tc, static_cast<uint8_t>(65U << 2));
  }
}
TEST_F(HostRoceUt, ConcurrentDisconnectDuringWaitReturnsNotConnected) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[4]{};
  char remote[4]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  verbs->hold_completions = true;
  HixlOneSideOpDesc op{remote, local, sizeof(local)};
  Status wait_ret = SUCCESS;
  std::thread waiter([&] { wait_ret = channel->BatchPutSync(&op, 1, 5000); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (verbs->PostCount() == 0U) {
    ASSERT_LT(std::chrono::steady_clock::now(), deadline);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  EXPECT_EQ(channel->Disconnect(), SUCCESS);
  waiter.join();
  EXPECT_EQ(wait_ret, NOT_CONNECTED);
}
TEST_F(HostRoceUt, MultiPeerRemoteImportAllowsCollidingRegistrationIds) {
  char peer_a[8]{};
  char peer_b[8]{};
  const auto desc_a = EncodeHostRoceMem({reinterpret_cast<uint64_t>(peer_a), 8, 1, 101, COMM_MEM_TYPE_HOST});
  const auto desc_b = EncodeHostRoceMem({reinterpret_cast<uint64_t>(peer_b), 8, 1, 202, COMM_MEM_TYPE_HOST});
  CommMem imported{};
  ASSERT_EQ(ep->ImportMem(desc_a.data(), desc_a.size(), imported), SUCCESS);
  ASSERT_EQ(ep->ImportMem(desc_b.data(), desc_b.size(), imported), SUCCESS);
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  HostRoceMemDesc found{};
  ASSERT_EQ(native->FindRemote(reinterpret_cast<uint64_t>(peer_a), 8, found), SUCCESS);
  EXPECT_EQ(found.rkey, 101U);
  ASSERT_EQ(native->FindRemote(reinterpret_cast<uint64_t>(peer_b), 8, found), SUCCESS);
  EXPECT_EQ(found.rkey, 202U);
  EXPECT_EQ(ep->UnimportMem(desc_a.data(), desc_a.size()), SUCCESS);
  EXPECT_EQ(ep->UnimportMem(desc_b.data(), desc_b.size()), SUCCESS);
}
TEST_F(HostRoceUt, ConfigureRejectsOutOfRangeVerbsAttributes) {
  ChannelDesc bad{};
  bad.remote_endpoint = NativeEndpoint("127.0.0.2");
  bad.sl = 8;
  ChannelHandle handle = 0;
  EXPECT_EQ(ep->CreateChannel(bad, handle, 1000), PARAM_INVALID);
  bad.sl = 4;
  bad.retry_cnt = 8;
  EXPECT_EQ(ep->CreateChannel(bad, handle, 1000), PARAM_INVALID);
  bad.retry_cnt = 7;
  bad.retry_interval = 32;
  EXPECT_EQ(ep->CreateChannel(bad, handle, 1000), PARAM_INVALID);
  EXPECT_EQ(verbs->created, 0U);
}

TEST_F(HostRoceUt, ChannelDisconnectFailsInFlightAsyncHandleAndReleasesMemory) {
  verbs->qp_capacity = 8;
  verbs->hold_completions = true;
  HixlMemStore store;
  GlobalConfig config = NativeConfig();
  auto channel = std::make_shared<HostRoceChannel>(*ep);
  const ChannelContext context{ep.get(), -1, &config, &store, true};
  ASSERT_EQ(channel->Initialize(context), SUCCESS);
  ChannelDesc desc{};
  desc.remote_endpoint = NativeEndpoint("127.0.0.2");
  desc.tc = 132;
  desc.sl = 4;
  desc.retry_cnt = 7;
  desc.retry_interval = 20;
  ASSERT_EQ(channel->Connect(desc, 1000), SUCCESS);
  Ready(channel);

  char local[16] = "disconnect-test";
  char remote[16]{};
  uint64_t remote_flag_val = 1ULL;
  CommMem local_mem{COMM_MEM_TYPE_HOST, local, sizeof(local)};
  MemHandle local_handle = nullptr;
  ASSERT_EQ(channel->RegMem("local", &local_mem, &local_handle), SUCCESS);
  const auto remote_wire =
      EncodeHostRoceMem({reinterpret_cast<uint64_t>(remote), sizeof(remote), 31, 41, COMM_MEM_TYPE_HOST});
  const auto flag_wire = EncodeHostRoceMem(
      {reinterpret_cast<uint64_t>(&remote_flag_val), sizeof(remote_flag_val), 32, 42, COMM_MEM_TYPE_DEVICE});
  CommMem imported{};
  ASSERT_EQ(ep->ImportMem(remote_wire.data(), remote_wire.size(), imported), SUCCESS);
  ASSERT_EQ(store.RecordMemory(true, remote, sizeof(remote), true), SUCCESS);
  ASSERT_EQ(ep->ImportMem(flag_wire.data(), flag_wire.size(), imported), SUCCESS);
  ASSERT_EQ(channel->SetRemoteMemInfo(imported), SUCCESS);

  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  void *handle = nullptr;
  ASSERT_EQ(channel->BatchPutAsync(&op, 1, &handle), SUCCESS);
  ASSERT_NE(handle, nullptr);
  const auto post_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (verbs->PostCount() == 0U) {
    ASSERT_LT(std::chrono::steady_clock::now(), post_deadline);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }

  EXPECT_EQ(channel->Disconnect(), SUCCESS);
  EXPECT_EQ(verbs->stopped.load(), 1U);
  // MRs pinned by the aborted async task must be released immediately after Disconnect stops the QP.
  EXPECT_EQ(channel->UnregMem(local_handle), SUCCESS);
  HixlCompleteStatus status = HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(channel->QueryCompleteStatus(handle, &status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_FAILED);
  EXPECT_EQ(channel->Destroy(), SUCCESS);
}
TEST_F(HostRoceUt, MultiChannelConcurrentAsyncAndDisconnectAreRaceFree) {
  verbs->qp_capacity = 32;
  verbs->defer_copies = true;
  constexpr uint32_t kPort = 26646;
  constexpr size_t kClients = 4;
  constexpr size_t kIters = 12;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  const auto remote_ep = NativeEndpoint("127.0.0.2");
  ASSERT_EQ(server.Initialize(&remote_ep, 1), SUCCESS);
  ScopeGuard server_cleanup([&] { (void)server.Finalize(); });

  std::array<std::array<char, 32>, kClients> remote_bufs{};
  for (size_t c = 0; c < kClients; ++c) {
    CommMem mem{COMM_MEM_TYPE_HOST, remote_bufs[c].data(), remote_bufs[c].size()};
    MemHandle mh = nullptr;
    ASSERT_EQ(server.RegisterMem(("data_" + std::to_string(c)).c_str(), &mem, &mh), SUCCESS);
  }
  ASSERT_EQ(server.Listen(16), SUCCESS);

  std::vector<std::unique_ptr<HixlCSClient>> clients(kClients);
  std::array<std::array<char, 32>, kClients> local_bufs{};
  const auto local_ep = NativeEndpoint();
  for (size_t c = 0; c < kClients; ++c) {
    clients[c] = std::make_unique<HixlCSClient>();
    HixlClientDesc desc{};
    desc.local_endpoint = &local_ep;
    desc.remote_endpoint = &remote_ep;
    desc.server_ip = "127.0.0.1";
    desc.server_port = kPort;
    desc.tc = 132;
    desc.sl = 4;
    HixlClientConfig cfg{};
    cfg.global_resource_config = kRoceHixlBackendJson;
    ASSERT_EQ(clients[c]->Create(&desc, &cfg), SUCCESS);
    ASSERT_EQ(clients[c]->Connect(3000), SUCCESS);
    CommMem *mems = nullptr;
    char **tags = nullptr;
    uint32_t cnt = 0;
    ASSERT_EQ(clients[c]->GetRemoteMem(&mems, &tags, &cnt, 1000), SUCCESS);
    CommMem lmem{COMM_MEM_TYPE_HOST, local_bufs[c].data(), local_bufs[c].size()};
    MemHandle lmh = nullptr;
    ASSERT_EQ(clients[c]->RegMem("local", &lmem, &lmh), SUCCESS);
  }
  ScopeGuard clients_cleanup([&] {
    for (auto &c : clients) {
      if (c != nullptr) (void)c->Destroy();
    }
  });

  // Drive concurrent async and sync transfers across all four clients.
  std::vector<std::thread> workers;
  for (size_t c = 0; c < kClients; ++c) {
    workers.emplace_back([&, c] {
      const HixlOneSideOpDesc op{remote_bufs[c].data(), local_bufs[c].data(), local_bufs[c].size()};
      for (size_t i = 0; i < kIters; ++i) {
        std::memset(local_bufs[c].data(), static_cast<int>('a' + ((c + i) % 26)), local_bufs[c].size());
        void *h_put = nullptr;
        ASSERT_EQ(clients[c]->BatchPutAsync(&op, 1, &h_put), SUCCESS);
        HixlCompleteStatus st{};
        ASSERT_EQ(WaitAsyncStatus(*clients[c], h_put, st), SUCCESS);
        ASSERT_EQ(st, HIXL_COMPLETE_STATUS_COMPLETED);
        EXPECT_EQ(std::memcmp(local_bufs[c].data(), remote_bufs[c].data(), local_bufs[c].size()), 0);

        std::memset(local_bufs[c].data(), 0, local_bufs[c].size());
        void *h_get = nullptr;
        ASSERT_EQ(clients[c]->BatchGetAsync(&op, 1, &h_get), SUCCESS);
        ASSERT_EQ(WaitAsyncStatus(*clients[c], h_get, st), SUCCESS);
        ASSERT_EQ(st, HIXL_COMPLETE_STATUS_COMPLETED);
        EXPECT_EQ(std::memcmp(local_bufs[c].data(), remote_bufs[c].data(), local_bufs[c].size()), 0);
      }
      ASSERT_EQ(clients[c]->BatchPutSync(&op, 1, 2000), SUCCESS);
    });
  }
  for (auto &t : workers) t.join();

  // Now stall completions on all 4 clients and tear them down concurrently.
  verbs->hold_completions = true;
  for (size_t c = 0; c < kClients; ++c) {
    const HixlOneSideOpDesc op{remote_bufs[c].data(), local_bufs[c].data(), local_bufs[c].size()};
    void *h = nullptr;
    ASSERT_EQ(clients[c]->BatchPutAsync(&op, 1, &h), SUCCESS);
  }
  std::vector<std::thread> destroyers;
  for (size_t c = 0; c < kClients; ++c) {
    destroyers.emplace_back([&, c] { EXPECT_EQ(clients[c]->Destroy(), SUCCESS); });
  }
  for (auto &t : destroyers) t.join();
}

TEST_F(HostRoceUt, ServerPassiveQpUsesMinQueueDepthOnNativePath) {
  constexpr uint16_t kPort = 26661;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, kPort, nullptr));
  ASSERT_EQ(verbs->created_configs.size(), 2U);
  // Client is created first, then server passive QP on Prepare.
  EXPECT_EQ(verbs->created_configs[0].queue_depth,
            CalculateTransportQueueDepth(kDefaultHostRoceMaxTransferCountPerBatch));
  EXPECT_EQ(verbs->created_configs[1].queue_depth, kMinTransportQueueDepth);
}

TEST_F(HostRoceUt, DeviceGetLocalFlushRunsByDefaultOncePerDeviceAfterDataCompletes) {
  verbs->qp_capacity = 128;
  verbs->defer_copies = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[8]{};
  char remote[8] = "device";
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem, COMM_MEM_TYPE_DEVICE);
  const HixlOneSideOpDesc ops[] = {{remote, local, sizeof(local)}, {remote, local, sizeof(local)}};
  verbs->check_flush = [&] { EXPECT_STREQ(local, remote); };
  for (int i = 0; i < 2; ++i) {
    std::memset(local, 0, sizeof(local));
    ASSERT_EQ(channel->BatchGetSync(ops, 2, 2000), SUCCESS);
  }
  EXPECT_EQ(verbs->flush_created.load(), 1U);
  EXPECT_EQ(verbs->flushed_devices, (std::vector<uint32_t>{0U, 0U}));
  EXPECT_EQ(verbs->flush_vendor_modes, (std::vector<bool>{true}));
  ASSERT_EQ(verbs->flush_configs.size(), 1U);
  EXPECT_EQ(verbs->flush_configs[0].tc, 132U);
  EXPECT_EQ(verbs->flush_configs[0].sl, 4U);
  EXPECT_EQ(verbs->created.load(), 1U);  // Loopback flush pairs do not consume data-channel plane rotation.
  EXPECT_EQ(channel->Destroy(), SUCCESS);
  EXPECT_EQ(verbs->flush_destroyed.load(), 1U);
}

TEST_F(HostRoceUt, DisconnectReportsFlushTeardownFailure) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[8]{};
  char remote[8] = "device";
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem, COMM_MEM_TYPE_DEVICE);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(channel->BatchGetSync(&op, 1, 2000), SUCCESS);
  verbs->flush_close_result = FAILED;
  EXPECT_EQ(channel->Disconnect(), FAILED);
  EXPECT_EQ(verbs->flush_destroyed.load(), 1U);
}

TEST_F(HostRoceUt, FailedFlushSetupRetainsTeardownError) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[8]{};
  char remote[8] = "device";
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem, COMM_MEM_TYPE_DEVICE);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  verbs->create_flush_result = FAILED;
  verbs->flush_close_result = UNSUPPORTED;
  EXPECT_EQ(channel->BatchGetSync(&op, 1, 2000), FAILED);
  EXPECT_EQ(channel->Disconnect(), UNSUPPORTED);
  EXPECT_EQ(verbs->flush_destroyed.load(), 1U);
}

TEST_F(HostRoceUt, LocalFlushSkipsHostGetsAndDevicePuts) {
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[8]{};
  char remote[8] = "host";
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(channel->BatchGetSync(&op, 1, 2000), SUCCESS);
  ASSERT_EQ(ep->DeregisterMem(mem), SUCCESS);
  CommMem device{};
  device.type = COMM_MEM_TYPE_DEVICE;
  device.addr = local;
  device.size = sizeof(local);
  ASSERT_EQ(ep->RegisterMem("local", device, mem), SUCCESS);
  ASSERT_EQ(channel->BatchPutSync(&op, 1, 2000), SUCCESS);
  EXPECT_EQ(verbs->flush_created.load(), 0U);
  EXPECT_TRUE(verbs->flushed_devices.empty());
}

TEST_F(HostRoceUt, DeviceGetFlushPrecedesHbmFlagReadAndFailureStopsChannel) {
  constexpr uint16_t kPort = 26662;
  verbs->qp_capacity = 128;
  verbs->defer_copies = true;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, kPort, nullptr));
  char local[8]{};
  char remote[8] = "device";
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local), COMM_MEM_TYPE_DEVICE));
  verbs->check_flush = [&] { EXPECT_STREQ(local, remote); };
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};
  ASSERT_EQ(client.BatchGetSync(&op, 1, 2000), SUCCESS);
  {
    std::lock_guard<std::mutex> lock(verbs->mutex);
    ASSERT_EQ(verbs->flush_post_counts.size(), 1U);
    EXPECT_EQ(verbs->flush_post_counts[0], 2U);  // Flush runs right after 2 data WRs, before HBM flag read.
  }
  EXPECT_EQ(verbs->PostCount(), 4U);  // 2 data WRs + 2 trailing HBM flag WRs.

  // A failed flush must stop the channel and never post the trailing HBM completion-flag read.
  verbs->flush_result = FAILED;
  verbs->flush_close_result = FAILED;
  void *handle = nullptr;
  ASSERT_EQ(client.BatchGetAsync(&op, 1, &handle), SUCCESS);
  ASSERT_NE(handle, nullptr);
  HixlCompleteStatus status{};
  EXPECT_EQ(WaitAsyncStatus(client, handle, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_FAILED);
  EXPECT_EQ(verbs->PostCount(), 6U);  // Only the 2 data WRs were posted; no trailing flag read.
  EXPECT_EQ(client.BatchGetSync(&op, 1, 1000), FAILED);
  EXPECT_EQ(client.Destroy(), FAILED);
}

TEST_F(HostRoceUt, AsyncDeviceGetStalledFlushIsNonBlockingAndAbortsOnDisconnect) {
  constexpr uint16_t kPort = 26663;
  verbs->qp_capacity = 128;
  verbs->hold_flush_completions = true;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, kPort, nullptr));
  char local[8]{};
  char remote[8] = "device";
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local), COMM_MEM_TYPE_DEVICE));
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};

  void *handle = nullptr;
  const auto submit_start = std::chrono::steady_clock::now();
  ASSERT_EQ(client.BatchGetAsync(&op, 1, &handle), SUCCESS);
  ASSERT_NE(handle, nullptr);
  EXPECT_LT(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - submit_start).count(),
      500);

  EXPECT_EQ(verbs->flush_posted.load(), 0U);
  HixlCompleteStatus status{};
  ASSERT_EQ(client.QueryCompleteStatus(handle, &status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_WAITING);
  ASSERT_EQ(verbs->flush_posted.load(), 1U);
  EXPECT_EQ(verbs->PostCount(), 2U);  // The flag read follows the flush completion.

  // Destroy/Disconnect must abort immediately without deadlocking on the stalled flush and destroy flush QPs.
  const auto disconnect_start = std::chrono::steady_clock::now();
  EXPECT_EQ(client.Destroy(), SUCCESS);
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - disconnect_start)
                .count(),
            1000);
  EXPECT_EQ(verbs->flush_destroyed.load(), 1U);
}

TEST_F(HostRoceUt, DeviceGetSyncTimeoutDuringStalledFlushFailsDeterministically) {
  constexpr uint16_t kPort = 26664;
  verbs->qp_capacity = 128;
  verbs->hold_flush_completions = true;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, kPort, nullptr));
  char local[8]{};
  char remote[8] = "device";
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local), COMM_MEM_TYPE_DEVICE));
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};

  EXPECT_EQ(client.BatchGetSync(&op, 1, 30), TIMEOUT);
  EXPECT_EQ(verbs->flush_destroyed.load(), 1U);
  EXPECT_EQ(verbs->PostCount(), 2U);
}

TEST_F(HostRoceUt, BatchedPollAndFlatOutstandingTableHandleLargeOutOfOrderCompletionWaves) {
  constexpr uint16_t kPort = 26665;
  constexpr uint32_t kOps = 400;
  verbs->qp_capacity = 512;
  verbs->model_read_pipeline = true;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, kPort, nullptr));
  std::vector<char> local(kOps * 4U, 0);
  std::vector<char> remote(kOps * 4U, 0);
  for (size_t i = 0; i < remote.size(); ++i) {
    remote[i] = static_cast<char>('A' + (i % 26));
  }
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local.data(), remote.data(), local.size()));
  std::vector<HixlOneSideOpDesc> ops(kOps);
  for (uint32_t i = 0; i < kOps; ++i) {
    ops[i] = {remote.data() + i * 4U, local.data() + i * 4U, 4U};
  }
  void *handle = nullptr;
  ASSERT_EQ(client.BatchGetAsync(ops.data(), kOps, &handle), SUCCESS);
  HixlCompleteStatus status{};
  ASSERT_EQ(WaitAsyncStatus(client, handle, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_EQ(local, remote);
  EXPECT_EQ(verbs->polled_completions.load(), kOps + 2U);
}

TEST_F(HostRoceUt, SplitReadBatchesPipelineAcrossChunksWithSingleFinalFenceAndSingleFlush) {
  constexpr uint16_t kPort = 26666;
  verbs->qp_capacity = 128;
  verbs->defer_copies = true;
  HixlCSServer server("127.0.0.1", kPort, NativeConfig());
  HixlCSClient client;
  ScopeGuard cleanup([&] {
    (void)client.Destroy();
    (void)server.Finalize();
  });
  ASSERT_NO_FATAL_FAILURE(ConnectPair(server, client, kPort, "{\"transfer_config.max_transfer_count_per_batch\":2}"));
  char local[40]{};
  char remote[] = "0123456789abcdefghijklmnopqrstuvwxyz123";
  ASSERT_NO_FATAL_FAILURE(RegisterPairMemory(server, client, local, remote, sizeof(local), COMM_MEM_TYPE_DEVICE));

  std::vector<HixlOneSideOpDesc> ops;
  for (size_t i = 0; i < 5U; ++i) {
    ops.push_back({remote + i * 8U, local + i * 8U, 8U});
  }
  ASSERT_EQ(client.BatchGetSync(ops.data(), ops.size(), 2000), SUCCESS);
  EXPECT_EQ(std::memcmp(local, remote, sizeof(local)), 0);

  const auto posts = verbs->PostsSnapshot();
  ASSERT_EQ(posts.size(), 12U);
  {
    std::lock_guard<std::mutex> lock(verbs->mutex);
    EXPECT_EQ(verbs->batch_sizes, (std::vector<size_t>{4, 4, 2, 2}));
    ASSERT_EQ(verbs->flush_post_counts.size(), 1U);
    EXPECT_EQ(verbs->flush_post_counts[0], 10U);  // Single local flush after all 10 data WRs complete.
  }
  EXPECT_EQ(posts[4].pending_before, 4U);
  EXPECT_EQ(posts[8].pending_before, 8U);
  for (size_t i = 0; i < 9U; ++i) {
    EXPECT_FALSE(posts[i].fence);
    EXPECT_TRUE(posts[i].signaled);
  }
  EXPECT_TRUE(posts[9].fence);  // Final WQE of the Read request sets IBV_SEND_FENCE.
  EXPECT_EQ(posts[10].pending_before, 0U);
  EXPECT_TRUE(posts[10].fence);
  EXPECT_TRUE(posts[11].fence);

  // Overlapping Reads across the max_transfer_count_per_batch boundary still keep dependency fences.
  verbs->model_read_pipeline = true;
  std::memset(local, 0, sizeof(local));
  const HixlOneSideOpDesc overlapping[] = {
      {remote + 0U, local, 8U},
      {remote + 8U, local + 8U, 8U},
      {remote + 16U, local + 16U, 8U},
      {remote + 24U, local, 8U},  // Overlaps op[0] across the batch_size=2 boundary.
  };
  ASSERT_EQ(client.BatchGetSync(overlapping, 4U, 2000), SUCCESS);
  EXPECT_EQ(std::memcmp(local, remote + 24U, 8U), 0);
}

TEST_F(HostRoceUt, DestroyQuarantinesFlagStorageWhenFlagUnregFails) {
  auto retry_channel = NewTransferChannel();
  ASSERT_NE(retry_channel, nullptr);
  std::weak_ptr<uint64_t> weak_retry_flags = retry_channel->flag_storage_;
  ASSERT_FALSE(weak_retry_flags.expired());
  verbs->deregister_result = FAILED;
  ScopeGuard restore([&] { verbs->deregister_result = SUCCESS; });
  EXPECT_NE(retry_channel->Destroy(), SUCCESS);
  EXPECT_NE(retry_channel->flag_mem_handle_, nullptr);
  EXPECT_FALSE(weak_retry_flags.expired());
  verbs->deregister_result = SUCCESS;
  EXPECT_EQ(retry_channel->Destroy(), SUCCESS);
  EXPECT_EQ(retry_channel->flag_mem_handle_, nullptr);
  retry_channel.reset();
  EXPECT_TRUE(weak_retry_flags.expired());

  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  std::weak_ptr<uint64_t> weak_flags = channel->flag_storage_;
  ASSERT_FALSE(weak_flags.expired());
  verbs->deregister_result = FAILED;
  EXPECT_NE(channel->Destroy(), SUCCESS);
  EXPECT_NE(channel->flag_mem_handle_, nullptr);
  channel.reset();
  // Backing buffer must remain quarantined rather than freed while its MR deregistration failed.
  EXPECT_FALSE(weak_flags.expired());
}

class FlushContextAclStub : public llm::AclRuntimeStub {
 public:
  int32_t current_user_dev{1};
  int32_t last_created_user_dev{-1};
  uint32_t create_context_calls{0};

  aclError aclrtGetDevice(int32_t *device_id) override {
    if (device_id == nullptr) {
      return ACL_ERROR_INVALID_PARAM;
    }
    *device_id = current_user_dev;
    return ACL_SUCCESS;
  }

  aclError aclrtCreateContext(aclrtContext *context, int32_t device_id) override {
    ++create_context_calls;
    last_created_user_dev = device_id;
    return llm::AclRuntimeStub::aclrtCreateContext(context, device_id);
  }
};

TEST_F(HostRoceUt, FlushContextUsesAclUserDeviceIdDirectly) {
  FlushContextAclStub acl_stub;
  llm::AclRuntimeStub::Install(&acl_stub);
  ScopeGuard restore([&] { llm::AclRuntimeStub::UnInstall(&acl_stub); });

  void *ctx = nullptr;
  bool owns_context = false;
  EXPECT_EQ(ResolveHostRoceFlushContext(UINT32_MAX, ctx, owns_context), PARAM_INVALID);
  // Current context already belongs to user_device_id=1.
  ASSERT_EQ(ResolveHostRoceFlushContext(1U, ctx, owns_context), SUCCESS);
  EXPECT_NE(ctx, nullptr);
  EXPECT_FALSE(owns_context);
  EXPECT_EQ(acl_stub.create_context_calls, 0U);

  // When the current thread is on user_device_id=0, flush creates a context on user_device_id=1.
  acl_stub.current_user_dev = 0;
  void *created_ctx = nullptr;
  ASSERT_EQ(ResolveHostRoceFlushContext(1U, created_ctx, owns_context), SUCCESS);
  EXPECT_NE(created_ctx, nullptr);
  EXPECT_TRUE(owns_context);
  EXPECT_EQ(acl_stub.create_context_calls, 1U);
  EXPECT_EQ(acl_stub.last_created_user_dev, 1);
  EXPECT_EQ(aclrtDestroyContext(created_ctx), ACL_SUCCESS);
}

TEST_F(HostRoceUt, DeregisteredMrErasesWeakCacheEntry) {
  auto *native = dynamic_cast<HostRoceEndpoint *>(ep.get());
  ASSERT_NE(native, nullptr);
  char buf_a[16]{};
  char buf_b[16]{};
  CommMem mem_a{COMM_MEM_TYPE_HOST, buf_a, sizeof(buf_a)};
  CommMem mem_b{COMM_MEM_TYPE_HOST, buf_b, sizeof(buf_b)};
  MemHandle handle_a = nullptr;
  MemHandle handle_b = nullptr;
  ASSERT_EQ(ep->RegisterMem("a", mem_a, handle_a), SUCCESS);
  ASSERT_EQ(ep->RegisterMem("b", mem_b, handle_b), SUCCESS);
  {
    std::lock_guard<std::mutex> lock(native->Device()->memory_mutex);
    EXPECT_EQ(native->Device()->registrations.size(), 2U);
  }
  ASSERT_EQ(ep->DeregisterMem(handle_a), SUCCESS);
  {
    std::lock_guard<std::mutex> lock(native->Device()->memory_mutex);
    EXPECT_EQ(native->Device()->registrations.size(), 1U);
  }
  ASSERT_EQ(ep->DeregisterMem(handle_b), SUCCESS);
  {
    std::lock_guard<std::mutex> lock(native->Device()->memory_mutex);
    EXPECT_TRUE(native->Device()->registrations.empty());
  }
}

TEST_F(HostRoceUt, SyncDeadlineDrainErrorSetsNotifyWaitersAndWakesConcurrentSync) {
  verbs->qp_capacity = 128;
  verbs->hold_completions = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  char local[8] = "wake";
  char remote[8]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};

  Status ret_short = SUCCESS;
  Status ret_long = SUCCESS;
  const auto start = std::chrono::steady_clock::now();
  const auto wait_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  // Launch t_short first and wait until it is queued at async_queue_.front(), so it deterministically enters
  // kRunningFlag first and triggers DrainReady() error on its deadline.
  std::thread t_short([&] { ret_short = channel->DoBatchTransferSync(false, &op, 1, 60); });
  while (true) {
    {
      std::lock_guard<std::mutex> lock(channel->mutex_);
      if (channel->async_handles_.size() == 1U) {
        break;
      }
    }
    ASSERT_LT(std::chrono::steady_clock::now(), wait_deadline);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  std::thread t_long([&] { ret_long = channel->DoBatchTransferSync(false, &op, 1, 5000); });
  while (true) {
    {
      std::lock_guard<std::mutex> lock(channel->mutex_);
      if (channel->async_handles_.size() == 2U) {
        break;
      }
    }
    ASSERT_LT(std::chrono::steady_clock::now(), wait_deadline);
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  verbs->read_post_delay_ms = 80;
  verbs->read_completion_error = 10;
  verbs->hold_completions = false;
  t_short.join();
  t_long.join();
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_EQ(ret_short, FAILED);
  EXPECT_EQ(ret_long, FAILED);
  EXPECT_LT(elapsed_ms, 1000);
}

TEST_F(HostRoceUt, QueryCanRetireAnotherChannelsSharedFlush) {
  auto first = NewTransferChannel();
  auto second = NewTransferChannel();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  char local[2][8]{};
  char remote[2][8] = {"first", "second"};
  MemHandle mem = nullptr;
  Memory(local[0], remote[0], sizeof(local[0]), mem, COMM_MEM_TYPE_DEVICE);
  Memory(local[1], remote[1], sizeof(local[1]), mem, COMM_MEM_TYPE_DEVICE);
  const HixlOneSideOpDesc op1{remote[0], local[0], sizeof(local[0])};
  const HixlOneSideOpDesc op2{remote[1], local[1], sizeof(local[1])};

  verbs->hold_flush_completions = true;
  void *first_handle = nullptr;
  ASSERT_EQ(first->BatchGetAsync(&op1, 1, &first_handle), SUCCESS);
  HixlCompleteStatus status{};
  ASSERT_EQ(first->QueryCompleteStatus(first_handle, &status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_WAITING);
  ASSERT_EQ(verbs->flush_posted.load(), 1U);

  void *second_handle = nullptr;
  ASSERT_EQ(second->BatchGetAsync(&op2, 1, &second_handle), SUCCESS);
  verbs->hold_flush_completions = false;
  ASSERT_EQ(WaitChannelAsync(second, second_handle, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  ASSERT_EQ(WaitChannelAsync(first, first_handle, status), SUCCESS);
  EXPECT_EQ(status, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_EQ(verbs->flush_created.load(), 1U);
  EXPECT_EQ(verbs->flush_posted.load(), 2U);
}

TEST_F(HostRoceUt, DeviceReusesLocalFlushAcrossChannelsAndIsolatesStalledDisconnect) {
  verbs->qp_capacity = 128;
  verbs->defer_copies = true;
  std::vector<std::shared_ptr<HostRoceChannel>> channels(4);
  for (auto &ch : channels) {
    ch = NewTransferChannel();
    ASSERT_NE(ch, nullptr);
  }
  char local[4][8]{};
  char remote[4][8] = {"dev-ch0", "dev-ch1", "dev-ch2", "dev-ch3"};
  for (size_t i = 0; i < 4U; ++i) {
    MemHandle mh = nullptr;
    Memory(local[i], remote[i], sizeof(local[i]), mh, COMM_MEM_TYPE_DEVICE);
  }

  for (size_t i = 0; i < 4U; ++i) {
    const HixlOneSideOpDesc op{remote[i], local[i], sizeof(local[i])};
    ASSERT_EQ(channels[i]->BatchGetSync(&op, 1, 2000), SUCCESS);
    EXPECT_STREQ(local[i], remote[i]);
  }
  // Strictly 1 loopback flush QP pair per (NIC, memory_device), reused across all 4 channels.
  EXPECT_EQ(verbs->flush_created.load(), 1U);
  EXPECT_EQ(verbs->flushed_devices.size(), 4U);
  EXPECT_EQ(verbs->flush_destroyed.load(), 0U);

  // Disconnecting one idle channel keeps the device-level warm flush QP alive.
  ASSERT_EQ(channels[0]->Destroy(), SUCCESS);
  EXPECT_EQ(verbs->flush_destroyed.load(), 0U);
  std::memset(local[2], 0, sizeof(local[2]));
  const HixlOneSideOpDesc op2{remote[2], local[2], sizeof(local[2])};
  ASSERT_EQ(channels[2]->BatchGetSync(&op2, 1, 2000), SUCCESS);
  EXPECT_STREQ(local[2], remote[2]);
  EXPECT_EQ(verbs->flush_created.load(), 1U);

  // Leave the shared flush pending, then disconnect its owner while another channel needs it.
  verbs->hold_flush_completions = true;
  const uint32_t posted_before = verbs->flush_posted.load();
  const HixlOneSideOpDesc op1{remote[1], local[1], sizeof(local[1])};
  void *h_stalled = nullptr;
  ASSERT_EQ(channels[1]->BatchGetAsync(&op1, 1, &h_stalled), SUCCESS);
  HixlCompleteStatus st{};
  ASSERT_EQ(channels[1]->QueryCompleteStatus(h_stalled, &st), SUCCESS);
  EXPECT_EQ(st, HIXL_COMPLETE_STATUS_WAITING);
  ASSERT_EQ(verbs->flush_posted.load(), posted_before + 1U);

  std::memset(local[2], 0, sizeof(local[2]));
  void *h_peer = nullptr;
  ASSERT_EQ(channels[2]->BatchGetAsync(&op2, 1, &h_peer), SUCCESS);
  ASSERT_EQ(channels[2]->QueryCompleteStatus(h_peer, &st), SUCCESS);
  EXPECT_EQ(st, HIXL_COMPLETE_STATUS_WAITING);
  EXPECT_EQ(verbs->flush_posted.load(), posted_before + 1U);
  ASSERT_EQ(channels[1]->Destroy(), SUCCESS);
  EXPECT_EQ(verbs->flush_destroyed.load(), 1U);

  verbs->hold_flush_completions = false;
  ASSERT_EQ(WaitChannelAsync(channels[2], h_peer, st), SUCCESS);
  EXPECT_EQ(st, HIXL_COMPLETE_STATUS_COMPLETED);
  EXPECT_STREQ(local[2], remote[2]);
  EXPECT_EQ(verbs->flush_created.load(), 2U);

  // Destroying all remaining channels releases the device-level cached flush QP (zero device reference cycle).
  for (size_t i = 2; i < 4U; ++i) {
    EXPECT_EQ(channels[i]->Destroy(), SUCCESS);
  }
  EXPECT_EQ(verbs->flush_destroyed.load(), verbs->flush_created.load());
}

TEST_F(HostRoceUt, RoceRetryCntAndTimeoutConfigOverrideEnvAndValidateBounds) {
  setenv("HCCL_RDMA_RETRY_CNT", "2", 1);
  setenv("HCCL_RDMA_TIMEOUT", "10", 1);
  ScopeGuard env_cleanup([] {
    unsetenv("HCCL_RDMA_RETRY_CNT");
    unsetenv("HCCL_RDMA_TIMEOUT");
  });

  EndpointDesc local_ep = NativeEndpoint("127.0.0.1");
  EndpointDesc remote_ep = NativeEndpoint("127.0.0.2");
  HixlClientDesc client_desc{};
  client_desc.server_ip = "127.0.0.1";
  client_desc.server_port = 18080;
  client_desc.local_endpoint = &local_ep;
  client_desc.remote_endpoint = &remote_ep;

  HixlClientConfig cfg{};
  cfg.global_resource_config =
      R"({"roce_config.hixl_backend":true,"roce_config.retry_cnt":5,"roce_config.timeout":"16"})";
  HixlCSClient client;
  ASSERT_EQ(client.Create(&client_desc, &cfg), SUCCESS);
  EXPECT_EQ(client.retry_cnt_, 5U);
  EXPECT_EQ(client.retry_interval_, 16U);
  EXPECT_EQ(client.Destroy(), SUCCESS);

  // With the RoCE fields omitted, the environment variables take effect.
  HixlClientConfig env_cfg{};
  env_cfg.global_resource_config = R"({"roce_config.hixl_backend":true})";
  HixlCSClient env_client;
  ASSERT_EQ(env_client.Create(&client_desc, &env_cfg), SUCCESS);
  EXPECT_EQ(env_client.retry_cnt_, 2U);
  EXPECT_EQ(env_client.retry_interval_, 10U);
  EXPECT_EQ(env_client.Destroy(), SUCCESS);

  // Out-of-range values in roce_config.* are rejected.
  for (const char *bad_json : {
           R"({"roce_config.retry_cnt":0})",
           R"({"roce_config.retry_cnt":8})",
           R"({"roce_config.timeout":4})",
           R"({"roce_config.timeout":21})",
       }) {
    HixlClientConfig bad_cfg{};
    bad_cfg.global_resource_config = bad_json;
    HixlCSClient bad_client;
    EXPECT_EQ(bad_client.Create(&client_desc, &bad_cfg), PARAM_INVALID) << bad_json;
  }
}

TEST_F(HostRoceUt, AsyncSubmitTimeoutCapsStalledQueueWait) {
  verbs->qp_capacity = 2;
  verbs->hold_completions = true;
  auto channel = NewTransferChannel();
  ASSERT_NE(channel, nullptr);
  EXPECT_EQ(channel->async_submit_timeout_ms_, 20ULL * 60ULL * 1000ULL);
  channel->async_submit_timeout_ms_ = 30U;

  char local[] = "abcdefghijklmno";
  char remote[sizeof(local)]{};
  MemHandle mem = nullptr;
  Memory(local, remote, sizeof(local), mem);
  const HixlOneSideOpDesc op{remote, local, sizeof(local)};

  void *handle = nullptr;
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(channel->BatchPutAsync(&op, 1, &handle), TIMEOUT);
  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
  EXPECT_LT(elapsed_ms, 1000);
  EXPECT_EQ(handle, nullptr);
  EXPECT_TRUE(channel->async_handles_.empty());
  EXPECT_TRUE(channel->transfer_failure_latched_);
  EXPECT_EQ(channel->transfer_failure_status_, TIMEOUT);
  EXPECT_EQ(channel->BatchPutAsync(&op, 1, &handle), FAILED);
}
}  // namespace
}  // namespace hixl
