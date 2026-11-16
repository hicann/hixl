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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

#include "ascendcl_stub.h"
#include "channel.h"
// The export-descriptor codec and the remote-address rewrite are private members of UbMemEndpoint; the
// tests below exercise them directly, so the header is included with the test-only visibility switch.
#define private public
#define protected public
#include "cs/ubmem/ubmem_endpoint.h"
#undef protected
#undef private
#include "cs/ubmem/ubmem_channel.h"
#include "cs/ubmem/ubmem_types.h"
#include "cs/ubmem/ubmem_virtual_memory_manager.h"
#include "cs/transfer_pool.h"
#include "depends/hccl/src/hccl_stub.h"
#include "endpoint.h"
#include "endpoint_store.h"
#include "engine/endpoint_test_utils.h"
#include "engine/test_mmpa_utils.h"
#include "cs/ubmem/ubmem_allocator.h"
#include "hccl/hccl_types.h"
#include "hcomm/hcomm_res_defs.h"
#include "hixl/hixl_types.h"

namespace hixl {
namespace {
constexpr int32_t kUbMemEndpointPoolDevId = 910263;

EndpointDesc MakeUbMemEndpoint(EndpointLocType loc_type) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_UB_MEM;
  ep.loc.locType = loc_type;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = 1U;
  return ep;
}
// ShareHandleInfo with deterministic bytes, so the codec and the ownership check can compare them.
ShareHandleInfo MakeShareHandle(uintptr_t va_addr, size_t len, MemType mem_type) {
  ShareHandleInfo info{};
  info.va_addr = va_addr;
  info.len = len;
  info.mem_type = mem_type;
  for (size_t i = 0; i < sizeof(info.share_handle.data); ++i) {
    info.share_handle.data[i] = static_cast<uint8_t>(i + 1U);
  }
  return info;
}
}  // namespace

class UbMemEndpointUt : public ::testing::Test {
 protected:
  void SetUp() override {
    acl_stub_ = endpoint_test::CreateAclRuntimeStub("Ascend910_9391", 0, 0, 9, 8);
    llm::AclRuntimeStub::SetInstance(acl_stub_);
    hixl_test::InstallSysApiHooks(std::make_shared<test::KernelJsonMmpaStub>());
    VirtualMemoryManager::GetInstance().Finalize();
    ResetMemRegRecord();
    ResetTransferCounter();
  }

  void TearDown() override {
    auto *pool = TransferPool::GetInstance(kUbMemEndpointPoolDevId);
    if (pool != nullptr) {
      pool->Finalize();
    }
    VirtualMemoryManager::GetInstance().Finalize();
    hixl_test::ResetSysApiHooks();
    llm::AclRuntimeStub::Reset();
  }

  std::shared_ptr<endpoint_test::MockAclRuntimeStub> acl_stub_;
};

TEST_F(UbMemEndpointUt, InitializeUsesEndpointItselfAsHandleWithoutHcomm) {
  UbMemEndpoint endpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(endpoint.Initialize(), SUCCESS);
  // UB_MEM endpoint state is the Endpoint object itself, so the opaque handle names it directly.
  EXPECT_NE(endpoint.GetHandle(), nullptr);
  EXPECT_EQ(endpoint.GetHandle(), static_cast<EndpointHandle>(&endpoint));
  EXPECT_EQ(endpoint.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, RegisterExportThenForeignImportMapsAndRewritesRemoteVa) {
  constexpr size_t kLen = 32U;
  UbMemEndpoint local(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(local.Initialize(), SUCCESS);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  ASSERT_NE(ptr, nullptr);

  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = ptr;
  mem.size = kLen;
  MemHandle handle = nullptr;
  ASSERT_EQ(local.RegisterMem("ubmem_buf", mem, handle), SUCCESS);
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(GetMemRegRecordCount(), 0U);

  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(local.ExportMem(exported), SUCCESS);
  ASSERT_EQ(exported.size(), 1U);
  ASSERT_NE(exported[0].export_desc, nullptr);
  EXPECT_GT(exported[0].export_len, 0U);

  // What a peer in another process sends: the same VA and length, but a handle this process never made.
  const std::string payload(static_cast<const char *>(exported[0].export_desc), exported[0].export_len);
  nlohmann::json items = nlohmann::json::parse(payload);
  ASSERT_TRUE(items.is_array());
  ASSERT_EQ(items.size(), 1U);
  nlohmann::json &share_handle = items[0]["share_handle"];
  ASSERT_TRUE(share_handle.is_array());
  ASSERT_FALSE(share_handle.empty());
  share_handle[0] = static_cast<uint8_t>(share_handle[0].get<uint8_t>() ^ 0xFFU);
  const std::string foreign_desc = items.dump();
  const auto *desc_ptr = foreign_desc.data();
  const auto desc_len = static_cast<uint32_t>(foreign_desc.size());

  // A handle this process never exported is mapped into the importer's own VA space.
  UbMemEndpoint peer(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(peer.Initialize(), SUCCESS);
  CommMem imported{};
  ASSERT_EQ(peer.ImportMem(desc_ptr, desc_len, imported), SUCCESS);
  EXPECT_EQ(imported.addr, ptr);
  EXPECT_EQ(imported.size, kLen);

  HixlOneSideOpDesc src{};
  src.local_buf = ptr;
  src.remote_buf = imported.addr;
  src.len = kLen;
  std::vector<HixlOneSideOpDesc> translated;
  ASSERT_EQ(peer.TranslateRemoteDescs(1U, &src, translated), SUCCESS);
  ASSERT_EQ(translated.size(), 1U);
  // The peer mapped the buffer, so the transfer targets the mapped VA, not the exporter's original one.
  EXPECT_NE(translated[0].remote_buf, ptr);
  EXPECT_NE(translated[0].remote_buf, nullptr);

  // Releasing the import drops the mapping, so the exporter's own memory is free to go.
  EXPECT_EQ(peer.UnimportMem(desc_ptr, desc_len), SUCCESS);
  EXPECT_EQ(peer.Finalize(), SUCCESS);
  EXPECT_EQ(local.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  EXPECT_EQ(local.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, PeerImportOfOwnProcessExportBindsLocalVaWithoutMapping) {
  constexpr size_t kLen = 32U;
  UbMemEndpoint local(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(local.Initialize(), SUCCESS);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  ASSERT_NE(ptr, nullptr);

  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = ptr;
  mem.size = kLen;
  MemHandle handle = nullptr;
  ASSERT_EQ(local.RegisterMem("ubmem_buf", mem, handle), SUCCESS);

  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(local.ExportMem(exported), SUCCESS);
  ASSERT_EQ(exported.size(), 1U);
  ASSERT_NE(exported[0].export_desc, nullptr);
  // Deregistering frees the descriptor buffer, so keep a copy for the reimport at the end of the test.
  const std::string desc_copy(static_cast<const char *>(exported[0].export_desc), exported[0].export_len);
  const auto desc_len = static_cast<uint32_t>(desc_copy.size());

  // ACL cannot import a handle of this process, so another endpoint here binds the range to its original
  // VA instead of mapping it, even though it never registered the range itself.
  UbMemEndpoint peer(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(peer.Initialize(), SUCCESS);
  CommMem bound{};
  ASSERT_EQ(peer.ImportMem(desc_copy.data(), desc_len, bound), SUCCESS);
  EXPECT_EQ(bound.addr, ptr);
  EXPECT_EQ(bound.size, kLen);

  HixlOneSideOpDesc src{};
  src.local_buf = ptr;
  src.remote_buf = bound.addr;
  src.len = kLen;
  std::vector<HixlOneSideOpDesc> translated;
  ASSERT_EQ(peer.TranslateRemoteDescs(1U, &src, translated), SUCCESS);
  ASSERT_EQ(translated.size(), 1U);
  // The binding owns nothing and rewrites nothing: the peer addresses the exporter's own VA.
  EXPECT_EQ(translated[0].remote_buf, ptr);

  EXPECT_EQ(peer.UnimportMem(desc_copy.data(), desc_len), SUCCESS);
  EXPECT_EQ(peer.Finalize(), SUCCESS);

  // The export ends with its registration, so a later import of the same handle is a real peer import.
  EXPECT_EQ(local.DeregisterMem(handle), SUCCESS);
  UbMemEndpoint late_peer(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(late_peer.Initialize(), SUCCESS);
  CommMem remapped{};
  ASSERT_EQ(late_peer.ImportMem(desc_copy.data(), desc_len, remapped), SUCCESS);
  ASSERT_EQ(late_peer.TranslateRemoteDescs(1U, &src, translated), SUCCESS);
  ASSERT_EQ(translated.size(), 1U);
  EXPECT_NE(translated[0].remote_buf, ptr);

  EXPECT_EQ(late_peer.UnimportMem(desc_copy.data(), desc_len), SUCCESS);
  EXPECT_EQ(late_peer.Finalize(), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  EXPECT_EQ(local.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, ImportingOwnHandlesBindsLocalVaWithoutMapping) {
  constexpr size_t kLen = 32U;
  UbMemEndpoint endpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(endpoint.Initialize(), SUCCESS);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  ASSERT_NE(ptr, nullptr);

  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = ptr;
  mem.size = kLen;
  MemHandle handle = nullptr;
  ASSERT_EQ(endpoint.RegisterMem("ubmem_buf", mem, handle), SUCCESS);

  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(endpoint.ExportMem(exported), SUCCESS);
  ASSERT_EQ(exported.size(), 1U);
  ASSERT_NE(exported[0].export_desc, nullptr);

  // Handles this endpoint exported are already addressable here, so they are bound to their original VA
  // instead of being imported and mapped: no ACL import, no VMM reservation.
  CommMem bound{};
  ASSERT_EQ(endpoint.ImportMem(exported[0].export_desc, exported[0].export_len, bound), SUCCESS);
  EXPECT_EQ(bound.addr, ptr);

  HixlOneSideOpDesc src{};
  src.local_buf = ptr;
  src.remote_buf = bound.addr;
  src.len = kLen;
  std::vector<HixlOneSideOpDesc> translated;
  ASSERT_EQ(endpoint.TranslateRemoteDescs(1U, &src, translated), SUCCESS);
  ASSERT_EQ(translated.size(), 1U);
  EXPECT_EQ(translated[0].remote_buf, ptr);

  // The binding owns nothing, so teardown must not touch ACL or the VMM.
  EXPECT_EQ(endpoint.UnimportMem(exported[0].export_desc, exported[0].export_len), SUCCESS);
  EXPECT_EQ(endpoint.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  EXPECT_EQ(endpoint.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, ImportMapsForeignHandleEvenWhenVaCoincides) {
  constexpr size_t kLen = 32U;
  UbMemEndpoint local(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(local.Initialize(), SUCCESS);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  ASSERT_NE(ptr, nullptr);

  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = ptr;
  mem.size = kLen;
  MemHandle handle = nullptr;
  ASSERT_EQ(local.RegisterMem("ubmem_buf", mem, handle), SUCCESS);

  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(local.ExportMem(exported), SUCCESS);
  ASSERT_EQ(exported.size(), 1U);
  ASSERT_NE(exported[0].export_desc, nullptr);

  // What a real peer sends: the same VA and length, but a handle this process never exported.
  const std::string payload(static_cast<const char *>(exported[0].export_desc), exported[0].export_len);
  nlohmann::json items = nlohmann::json::parse(payload);
  ASSERT_TRUE(items.is_array());
  ASSERT_EQ(items.size(), 1U);
  nlohmann::json &share_handle = items[0]["share_handle"];
  ASSERT_TRUE(share_handle.is_array());
  ASSERT_FALSE(share_handle.empty());
  share_handle[0] = static_cast<uint8_t>(share_handle[0].get<uint8_t>() ^ 0xFFU);
  const std::string foreign_desc = items.dump();

  UbMemEndpoint peer(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(peer.Initialize(), SUCCESS);
  CommMem imported{};
  ASSERT_EQ(peer.ImportMem(foreign_desc.data(), static_cast<uint32_t>(foreign_desc.size()), imported), SUCCESS);
  EXPECT_EQ(imported.addr, ptr);
  EXPECT_EQ(imported.size, kLen);

  HixlOneSideOpDesc src{};
  src.local_buf = ptr;
  src.remote_buf = imported.addr;
  src.len = kLen;
  std::vector<HixlOneSideOpDesc> translated;
  ASSERT_EQ(peer.TranslateRemoteDescs(1U, &src, translated), SUCCESS);
  ASSERT_EQ(translated.size(), 1U);
  // The matching VA must not fool the ownership check: a foreign handle is imported and remapped.
  EXPECT_NE(translated[0].remote_buf, ptr);
  EXPECT_NE(translated[0].remote_buf, nullptr);

  // Teardown releases the mapped import; the peer's own registration is untouched.
  EXPECT_EQ(peer.Finalize(), SUCCESS);
  EXPECT_EQ(local.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  EXPECT_EQ(local.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, MemUnimportReleasesImportedMappingAndAllowsReimport) {
  constexpr size_t kLen = 32U;
  UbMemEndpoint local(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(local.Initialize(), SUCCESS);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  ASSERT_NE(ptr, nullptr);

  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = ptr;
  mem.size = kLen;
  MemHandle handle = nullptr;
  ASSERT_EQ(local.RegisterMem("ubmem_buf", mem, handle), SUCCESS);

  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(local.ExportMem(exported), SUCCESS);
  ASSERT_EQ(exported.size(), 1U);
  ASSERT_NE(exported[0].export_desc, nullptr);

  // What a real peer sends: the same VA and length, but a handle this process never exported.
  const std::string payload(static_cast<const char *>(exported[0].export_desc), exported[0].export_len);
  nlohmann::json items = nlohmann::json::parse(payload);
  ASSERT_TRUE(items.is_array());
  ASSERT_EQ(items.size(), 1U);
  nlohmann::json &share_handle = items[0]["share_handle"];
  ASSERT_TRUE(share_handle.is_array());
  ASSERT_FALSE(share_handle.empty());
  share_handle[0] = static_cast<uint8_t>(share_handle[0].get<uint8_t>() ^ 0xFFU);
  const std::string foreign_desc = items.dump();
  const auto *desc_ptr = foreign_desc.data();
  const auto desc_len = static_cast<uint32_t>(foreign_desc.size());

  UbMemEndpoint peer(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(peer.Initialize(), SUCCESS);
  CommMem imported{};
  ASSERT_EQ(peer.ImportMem(desc_ptr, desc_len, imported), SUCCESS);

  HixlOneSideOpDesc src{};
  src.local_buf = ptr;
  src.remote_buf = imported.addr;
  src.len = kLen;
  std::vector<HixlOneSideOpDesc> translated;
  ASSERT_EQ(peer.TranslateRemoteDescs(1U, &src, translated), SUCCESS);
  ASSERT_EQ(translated.size(), 1U);
  void *mapped = translated[0].remote_buf;
  EXPECT_NE(mapped, ptr);

  // Matching the descriptor releases the mapped range, so the same op no longer translates.
  ASSERT_EQ(peer.UnimportMem(desc_ptr, desc_len), SUCCESS);
  EXPECT_NE(peer.TranslateRemoteDescs(1U, &src, translated), SUCCESS);

  // Importing the same descriptor again re-establishes the mapping on the released VMM block.
  CommMem reimported{};
  ASSERT_EQ(peer.ImportMem(desc_ptr, desc_len, reimported), SUCCESS);
  ASSERT_EQ(peer.TranslateRemoteDescs(1U, &src, translated), SUCCESS);
  ASSERT_EQ(translated.size(), 1U);
  EXPECT_EQ(translated[0].remote_buf, mapped);

  // Releasing twice is a no-op, not a failure: teardown paths must not fail on an already-freed range.
  EXPECT_EQ(peer.UnimportMem(desc_ptr, desc_len), SUCCESS);
  EXPECT_EQ(peer.UnimportMem(desc_ptr, desc_len), SUCCESS);

  // The peer's own registration is untouched by either side.
  EXPECT_EQ(peer.Finalize(), SUCCESS);
  EXPECT_EQ(local.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  EXPECT_EQ(local.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, MemUnimportRejectsInvalidArguments) {
  UbMemEndpoint peer(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(peer.Initialize(), SUCCESS);

  // The argument guard fires before anything is decoded, so the buffer content is irrelevant here.
  const char probe[] = "{}";
  const auto probe_len = static_cast<uint32_t>(sizeof(probe) - 1U);
  EXPECT_EQ(peer.UnimportMem(nullptr, probe_len), PARAM_INVALID);
  EXPECT_EQ(peer.UnimportMem(probe, 0U), PARAM_INVALID);

  // A descriptor that does not decode reports PARAM_INVALID, so nothing is released for it.
  const char not_json[] = "not-json";
  EXPECT_EQ(peer.UnimportMem(not_json, static_cast<uint32_t>(sizeof(not_json) - 1U)), PARAM_INVALID);
  const char wrong_type[] = "{}";
  EXPECT_EQ(peer.UnimportMem(wrong_type, static_cast<uint32_t>(sizeof(wrong_type) - 1U)), PARAM_INVALID);
  // An empty handle list decodes to PARAM_INVALID rather than releasing everything.
  const char empty[] = "[]";
  EXPECT_EQ(peer.UnimportMem(empty, static_cast<uint32_t>(sizeof(empty) - 1U)), PARAM_INVALID);

  EXPECT_EQ(peer.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, RegisterMemRejectsNullAddress) {
  UbMemEndpoint endpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(endpoint.Initialize(), SUCCESS);
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = nullptr;
  mem.size = 32U;
  MemHandle handle = nullptr;
  EXPECT_EQ(endpoint.RegisterMem("invalid", mem, handle), PARAM_INVALID);
  EXPECT_EQ(handle, nullptr);
  EXPECT_EQ(GetMemRegRecordCount(), 0U);
  EXPECT_EQ(endpoint.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, DuplicateRegistrationReusesExportDescriptor) {
  constexpr size_t kLen = 32U;
  UbMemEndpoint endpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(endpoint.Initialize(), SUCCESS);
  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = ptr;
  mem.size = kLen;

  MemHandle first_handle = nullptr;
  MemHandle duplicate_handle = nullptr;
  ASSERT_EQ(endpoint.RegisterMem("ubmem_buf", mem, first_handle), SUCCESS);
  ASSERT_EQ(endpoint.RegisterMem("ubmem_buf", mem, duplicate_handle), SUCCESS);
  EXPECT_EQ(duplicate_handle, first_handle);
  EXPECT_EQ(endpoint.exports_.size(), 1U);

  std::vector<HixlMemDesc> exported;
  ASSERT_EQ(endpoint.ExportMem(exported), SUCCESS);
  ASSERT_EQ(exported.size(), 1U);
  EXPECT_NE(exported[0].export_desc, nullptr);
  EXPECT_GT(exported[0].export_len, 0U);

  EXPECT_EQ(endpoint.DeregisterMem(first_handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  EXPECT_EQ(endpoint.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, ChannelCreateSkipsHcomm) {
  UbMemEndpoint endpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(endpoint.Initialize(), SUCCESS);
  ChannelHandle channel_handle = 0UL;
  ChannelDesc channel_desc{};
  ResetChannelCreateRecord();
  ASSERT_EQ(endpoint.CreateChannel(channel_desc, channel_handle, 0U), SUCCESS);
  EXPECT_NE(channel_handle, 0UL);
  HcommChannelDesc created_desc{};
  EXPECT_FALSE(GetLastChannelCreateDesc(&created_desc));
  EXPECT_EQ(endpoint.DestroyChannel(channel_handle), SUCCESS);
  EXPECT_EQ(endpoint.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, StoreMatchesUbMemByLocationAndDeviceId) {
  EndpointStore store;
  EndpointHandle created = nullptr;
  ASSERT_EQ(store.CreateEndpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_DEVICE), created), SUCCESS);
  ASSERT_NE(created, nullptr);

  EndpointDesc query = MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_DEVICE);
  query.commAddr.id = 99U;
  EndpointHandle matched = nullptr;
  EXPECT_EQ(store.MatchEndpoint(query, matched), nullptr);

  query.commAddr.id = 1U;
  ASSERT_NE(store.MatchEndpoint(query, matched), nullptr);
  EXPECT_EQ(matched, created);

  EndpointHandle host_matched = nullptr;
  EXPECT_EQ(store.MatchEndpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST), host_matched), nullptr);
  EXPECT_EQ(store.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, ExportMemEmptyWhenNothingRegistered) {
  UbMemEndpoint endpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(endpoint.Initialize(), SUCCESS);
  std::vector<HixlMemDesc> mem_descs;
  ASSERT_EQ(endpoint.ExportMem(mem_descs), SUCCESS);
  EXPECT_TRUE(mem_descs.empty());
  EXPECT_EQ(endpoint.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, CreateLogicalChannelOnUbMemEndpoint) {
  UbMemEndpoint endpoint(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(endpoint.Initialize(), SUCCESS);
  ASSERT_NE(endpoint.GetHandle(), nullptr);
  ChannelHandle channel_handle = 0UL;
  ChannelDesc channel_desc{};
  ASSERT_EQ(endpoint.CreateChannel(channel_desc, channel_handle, 0U), SUCCESS);
  EXPECT_NE(channel_handle, 0UL);
  EXPECT_EQ(endpoint.DestroyChannel(channel_handle), SUCCESS);
  EXPECT_EQ(endpoint.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, InitializeRejectsCapacityMismatch) {
  GlobalConfig first_config;
  ASSERT_EQ(GlobalConfig::Parse(R"({"fabric_memory.max_capacity":"10"})", first_config), SUCCESS);
  UbMemEndpoint first(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_DEVICE), first_config);
  ASSERT_EQ(first.Initialize(), SUCCESS);

  GlobalConfig conflicting_config;
  ASSERT_EQ(GlobalConfig::Parse(R"({"fabric_memory.max_capacity":"11"})", conflicting_config), SUCCESS);
  UbMemEndpoint conflicting(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_DEVICE), conflicting_config);
  EXPECT_EQ(conflicting.Initialize(), PARAM_INVALID);

  EXPECT_EQ(first.Finalize(), SUCCESS);
}

TEST_F(UbMemEndpointUt, EndpointFinalizeDoesNotTearDownProcessWideVmm) {
  constexpr size_t kOneGB = 1024UL * 1024UL * 1024UL;
  auto &vmm = VirtualMemoryManager::GetInstance();
  UbMemEndpoint first(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  UbMemEndpoint peer(MakeUbMemEndpoint(ENDPOINT_LOC_TYPE_HOST));
  ASSERT_EQ(first.Initialize(), SUCCESS);
  ASSERT_EQ(peer.Initialize(), SUCCESS);

  uintptr_t addr = 0;
  ASSERT_EQ(vmm.ReserveMemory(kOneGB, addr), SUCCESS);

  // The endpoint does not own the process-wide VA pool, so its teardown must not release the pool.
  ASSERT_EQ(first.Finalize(), SUCCESS);
  EXPECT_EQ(vmm.ReleaseMemory(addr), SUCCESS);

  // The pool remains available after all endpoints are gone; only the singleton destructor releases it.
  ASSERT_EQ(peer.Finalize(), SUCCESS);
  uintptr_t another_addr = 0;
  EXPECT_EQ(vmm.ReserveMemory(kOneGB, another_addr), SUCCESS);
}

TEST_F(UbMemEndpointUt, ShareHandleEncodeDecodeRoundTrip) {
  const std::vector<ShareHandleInfo> src = {MakeShareHandle(0x1000U, 64U, MEM_HOST)};
  void *export_desc = nullptr;
  uint32_t export_len = 0U;
  ASSERT_EQ(UbMemEndpoint::EncodeShareHandles(src, export_desc, export_len), SUCCESS);
  ASSERT_NE(export_desc, nullptr);
  EXPECT_GT(export_len, 0U);

  std::vector<ShareHandleInfo> decoded;
  ASSERT_EQ(UbMemEndpoint::DecodeShareHandles(export_desc, export_len, decoded), SUCCESS);
  ASSERT_EQ(decoded.size(), 1U);
  EXPECT_EQ(decoded[0].va_addr, 0x1000U);
  EXPECT_EQ(decoded[0].len, 64U);
  EXPECT_EQ(decoded[0].mem_type, MEM_HOST);
  EXPECT_EQ(decoded[0].share_handle.data[0], 1U);
  UbMemEndpoint::FreeExportDesc(export_desc);
  EXPECT_EQ(export_desc, nullptr);
}

TEST_F(UbMemEndpointUt, ShareHandleDecodeRejectsEmptyAndInvalidPayload) {
  std::vector<ShareHandleInfo> decoded;
  EXPECT_EQ(UbMemEndpoint::DecodeShareHandles(nullptr, 4U, decoded), PARAM_INVALID);
  const char empty[] = "[]";
  EXPECT_EQ(UbMemEndpoint::DecodeShareHandles(empty, 0U, decoded), PARAM_INVALID);
  EXPECT_EQ(UbMemEndpoint::DecodeShareHandles(empty, 2U, decoded), PARAM_INVALID);
  const char not_array[] = "{}";
  EXPECT_EQ(UbMemEndpoint::DecodeShareHandles(not_array, 2U, decoded), PARAM_INVALID);
}

TEST_F(UbMemEndpointUt, ShareHandleTranslateRemoteOpDescsMapsAndSplits) {
  UbMemRemoteIndex index;
  index.emplace(0x1000U, VaInfo{0x2000U, 256U, /*imported=*/true});

  uint8_t local_buf[256] = {};
  HixlOneSideOpDesc src{};
  src.local_buf = local_buf;
  src.remote_buf = reinterpret_cast<void *>(0x1000U);
  src.len = 256U;
  std::vector<HixlOneSideOpDesc> dst;
  ASSERT_EQ(UbMemEndpoint::TranslateRemoteOpDescs(index, 1U, &src, dst), SUCCESS);
  ASSERT_EQ(dst.size(), 1U);
  EXPECT_EQ(dst[0].remote_buf, reinterpret_cast<void *>(0x2000U));
  EXPECT_EQ(dst[0].local_buf, local_buf);
  EXPECT_EQ(dst[0].len, 256U);

  // A range reaching past the imported segment, or not imported at all, is rejected.
  src.len = 512U;
  EXPECT_EQ(UbMemEndpoint::TranslateRemoteOpDescs(index, 1U, &src, dst), PARAM_INVALID);
  src.remote_buf = reinterpret_cast<void *>(0x3000U);
  src.len = 8U;
  EXPECT_EQ(UbMemEndpoint::TranslateRemoteOpDescs(index, 1U, &src, dst), PARAM_INVALID);

  src.len = 0U;
  EXPECT_EQ(UbMemEndpoint::TranslateRemoteOpDescs(index, 1U, &src, dst), PARAM_INVALID);

  // Two adjacent imported segments: one range becomes one descriptor per segment, the first one only
  // covering what is left of its segment.
  UbMemRemoteIndex split_index;
  split_index.emplace(0x1000U, VaInfo{0x2000U, 128U, /*imported=*/true});
  split_index.emplace(0x1080U, VaInfo{0x3000U, 128U, /*imported=*/true});
  src.remote_buf = reinterpret_cast<void *>(0x1010U);
  src.len = 240U;
  ASSERT_EQ(UbMemEndpoint::TranslateRemoteOpDescs(split_index, 1U, &src, dst), SUCCESS);
  ASSERT_EQ(dst.size(), 2U);
  EXPECT_EQ(dst[0].remote_buf, reinterpret_cast<void *>(0x2010U));
  EXPECT_EQ(dst[0].local_buf, local_buf);
  EXPECT_EQ(dst[0].len, 112U);
  EXPECT_EQ(dst[1].remote_buf, reinterpret_cast<void *>(0x3000U));
  EXPECT_EQ(dst[1].local_buf, reinterpret_cast<void *>(local_buf + 112U));
  EXPECT_EQ(dst[1].len, 128U);
}

TEST_F(UbMemEndpointUt, ShareHandleTranslateRemoteOpDescsKeepsMergedRangeWhole) {
  // Import lays every PA block of one peer range at its peer offset inside one reserved VA, so the
  // translation table carries a single contiguous entry and an op inside it never needs a split.
  UbMemRemoteIndex index;
  index.emplace(0x1000U, VaInfo{0x2000U, 256U, /*imported=*/true});

  uint8_t local_buf[128] = {};
  HixlOneSideOpDesc src{};
  src.local_buf = local_buf;
  src.remote_buf = reinterpret_cast<void *>(0x1040U);
  src.len = 128U;
  std::vector<HixlOneSideOpDesc> dst;
  ASSERT_EQ(UbMemEndpoint::TranslateRemoteOpDescs(index, 1U, &src, dst), SUCCESS);
  ASSERT_EQ(dst.size(), 1U);
  EXPECT_EQ(dst[0].remote_buf, reinterpret_cast<void *>(0x2040U));
  EXPECT_EQ(dst[0].local_buf, local_buf);
  EXPECT_EQ(dst[0].len, 128U);
}

}  // namespace hixl
