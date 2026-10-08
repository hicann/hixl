/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <string>
#include <vector>
#include "cs/ubmem/ubmem_allocator.h"
#include "slog_stub.h"
#include "ubmem_runtime_stub.h"

namespace hixl {
namespace {
using namespace ubmem_test;

void ExpectMallocRollsBackOnAclFailure(MemType type, const char *acl_api,
                                       const std::shared_ptr<UbMemRuntimeStub> &runtime) {
  llm::GetAclStubMock() = acl_api;
  void *ptr = nullptr;
  EXPECT_NE(UbMemAllocator::MallocMem(type, kLen, &ptr), SUCCESS);
  EXPECT_EQ(ptr, nullptr);
  EXPECT_EQ(runtime->malloc_physical_count_, 1U);
  EXPECT_EQ(runtime->free_physical_count_, 1U);
  llm::GetAclStubMock().clear();

  void *ptr2 = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(type, kLen, &ptr2), SUCCESS);
  ASSERT_NE(ptr2, nullptr);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr2), SUCCESS);
}
}  // namespace

TEST_F(UbMemMemoryUTest, RegisterDeregisterAndGetShareHandles) {
  MemHandle invalid_handle = nullptr;
  EXPECT_EQ(memory_.RegisterMem({0U, kLen}, MEM_HOST, invalid_handle), PARAM_INVALID);
  EXPECT_EQ(memory_.RegisterMem({kLocalAddr, 0U}, MEM_HOST, invalid_handle), PARAM_INVALID);

  MemHandle host_handle = nullptr;
  EXPECT_EQ(memory_.RegisterMem({kLocalAddr, kLen}, MEM_HOST, host_handle), SUCCESS);
  ASSERT_NE(host_handle, nullptr);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory_.GetShareHandles(host_handle, handles), SUCCESS);
  ASSERT_EQ(handles.size(), 1U);
  EXPECT_EQ(handles[0].va_addr, kLocalAddr);
  EXPECT_EQ(handles[0].len, kLen);
  EXPECT_EQ(runtime_->mem_import_count_, 0U);
  EXPECT_EQ(runtime_->mem_set_access_count_, 0U);

  MemHandle duplicate_handle = nullptr;
  EXPECT_EQ(memory_.RegisterMem({kLocalAddr, kLen}, MEM_HOST, duplicate_handle), SUCCESS);
  EXPECT_EQ(duplicate_handle, host_handle);

  EXPECT_EQ(memory_.DeregisterMem(host_handle), SUCCESS);
  EXPECT_EQ(memory_.DeregisterMem(host_handle), SUCCESS);
  std::vector<ShareHandleInfo> after_deregister;
  EXPECT_EQ(memory_.GetShareHandles(host_handle, after_deregister), PARAM_INVALID);
}

TEST_F(UbMemMemoryUTest, ImportIdentityBindsOnlyOwnExports) {
  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kLocalAddr, kLen}, MEM_HOST, handle), SUCCESS);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory_.GetShareHandles(handle, handles), SUCCESS);
  ASSERT_EQ(handles.size(), 1U);

  ASSERT_EQ(memory_.Import({handles[0], BuildShareHandle(kRemoteOldAddr, kLen)}), SUCCESS);
  EXPECT_EQ(runtime_->mem_import_count_, 1U);
  EXPECT_EQ(runtime_->mem_map_count_, 1U);
  const auto index = memory_.GetTranslationIndex();
  ASSERT_NE(index, nullptr);
  ASSERT_EQ(index->size(), 2U);
  EXPECT_EQ(index->find(kLocalAddr)->second.va_addr, kLocalAddr);
  EXPECT_NE(index->find(kRemoteOldAddr)->second.va_addr, kRemoteOldAddr);
}

TEST_F(UbMemMemoryUTest, ImportIdentityBindsRangeExportedByAnotherEndpoint) {
  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kLocalAddr, kLen}, MEM_HOST, handle), SUCCESS);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory_.GetShareHandles(handle, handles), SUCCESS);
  ASSERT_EQ(handles.size(), 1U);

  // The second object stands for another endpoint of this process. ACL refuses to import an export of
  // this process, so the range is bound to its original VA without an import or a VMM reservation.
  UbMemMemory other;
  ASSERT_EQ(other.Import(handles), SUCCESS);
  EXPECT_EQ(runtime_->mem_import_count_, 0U);
  EXPECT_EQ(runtime_->mem_map_count_, 0U);
  const auto bound = other.GetTranslationIndex();
  ASSERT_NE(bound, nullptr);
  ASSERT_EQ(bound->size(), 1U);
  EXPECT_EQ(bound->begin()->first, kLocalAddr);
  EXPECT_EQ(bound->begin()->second.va_addr, kLocalAddr);
  EXPECT_FALSE(bound->begin()->second.imported);

  other.Finalize();
  EXPECT_EQ(runtime_->mem_unmap_count_, 0U);

  // Deregistering ends the export, so the same handle now belongs to a peer and has to be imported.
  ASSERT_EQ(memory_.DeregisterMem(handle), SUCCESS);
  UbMemMemory peer;
  ASSERT_EQ(peer.Import(handles), SUCCESS);
  EXPECT_EQ(runtime_->mem_import_count_, 1U);
  EXPECT_EQ(runtime_->mem_map_count_, 1U);
  peer.Finalize();
  EXPECT_EQ(runtime_->mem_unmap_count_, 1U);
}

TEST_F(UbMemMemoryUTest, RegisterDeviceMemoryDoesNotSetHostAccess) {
  MemHandle device_handle = nullptr;
  EXPECT_EQ(memory_.RegisterMem({kLocalAddr, kLen}, MEM_DEVICE, device_handle), SUCCESS);
  ASSERT_NE(device_handle, nullptr);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory_.GetShareHandles(device_handle, handles), SUCCESS);
  ASSERT_EQ(handles.size(), 1U);
  EXPECT_EQ(runtime_->mem_import_count_, 0U);
  EXPECT_EQ(runtime_->mem_set_access_count_, 0U);
  EXPECT_EQ(memory_.DeregisterMem(device_handle), SUCCESS);
}

TEST_F(UbMemMemoryUTest, RegisterRejectsOverflowRange) {
  MemHandle handle = nullptr;
  MemDesc mem{};
  mem.addr = std::numeric_limits<uintptr_t>::max();
  mem.len = 2U;
  EXPECT_EQ(memory_.RegisterMem(mem, MEM_HOST, handle), PARAM_INVALID);
  EXPECT_EQ(handle, nullptr);
}

TEST_F(UbMemMemoryUTest, DeregisterUnknownHandleIsNoOp) {
  EXPECT_EQ(memory_.DeregisterMem(reinterpret_cast<MemHandle>(0xDEAD)), SUCCESS);
}

TEST_F(UbMemMemoryUTest, RegisterOwnMemoryExportsOnceBeforePublicExport) {
  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_DEVICE, kLen, &ptr), SUCCESS);
  aclrtDrvMemHandle pa_handle = nullptr;
  ASSERT_EQ(UbMemAllocator::GetPaHandleFromVa(reinterpret_cast<uintptr_t>(ptr), pa_handle), SUCCESS);

  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({reinterpret_cast<uintptr_t>(ptr), kLen}, MEM_DEVICE, handle), SUCCESS);
  EXPECT_EQ(handle, pa_handle);
  EXPECT_EQ(runtime_->get_address_range_count_, 0U);
  EXPECT_EQ(runtime_->retain_count_, 0U);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  std::vector<ShareHandleInfo> registered_handles;
  ASSERT_EQ(memory_.GetShareHandles(handle, registered_handles), SUCCESS);
  ASSERT_EQ(registered_handles.size(), 1U);

  aclrtMemFabricHandle public_handle{};
  ASSERT_EQ(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(ptr), public_handle), SUCCESS);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(memcmp(registered_handles[0].share_handle.data, public_handle.data, sizeof(public_handle.data)), 0);

  EXPECT_EQ(memory_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
}

TEST_F(UbMemMemoryUTest, RegisterOwnHostDoesNotImportOrSetAccessAgain) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  EXPECT_EQ(runtime_->mem_set_access_count_, 1U);
  EXPECT_EQ(runtime_->mem_import_count_, 0U);

  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({reinterpret_cast<uintptr_t>(ptr), kLen}, MEM_HOST, handle), SUCCESS);
  EXPECT_EQ(runtime_->retain_count_, 0U);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(runtime_->mem_import_count_, 0U);
  EXPECT_EQ(runtime_->mem_set_access_count_, 1U);

  EXPECT_EQ(memory_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(UbMemMemoryUTest, RegisterOwnMemoryReusesPreviouslyExportedHandle) {
  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_DEVICE, kLen, &ptr), SUCCESS);
  aclrtMemFabricHandle public_handle{};
  ASSERT_EQ(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(ptr), public_handle), SUCCESS);
  ASSERT_EQ(runtime_->mem_export_count_, 1U);

  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({reinterpret_cast<uintptr_t>(ptr), kLen}, MEM_DEVICE, handle), SUCCESS);
  EXPECT_EQ(runtime_->get_address_range_count_, 0U);
  EXPECT_EQ(runtime_->retain_count_, 0U);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  std::vector<ShareHandleInfo> registered_handles;
  ASSERT_EQ(memory_.GetShareHandles(handle, registered_handles), SUCCESS);
  ASSERT_EQ(registered_handles.size(), 1U);
  EXPECT_EQ(memcmp(registered_handles[0].share_handle.data, public_handle.data, sizeof(public_handle.data)), 0);

  EXPECT_EQ(memory_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
}

TEST_F(UbMemMemoryUTest, RegisterDeviceForeignSplitsMultiplePaBlocks) {
  constexpr uintptr_t kBase0 = 0x1000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr uintptr_t kBase1 = kBase0 + kBlock;
  runtime_->SetAddressRanges({{kBase0, kBlock}, {kBase1, kBlock}});

  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase0, kBlock * 2U}, MEM_DEVICE, handle), SUCCESS);
  ASSERT_NE(handle, nullptr);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory_.GetShareHandles(handle, handles), SUCCESS);
  ASSERT_EQ(handles.size(), 2U);
  EXPECT_EQ(handles[0].va_addr, kBase0);
  EXPECT_EQ(handles[0].len, kBlock);
  EXPECT_EQ(handles[1].va_addr, kBase1);
  EXPECT_EQ(handles[1].len, kBlock);
  EXPECT_EQ(runtime_->get_address_range_count_, 2U);
  EXPECT_EQ(runtime_->retain_count_, 2U);
  EXPECT_EQ(runtime_->mem_export_count_, 2U);
  EXPECT_EQ(runtime_->retained_addresses_, (std::vector<uintptr_t>{kBase0, kBase1}));

  EXPECT_EQ(memory_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 2U);
  std::vector<ShareHandleInfo> after_deregister;
  EXPECT_EQ(memory_.GetShareHandles(handle, after_deregister), PARAM_INVALID);
}

TEST_F(UbMemMemoryUTest, DeviceForeignImportMapsEveryBlockIntoOneContiguousVa) {
  constexpr uintptr_t kBase0 = 0x1000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr uintptr_t kBase1 = kBase0 + kBlock;
  runtime_->SetAddressRanges({{kBase0, kBlock}, {kBase1, kBlock}});

  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase0, kBlock * 2U}, MEM_DEVICE, handle), SUCCESS);
  std::vector<ShareHandleInfo> exported;
  ASSERT_EQ(memory_.GetShareHandles(handle, exported), SUCCESS);
  ASSERT_EQ(exported.size(), 2U);
  // Simulate the peer's own export: the ranges match, the handle bytes are ones this process never made.
  for (auto &info : exported) {
    info.share_handle.data[0] = static_cast<uint8_t>(info.share_handle.data[0] ^ 0xFFU);
  }

  // One 1 GB block is reserved for the whole span and each PA block is mapped at the offset the peer
  // used, so the two ranges stay adjacent locally and the translation table collapses them into one.
  UbMemMemory remote_memory;
  ASSERT_EQ(remote_memory.Import(exported), SUCCESS);
  EXPECT_EQ(runtime_->mem_import_count_, 2U);
  EXPECT_EQ(runtime_->mem_map_count_, 2U);

  const auto index = remote_memory.GetTranslationIndex();
  ASSERT_NE(index, nullptr);
  ASSERT_EQ(index->size(), 1U);
  EXPECT_EQ(index->begin()->first, kBase0);
  EXPECT_EQ(index->begin()->second.len, kBlock * 2U);
  EXPECT_TRUE(index->begin()->second.imported);

  remote_memory.Finalize();
  EXPECT_EQ(runtime_->mem_unmap_count_, 2U);
  EXPECT_EQ(runtime_->free_physical_count_, 2U);
  EXPECT_EQ(memory_.DeregisterMem(handle), SUCCESS);
}

TEST_F(UbMemMemoryUTest, RegisterForeignOffsetAdvertisesFullBlock) {
  constexpr uintptr_t kBase = 0x1000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr size_t kOffset = 0x100U;
  runtime_->SetAddressRanges({{kBase, kBlock}});

  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase + kOffset, kBlock - kOffset}, MEM_DEVICE, handle), SUCCESS);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory_.GetShareHandles(handle, handles), SUCCESS);
  ASSERT_EQ(handles.size(), 1U);
  EXPECT_EQ(handles[0].va_addr, kBase);
  EXPECT_EQ(handles[0].len, kBlock);
  EXPECT_EQ(runtime_->retained_addresses_, (std::vector<uintptr_t>{kBase}));
  EXPECT_EQ(memory_.DeregisterMem(handle), SUCCESS);
}

TEST_F(UbMemMemoryUTest, RegisterForeignReusesAdjacentRangesOnSamePaBlock) {
  constexpr uintptr_t kBase = 0x1000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr size_t kHalf = kBlock / 2U;
  runtime_->SetAddressRanges({{kBase, kBlock}});

  MemHandle first = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase, kHalf}, MEM_DEVICE, first), SUCCESS);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(runtime_->retain_count_, 1U);

  MemHandle second = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase + kHalf, kHalf}, MEM_DEVICE, second), SUCCESS);
  ASSERT_NE(second, nullptr);
  EXPECT_NE(second, first);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(runtime_->retain_count_, 1U);
  std::vector<ShareHandleInfo> exported;
  EXPECT_EQ(memory_.GetShareHandles(first, exported), SUCCESS);
  EXPECT_EQ(exported.size(), 1U);

  EXPECT_EQ(memory_.DeregisterMem(first), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 0U);
  std::vector<ShareHandleInfo> remaining;
  EXPECT_EQ(memory_.GetShareHandles(second, remaining), SUCCESS);
  EXPECT_EQ(remaining.size(), 1U);

  EXPECT_EQ(memory_.DeregisterMem(second), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 1U);
  std::vector<ShareHandleInfo> after_deregister;
  EXPECT_EQ(memory_.GetShareHandles(second, after_deregister), PARAM_INVALID);
}

TEST_F(UbMemMemoryUTest, FinalizeReleasesSharedForeignPaPages) {
  constexpr uintptr_t kBase = 0x3000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr size_t kHalf = kBlock / 2U;
  runtime_->SetAddressRanges({{kBase, kBlock}});

  MemHandle first = nullptr;
  MemHandle second = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase, kHalf}, MEM_DEVICE, first), SUCCESS);
  ASSERT_EQ(memory_.RegisterMem({kBase + kHalf, kHalf}, MEM_DEVICE, second), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 0U);

  EXPECT_NO_THROW(memory_.Finalize());
  EXPECT_EQ(runtime_->free_physical_count_, 1U);
  std::vector<ShareHandleInfo> after_finalize;
  EXPECT_EQ(memory_.GetShareHandles(first, after_finalize), PARAM_INVALID);
}

TEST_F(UbMemMemoryUTest, RegisterForeignReusesSharedPaThenExportsNewPage) {
  constexpr uintptr_t kBase0 = 0x1000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr size_t kHalf = kBlock / 2U;
  constexpr uintptr_t kBase1 = kBase0 + kBlock;
  runtime_->SetAddressRanges({{kBase0, kBlock}, {kBase1, kBlock}});

  MemHandle first = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase0, kHalf}, MEM_DEVICE, first), SUCCESS);
  MemHandle second = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase0 + kHalf, kHalf + kBlock}, MEM_DEVICE, second), SUCCESS);
  EXPECT_EQ(runtime_->retain_count_, 2U);
  EXPECT_EQ(runtime_->mem_export_count_, 2U);
  std::vector<ShareHandleInfo> exported;
  EXPECT_EQ(memory_.GetShareHandles(second, exported), SUCCESS);
  EXPECT_EQ(exported.size(), 2U);

  EXPECT_EQ(memory_.DeregisterMem(first), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 0U);
  std::vector<ShareHandleInfo> remaining;
  EXPECT_EQ(memory_.GetShareHandles(second, remaining), SUCCESS);
  EXPECT_EQ(remaining.size(), 2U);

  EXPECT_EQ(memory_.DeregisterMem(second), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 2U);
  std::vector<ShareHandleInfo> after_deregister;
  EXPECT_EQ(memory_.GetShareHandles(second, after_deregister), PARAM_INVALID);
}

TEST_F(UbMemMemoryUTest, ForeignShareHandleIsReusedAcrossMemoryInstances) {
  constexpr uintptr_t kBase = 0x4000000UL;
  constexpr size_t kBlock = 0x1000U;
  runtime_->SetAddressRanges({{kBase, kBlock}});

  UbMemMemory first_instance;
  MemHandle first_handle = nullptr;
  ASSERT_EQ(first_instance.RegisterMem({kBase, kBlock}, MEM_DEVICE, first_handle), SUCCESS);
  std::vector<ShareHandleInfo> first_handles;
  ASSERT_EQ(first_instance.GetShareHandles(first_handle, first_handles), SUCCESS);
  ASSERT_EQ(first_handles.size(), 1U);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(runtime_->retain_count_, 1U);

  // The second instance registers the same physical block. The process-level cache returns the
  // previously exported handle, so ACL export must not run a second time.
  UbMemMemory second_instance;
  MemHandle second_handle = nullptr;
  ASSERT_EQ(second_instance.RegisterMem({kBase, kBlock}, MEM_DEVICE, second_handle), SUCCESS);
  std::vector<ShareHandleInfo> second_handles;
  ASSERT_EQ(second_instance.GetShareHandles(second_handle, second_handles), SUCCESS);
  ASSERT_EQ(second_handles.size(), 1U);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(runtime_->retain_count_, 2U);
  EXPECT_EQ(memcmp(first_handles[0].share_handle.data, second_handles[0].share_handle.data,
                   sizeof(first_handles[0].share_handle.data)),
            0);

  // Each registration retains its own PA handle and releases it on deregistration.
  EXPECT_EQ(first_instance.DeregisterMem(first_handle), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 1U);
  EXPECT_EQ(second_instance.DeregisterMem(second_handle), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 2U);
}

TEST_F(UbMemMemoryUTest, ForeignShareHandleCacheInvalidatesWhenRangeIsReleased) {
  constexpr uintptr_t kBase = 0x9000000UL;
  constexpr size_t kBlock = 0x1000U;
  runtime_->SetAddressRanges({{kBase, kBlock}});

  UbMemMemory first_instance;
  MemHandle first_handle = nullptr;
  ASSERT_EQ(first_instance.RegisterMem({kBase, kBlock}, MEM_DEVICE, first_handle), SUCCESS);
  std::vector<ShareHandleInfo> first_handles;
  ASSERT_EQ(first_instance.GetShareHandles(first_handle, first_handles), SUCCESS);
  ASSERT_EQ(first_instance.DeregisterMem(first_handle), SUCCESS);

  // A later allocation may reuse the same address and length. It must produce a fresh export rather
  // than return the cached handle of the freed physical block.
  UbMemMemory second_instance;
  MemHandle second_handle = nullptr;
  ASSERT_EQ(second_instance.RegisterMem({kBase, kBlock}, MEM_DEVICE, second_handle), SUCCESS);
  std::vector<ShareHandleInfo> second_handles;
  ASSERT_EQ(second_instance.GetShareHandles(second_handle, second_handles), SUCCESS);
  EXPECT_EQ(runtime_->mem_export_count_, 2U);
  EXPECT_NE(memcmp(first_handles[0].share_handle.data, second_handles[0].share_handle.data,
                   sizeof(first_handles[0].share_handle.data)),
            0);
  ASSERT_EQ(second_instance.DeregisterMem(second_handle), SUCCESS);
}

TEST_F(UbMemMemoryUTest, ForeignShareHandleCacheIsKeyedByResolvedBlock) {
  constexpr uintptr_t kBase = 0x5000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr size_t kHalf = kBlock / 2U;
  runtime_->SetAddressRanges({{kBase, kBlock}});

  UbMemMemory first_instance;
  MemHandle first_handle = nullptr;
  ASSERT_EQ(first_instance.RegisterMem({kBase, kHalf}, MEM_DEVICE, first_handle), SUCCESS);
  std::vector<ShareHandleInfo> first_handles;
  ASSERT_EQ(first_instance.GetShareHandles(first_handle, first_handles), SUCCESS);
  ASSERT_EQ(first_handles.size(), 1U);
  EXPECT_EQ(first_handles[0].va_addr, kBase);
  EXPECT_EQ(first_handles[0].len, kBlock);

  // A different user range inside the same physical block resolves to the same block key, so the
  // second registration reuses the cached handle and no second ACL export happens.
  UbMemMemory second_instance;
  MemHandle second_handle = nullptr;
  ASSERT_EQ(second_instance.RegisterMem({kBase + kHalf, kHalf}, MEM_DEVICE, second_handle), SUCCESS);
  std::vector<ShareHandleInfo> second_handles;
  ASSERT_EQ(second_instance.GetShareHandles(second_handle, second_handles), SUCCESS);
  ASSERT_EQ(second_handles.size(), 1U);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(memcmp(first_handles[0].share_handle.data, second_handles[0].share_handle.data,
                   sizeof(first_handles[0].share_handle.data)),
            0);

  EXPECT_EQ(first_instance.DeregisterMem(first_handle), SUCCESS);
  EXPECT_EQ(second_instance.DeregisterMem(second_handle), SUCCESS);
}

TEST_F(UbMemMemoryUTest, ForeignShareHandleCacheDistinguishesPhysicalBlocks) {
  constexpr uintptr_t kBase0 = 0x6000000UL;
  constexpr uintptr_t kBase1 = 0x7000000UL;
  constexpr size_t kBlock = 0x1000U;
  runtime_->SetAddressRanges({{kBase0, kBlock}, {kBase1, kBlock}});

  MemHandle first_handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase0, kBlock}, MEM_DEVICE, first_handle), SUCCESS);
  MemHandle second_handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase1, kBlock}, MEM_DEVICE, second_handle), SUCCESS);
  std::vector<ShareHandleInfo> first_handles;
  std::vector<ShareHandleInfo> second_handles;
  ASSERT_EQ(memory_.GetShareHandles(first_handle, first_handles), SUCCESS);
  ASSERT_EQ(memory_.GetShareHandles(second_handle, second_handles), SUCCESS);
  ASSERT_EQ(first_handles.size(), 1U);
  ASSERT_EQ(second_handles.size(), 1U);
  EXPECT_EQ(runtime_->mem_export_count_, 2U);
  EXPECT_NE(memcmp(first_handles[0].share_handle.data, second_handles[0].share_handle.data,
                   sizeof(first_handles[0].share_handle.data)),
            0);

  EXPECT_EQ(memory_.DeregisterMem(first_handle), SUCCESS);
  EXPECT_EQ(memory_.DeregisterMem(second_handle), SUCCESS);
}

TEST_F(UbMemMemoryUTest, ForeignShareHandleExportFailureIsNotCached) {
  constexpr uintptr_t kBase = 0x8000000UL;
  constexpr size_t kBlock = 0x1000U;
  runtime_->SetAddressRanges({{kBase, kBlock}});
  runtime_->export_fail_once_ = ACL_ERROR_RT_INTERNAL_ERROR;

  MemHandle failed_handle = nullptr;
  EXPECT_NE(memory_.RegisterMem({kBase, kBlock}, MEM_DEVICE, failed_handle), SUCCESS);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(runtime_->free_physical_count_, 1U);

  // A failed export must not leave a cache entry behind; the retry exports and caches successfully.
  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase, kBlock}, MEM_DEVICE, handle), SUCCESS);
  EXPECT_EQ(runtime_->mem_export_count_, 2U);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory_.GetShareHandles(handle, handles), SUCCESS);
  ASSERT_EQ(handles.size(), 1U);
  aclrtMemFabricHandle zero_handle{};
  EXPECT_NE(memcmp(handles[0].share_handle.data, zero_handle.data, sizeof(zero_handle.data)), 0);

  EXPECT_EQ(memory_.DeregisterMem(handle), SUCCESS);
  EXPECT_EQ(runtime_->free_physical_count_, 2U);
}

TEST_F(UbMemMemoryUTest, RegisterForeignRejectsOverlappingUserRanges) {
  constexpr uintptr_t kBase = 0x1000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr size_t kHalf = kBlock / 2U;
  runtime_->SetAddressRanges({{kBase, kBlock}});

  MemHandle first = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase, kBlock}, MEM_DEVICE, first), SUCCESS);
  MemHandle second = nullptr;
  EXPECT_EQ(memory_.RegisterMem({kBase + kHalf, kHalf}, MEM_DEVICE, second), PARAM_INVALID);
  EXPECT_EQ(second, nullptr);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(memory_.DeregisterMem(first), SUCCESS);
}

TEST_F(UbMemMemoryUTest, RegisterForeignRollsBackEarlierSegmentsWhenRetainFails) {
  constexpr uintptr_t kBase0 = 0x1000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr uintptr_t kBase1 = kBase0 + kBlock;
  runtime_->SetAddressRanges({{kBase0, kBlock}, {kBase1, kBlock}});
  runtime_->retain_fail_on_count_ = 2U;

  MemHandle handle = nullptr;
  EXPECT_NE(memory_.RegisterMem({kBase0, kBlock * 2U}, MEM_DEVICE, handle), SUCCESS);
  EXPECT_EQ(handle, nullptr);
  EXPECT_EQ(runtime_->retain_count_, 2U);
  EXPECT_EQ(runtime_->mem_export_count_, 1U);
  EXPECT_EQ(runtime_->free_physical_count_, 1U);
}

TEST_F(UbMemMemoryUTest, RegisterHostForeignMultiBlockExportsWithoutLocalImport) {
  constexpr uintptr_t kBase0 = 0x1000000UL;
  constexpr size_t kBlock = 0x1000U;
  constexpr uintptr_t kBase1 = kBase0 + kBlock;
  runtime_->SetAddressRanges({{kBase0, kBlock}, {kBase1, kBlock}});

  MemHandle handle = nullptr;
  ASSERT_EQ(memory_.RegisterMem({kBase0, kBlock * 2U}, MEM_HOST, handle), SUCCESS);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory_.GetShareHandles(handle, handles), SUCCESS);
  ASSERT_EQ(handles.size(), 2U);
  EXPECT_EQ(handles[0].va_addr, kBase0);
  EXPECT_EQ(handles[0].len, kBlock);
  EXPECT_EQ(handles[1].va_addr, kBase1);
  EXPECT_EQ(handles[1].len, kBlock);
  EXPECT_EQ(runtime_->mem_export_count_, 2U);
  EXPECT_EQ(runtime_->mem_import_count_, 0U);
  EXPECT_EQ(runtime_->mem_set_access_count_, 0U);

  EXPECT_EQ(memory_.DeregisterMem(handle), SUCCESS);
}

TEST(UbMemMemoryImportUTest, ImportFinalizeRoundTrip) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  UbMemMemory remote_memory;
  EXPECT_EQ(remote_memory.GetTranslationIndex(), nullptr);
  ASSERT_EQ(remote_memory.Import({BuildShareHandle(kRemoteOldAddr, kLen)}), SUCCESS);
  const auto index = remote_memory.GetTranslationIndex();
  ASSERT_NE(index, nullptr);
  ASSERT_EQ(index->size(), 1U);
  EXPECT_EQ(index->begin()->first, kRemoteOldAddr);
  EXPECT_EQ(index->begin()->second.len, kLen);

  remote_memory.Finalize();
  EXPECT_EQ(remote_memory.GetTranslationIndex(), nullptr);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(UbMemMemoryImportUTest, ImportIdentityBindsOwnExport) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  UbMemMemory memory;
  MemHandle handle = nullptr;
  ASSERT_EQ(memory.RegisterMem({kLocalAddr, kLen}, MEM_HOST, handle), SUCCESS);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory.GetShareHandles(handle, handles), SUCCESS);
  const auto &info = handles[0];
  ASSERT_EQ(memory.Import({info}), SUCCESS);
  // The peer is this process, so the handle is never imported or mapped and the address is unchanged.
  EXPECT_EQ(runtime->mem_import_count_, 0U);
  EXPECT_EQ(runtime->mem_map_count_, 0U);

  const auto index = memory.GetTranslationIndex();
  ASSERT_NE(index, nullptr);
  ASSERT_EQ(index->size(), 1U);
  EXPECT_EQ(index->begin()->first, kLocalAddr);
  EXPECT_EQ(index->begin()->second.va_addr, kLocalAddr);
  EXPECT_EQ(index->begin()->second.len, kLen);

  memory.Finalize();
  // Nothing was mapped or reserved here, so teardown must not unmap or release it.
  EXPECT_EQ(runtime->mem_unmap_count_, 0U);
  EXPECT_EQ(memory.GetTranslationIndex(), nullptr);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(UbMemMemoryImportUTest, IdentityBindAndForeignImportCoexist) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  UbMemMemory memory;
  MemHandle handle = nullptr;
  ASSERT_EQ(memory.RegisterMem({kLocalAddr, kLen}, MEM_HOST, handle), SUCCESS);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory.GetShareHandles(handle, handles), SUCCESS);
  ASSERT_EQ(memory.Import({handles[0]}), SUCCESS);
  ASSERT_EQ(memory.Import({BuildShareHandle(kRemoteOldAddr, kLen)}), SUCCESS);
  EXPECT_EQ(runtime->mem_map_count_, 1U);

  const auto index = memory.GetTranslationIndex();
  ASSERT_NE(index, nullptr);
  ASSERT_EQ(index->size(), 2U);
  ASSERT_EQ(index->count(kLocalAddr), 1U);
  ASSERT_EQ(index->count(kRemoteOldAddr), 1U);
  EXPECT_EQ(index->find(kLocalAddr)->second.va_addr, kLocalAddr);
  EXPECT_NE(index->find(kRemoteOldAddr)->second.va_addr, kRemoteOldAddr);

  memory.Finalize();
  // Only the imported range is unmapped; the identity-bound one was never mapped here.
  EXPECT_EQ(runtime->mem_unmap_count_, 1U);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(UbMemMemoryImportUTest, ImportRollsBackOnMapFailure) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  llm::GetAclStubMock() = "aclrtMapMem";
  UbMemMemory remote_memory;
  EXPECT_NE(remote_memory.Import({BuildShareHandle(kRemoteOldAddr, kLen)}), SUCCESS);
  EXPECT_EQ(remote_memory.GetTranslationIndex(), nullptr);
  llm::GetAclStubMock().clear();
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(UbMemMemoryImportUTest, UnimportReleasesOnlyTheNamedRange) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  const auto first = BuildShareHandle(kRemoteOldAddr, kLen);
  const auto second = BuildShareHandle(kRemoteOldAddr + kLen, kLen);
  UbMemMemory remote_memory;
  ASSERT_EQ(remote_memory.Import({first, second}), SUCCESS);
  EXPECT_EQ(runtime->mem_map_count_, 2U);
  // The two ranges are adjacent on both sides, so the index holds one entry for the whole span.
  const auto merged = remote_memory.GetTranslationIndex();
  ASSERT_NE(merged, nullptr);
  ASSERT_EQ(merged->size(), 1U);
  const uintptr_t block_base = merged->begin()->second.va_addr;

  ASSERT_EQ(remote_memory.Unimport({first}), SUCCESS);
  EXPECT_EQ(runtime->mem_unmap_count_, 1U);
  EXPECT_EQ(runtime->free_physical_count_, 1U);
  const auto index = remote_memory.GetTranslationIndex();
  ASSERT_NE(index, nullptr);
  ASSERT_EQ(index->size(), 1U);
  EXPECT_EQ(index->count(kRemoteOldAddr), 0U);
  EXPECT_EQ(index->count(kRemoteOldAddr + kLen), 1U);
  // The named range is gone but its block is still reserved for its sibling, at the same offset.
  EXPECT_EQ(index->find(kRemoteOldAddr + kLen)->second.va_addr, block_base + kLen);

  // The range that was not named keeps its mapping until teardown.
  remote_memory.Finalize();
  EXPECT_EQ(runtime->mem_unmap_count_, 2U);
  EXPECT_EQ(runtime->free_physical_count_, 2U);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(UbMemMemoryImportUTest, UnimportDropsIdentityBoundRangeWithoutAclCalls) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  UbMemMemory memory;
  MemHandle handle = nullptr;
  ASSERT_EQ(memory.RegisterMem({kLocalAddr, kLen}, MEM_HOST, handle), SUCCESS);
  std::vector<ShareHandleInfo> handles;
  ASSERT_EQ(memory.GetShareHandles(handle, handles), SUCCESS);
  const auto &info = handles[0];
  ASSERT_EQ(memory.Import({info}), SUCCESS);
  ASSERT_EQ(memory.GetTranslationIndex()->size(), 1U);

  ASSERT_EQ(memory.Unimport({info}), SUCCESS);
  // An identity bind owns neither a mapping nor a VMM block, so dropping it must not touch ACL.
  EXPECT_EQ(runtime->mem_unmap_count_, 0U);
  EXPECT_EQ(runtime->free_physical_count_, 0U);
  const auto index = memory.GetTranslationIndex();
  ASSERT_NE(index, nullptr);
  EXPECT_TRUE(index->empty());

  memory.Finalize();
  EXPECT_EQ(runtime->mem_unmap_count_, 0U);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(UbMemMemoryImportUTest, UnimportUnknownHandleIsNoOp) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  UbMemMemory remote_memory;
  ASSERT_EQ(remote_memory.Import({BuildShareHandle(kRemoteOldAddr, kLen)}), SUCCESS);

  // Same VA and length, different share handle bytes: another process exported this range.
  auto foreign = BuildShareHandle(kRemoteOldAddr, kLen);
  foreign.share_handle.data[0] = static_cast<uint8_t>(foreign.share_handle.data[0] ^ 0xFFU);
  EXPECT_EQ(remote_memory.Unimport({foreign}), SUCCESS);
  EXPECT_EQ(runtime->mem_unmap_count_, 0U);
  EXPECT_EQ(runtime->free_physical_count_, 0U);
  ASSERT_NE(remote_memory.GetTranslationIndex(), nullptr);
  EXPECT_EQ(remote_memory.GetTranslationIndex()->size(), 1U);

  remote_memory.Finalize();
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(UbMemMemoryImportUTest, UnimportReturnsVmmBlockForReimport) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  const auto info = BuildShareHandle(kRemoteOldAddr, kLen);
  UbMemMemory remote_memory;
  ASSERT_EQ(remote_memory.Import({info}), SUCCESS);
  const uintptr_t first_mapped = remote_memory.GetTranslationIndex()->begin()->second.va_addr;

  // A connect retry re-imports the same range, so the earlier generation must be gone first.
  ASSERT_EQ(remote_memory.Unimport({info}), SUCCESS);
  ASSERT_EQ(remote_memory.Import({info}), SUCCESS);
  const uintptr_t second_mapped = remote_memory.GetTranslationIndex()->begin()->second.va_addr;
  EXPECT_EQ(second_mapped, first_mapped);
  EXPECT_EQ(runtime->mem_map_count_, 2U);
  EXPECT_EQ(runtime->free_physical_count_, 1U);

  remote_memory.Finalize();
  EXPECT_EQ(runtime->mem_unmap_count_, 2U);
  EXPECT_EQ(runtime->free_physical_count_, 2U);
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST(UbMemMemoryImportUTest, TranslationSnapshotIsSharedAndStableAcrossReplacement) {
  auto runtime = std::make_shared<UbMemRuntimeStub>();
  ScopedRuntimeMock scoped_runtime(runtime);
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  UbMemMemory memory;
  ASSERT_EQ(memory.Import({BuildShareHandle(kRemoteOldAddr, kLen)}), SUCCESS);
  const auto old_index = memory.GetTranslationIndex();
  ASSERT_NE(old_index, nullptr);
  EXPECT_EQ(old_index, memory.GetTranslationIndex());
  ASSERT_EQ(memory.Import({BuildShareHandle(kRemoteOldAddr + kLen, kLen)}), SUCCESS);
  const auto extended = memory.GetTranslationIndex();
  EXPECT_NE(old_index, extended);
  EXPECT_EQ(old_index->size(), 1U);
  ASSERT_EQ(extended->size(), 2U);
  memory.Finalize();
  EXPECT_EQ(memory.GetTranslationIndex(), nullptr);
  // Metadata can outlive teardown; it does not confer permission to use unmapped addresses.
  EXPECT_EQ(extended->size(), 2U);
  ASSERT_EQ(memory.Import({BuildShareHandle(kRemoteOldAddr + kLen * 8U, kLen)}), SUCCESS);
  EXPECT_NE(memory.GetTranslationIndex(), extended);
  EXPECT_EQ(memory.GetTranslationIndex()->count(kRemoteOldAddr), 0U);
  memory.Finalize();
  VirtualMemoryManager::GetInstance().Finalize();
}

TEST_F(UbMemAllocatorUTest, MallocMemSupportsDeviceMemory) {
  VirtualMemoryManager::GetInstance().Finalize();

  void *device_ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_DEVICE, kLen, &device_ptr), SUCCESS);
  ASSERT_NE(device_ptr, nullptr);
  EXPECT_EQ(runtime_->malloc_physical_count_, 1U);
  EXPECT_EQ(runtime_->last_physical_mem_prop_.location.type, ACL_MEM_LOCATION_TYPE_DEVICE);
  EXPECT_EQ(runtime_->last_physical_mem_prop_.location.id, 0U);
  EXPECT_EQ(runtime_->last_physical_mem_prop_.memAttr, ACL_HBM_MEM_HUGE);
  // Device Map already grants local device access; MemSetAccess is only needed for host VMM.
  EXPECT_EQ(runtime_->mem_set_access_count_, 0U);

  EXPECT_EQ(UbMemAllocator::FreeMem(device_ptr), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, MallocMemAndFreeMemHost) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  void *host_ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, sizeof(int32_t), &host_ptr), SUCCESS);
  ASSERT_NE(host_ptr, nullptr);
  auto *value = static_cast<int32_t *>(host_ptr);
  *value = 123;
  EXPECT_EQ(*value, 123);
  EXPECT_EQ(runtime_->mem_set_access_count_, 1U);
  EXPECT_EQ(runtime_->last_mem_set_access_size_, sizeof(int32_t));
  EXPECT_EQ(runtime_->last_mem_set_access_count_, 1U);
  EXPECT_EQ(runtime_->last_mem_access_desc_.flags, ACL_RT_MEM_ACCESS_FLAGS_READWRITE);
  EXPECT_EQ(runtime_->last_mem_access_desc_.location.type, ACL_MEM_LOCATION_TYPE_DEVICE);
  // Default runtime stub is 9.3.x (>= 9.2.0). aclrtGetDevice returns 0 and SetAccess must not remap it.
  EXPECT_EQ(runtime_->last_mem_access_desc_.location.id, 0U);
  EXPECT_TRUE(UbMemAllocator::IsAllocatedByMallocMem(reinterpret_cast<uintptr_t>(host_ptr)));
  EXPECT_FALSE(UbMemAllocator::IsAllocatedByMallocMem(reinterpret_cast<uintptr_t>(&host_ptr)));
  EXPECT_EQ(UbMemAllocator::FreeMem(host_ptr), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, IsAllocatedByMallocMemTracksLifecycle) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_DEVICE, kLen, &ptr), SUCCESS);
  EXPECT_TRUE(UbMemAllocator::IsAllocatedByMallocMem(reinterpret_cast<uintptr_t>(ptr)));
  ASSERT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  EXPECT_FALSE(UbMemAllocator::IsAllocatedByMallocMem(reinterpret_cast<uintptr_t>(ptr)));
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, MallocMemRejectsNullPtr) {
  EXPECT_EQ(UbMemAllocator::MallocMem(MEM_HOST, sizeof(int32_t), nullptr), PARAM_INVALID);
}
TEST_F(UbMemAllocatorUTest, ExportToShareableHandleReturnsPerAllocationHandle) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  void *first = nullptr;
  void *second = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &first), SUCCESS);
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_DEVICE, kLen, &second), SUCCESS);

  aclrtMemFabricHandle first_handle{};
  aclrtMemFabricHandle second_handle{};
  ASSERT_EQ(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(first), first_handle), SUCCESS);
  ASSERT_EQ(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(second), second_handle), SUCCESS);
  EXPECT_NE(memcmp(first_handle.data, second_handle.data, sizeof(first_handle.data)), 0);

  // Repeated calls return the cached handle; ACL export runs only once per allocation.
  aclrtMemFabricHandle again{};
  ASSERT_EQ(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(first), again), SUCCESS);
  EXPECT_EQ(memcmp(first_handle.data, again.data, sizeof(again.data)), 0);

  EXPECT_EQ(UbMemAllocator::FreeMem(first), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(second), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, ExportToShareableHandleRejectsUnknownAddress) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  aclrtMemFabricHandle handle{};
  EXPECT_EQ(UbMemAllocator::ExportToShareableHandle(0U, handle), PARAM_INVALID);
  uint8_t not_fabric_mem[kLen] = {};
  EXPECT_EQ(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(not_fabric_mem), handle),
            PARAM_INVALID);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  ASSERT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  EXPECT_EQ(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(ptr), handle), PARAM_INVALID);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, ExportToShareableHandleFailsWhenExportAclFails) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);

  void *ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, kLen, &ptr), SUCCESS);
  ASSERT_NE(ptr, nullptr);

  llm::GetAclStubMock() = "aclrtMemExportToShareableHandleV2";
  aclrtMemFabricHandle handle{};
  EXPECT_NE(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(ptr), handle), SUCCESS);
  llm::GetAclStubMock().clear();

  // Allocation survives a failed export; a later export must still succeed.
  ASSERT_EQ(UbMemAllocator::ExportToShareableHandle(reinterpret_cast<uintptr_t>(ptr), handle), SUCCESS);
  EXPECT_EQ(UbMemAllocator::FreeMem(ptr), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, MallocMemFreesPhysicalWhenReserveFails) {
  VirtualMemoryManager::GetInstance().Finalize();

  // A size larger than the virtual memory capacity makes ReserveMemory fail after the physical
  // memory has already been allocated; the allocator must release that physical handle.
  void *ptr = nullptr;
  EXPECT_EQ(UbMemAllocator::MallocMem(MEM_DEVICE, std::numeric_limits<size_t>::max(), &ptr), RESOURCE_EXHAUSTED);
  EXPECT_EQ(ptr, nullptr);
  EXPECT_EQ(runtime_->malloc_physical_count_, 1U);
  EXPECT_EQ(runtime_->free_physical_count_, 1U);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, MallocMemRollsBackWhenMapFails) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  // aclrtMapMem fails after physical alloc + VA reserve; both must be released.
  ExpectMallocRollsBackOnAclFailure(MEM_DEVICE, "aclrtMapMem", runtime_);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, MallocMemRollsBackWhenHostMemSetAccessFails) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  ExpectMallocRollsBackOnAclFailure(MEM_HOST, "aclrtMemSetAccess", runtime_);
  VirtualMemoryManager::GetInstance().Finalize();
}
struct ScopedRuntimeVersionOverride {
  ~ScopedRuntimeVersionOverride() {
    ResetRuntimeVersionStub();
  }
};

TEST_F(UbMemAllocatorUTest, MallocMemConvertsDeviceIdBeforeRuntime920) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  ScopedRuntimeVersionOverride restore_version;
  (void)restore_version;
  SetRuntimeVersionNum(90199999);
  void *host_ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, sizeof(int32_t), &host_ptr), SUCCESS);
  EXPECT_EQ(runtime_->last_mem_access_desc_.location.id, static_cast<uint32_t>(kUserToDriverLogicIdOffset));
  EXPECT_EQ(UbMemAllocator::FreeMem(host_ptr), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, MallocMemConvertsDeviceIdWhenRuntimeVersionQueryFails) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  ScopedRuntimeVersionOverride restore_version;
  (void)restore_version;
  SetRuntimeVersionQueryResult(-1);
  void *host_ptr = nullptr;
  ASSERT_EQ(UbMemAllocator::MallocMem(MEM_HOST, sizeof(int32_t), &host_ptr), SUCCESS);
  EXPECT_EQ(runtime_->last_mem_access_desc_.location.id, static_cast<uint32_t>(kUserToDriverLogicIdOffset));
  EXPECT_EQ(UbMemAllocator::FreeMem(host_ptr), SUCCESS);
  VirtualMemoryManager::GetInstance().Finalize();
}
TEST_F(UbMemAllocatorUTest, MallocMemRollsBackWhenUserToDriverIdFails) {
  VirtualMemoryManager::GetInstance().Finalize();
  ASSERT_EQ(VirtualMemoryManager::GetInstance().Initialize(), SUCCESS);
  ScopedRuntimeVersionOverride restore_version;
  (void)restore_version;
  SetRuntimeVersionNum(90199999);
  ExpectMallocRollsBackOnAclFailure(MEM_HOST, "aclrtGetLogicDevIdByUserDevId", runtime_);
  VirtualMemoryManager::GetInstance().Finalize();
}
}  // namespace hixl
