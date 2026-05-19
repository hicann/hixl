/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>
#include <cstring>
#include <memory>
#include "gtest/gtest.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "proxy/dsmi_proxy.h"

namespace hixl {

namespace {

constexpr uintptr_t kMockLibDrvdsmiHandle = 0x8001ULL;
constexpr uint32_t kUtMockSlotId = 3U;

struct dsmi_board_info_stru {
  unsigned int board_id;
  unsigned int pcb_id;
  unsigned int bom_id;
  unsigned int slot_id;
};

bool g_mock_success = true;
extern "C" int ut_mock_dsmi_get_board_info(int device_id, struct dsmi_board_info_stru *pboard_info) {
  (void)device_id;
  if (!g_mock_success) {
    return -1;
  }
  if (pboard_info != nullptr) {
    pboard_info->board_id = 0x1000;
    pboard_info->pcb_id = 0x2000;
    pboard_info->bom_id = 0x3000;
    pboard_info->slot_id = kUtMockSlotId;
  }
  return 0;
}

class ScopedMmpaDsmiMock {
 public:
  explicit ScopedMmpaDsmiMock(const std::shared_ptr<llm::MmpaStubApiGe> &impl) {
    llm::MmpaStub::GetInstance().SetImpl(impl);
  }
  ~ScopedMmpaDsmiMock() {
    llm::MmpaStub::GetInstance().Reset();
  }

  ScopedMmpaDsmiMock(const ScopedMmpaDsmiMock &) = delete;
  ScopedMmpaDsmiMock &operator=(const ScopedMmpaDsmiMock &) = delete;
};

class MockMmpaDsmi : public llm::MmpaStubApiGe {
 public:
  MockMmpaDsmi(void *mock_dsmi_get_board_info_func) : mock_dsmi_get_board_info_func_(mock_dsmi_get_board_info_func) {}

  void *DlOpen(const char *file_name, int32_t mode) override {
    if (file_name != nullptr && std::strcmp(file_name, "libdrvdsmi_host.so") == 0) {
      // Return a real dlopen handle so ~LibRaLoader's mmDlclose remains valid after MmpaStub::Reset() at test exit.
      handle_stub_ = llm::MmpaStubApiGe::DlOpen("libdl.so.2", mode);
      return handle_stub_;
    }
    return llm::MmpaStubApiGe::DlOpen(file_name, mode);
  }

  void *DlSym(void *handle, const char *func_name) override {
    if (func_name == nullptr) {
      return nullptr;
    }
    if (handle == handle_stub_) {
      if (std::strcmp(func_name, "dsmi_get_board_info") == 0) {
        return mock_dsmi_get_board_info_func_;
      }
      return nullptr;
    }
    return llm::MmpaStubApiGe::DlSym(handle, func_name);
  }

  int32_t DlClose(void *handle) override {
    return llm::MmpaStubApiGe::DlClose(handle);
  }

 protected:
  void *mock_dsmi_get_board_info_func_ = nullptr;

 private:
  void *handle_stub_ = nullptr;
};

class MockMmpaDsmiDlSymAlwaysFailBase : public llm::MmpaStubApiGe {
 public:
  void *DlSym(void *handle, const char *func_name) override {
    (void)handle;
    (void)func_name;
    return nullptr;
  }

  int32_t DlClose(void *handle) override {
    (void)handle;
    return 0;
  }
};

class MockMmpaDsmiDlOpenFail : public MockMmpaDsmiDlSymAlwaysFailBase {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    (void)file_name;
    (void)mode;
    return nullptr;
  }
};

class MockMmpaDsmiDlSymFail : public MockMmpaDsmiDlSymAlwaysFailBase {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    (void)mode;
    if (file_name != nullptr && std::strcmp(file_name, "libdrvdsmi_host.so") == 0) {
      return reinterpret_cast<void *>(kMockLibDrvdsmiHandle);
    }
    return nullptr;
  }
};

}  // namespace

// Single test keeps scenarios in order: DsmiProxy uses a static LibDrvdsmiHostLoader; after a successful mmDlopen
// the library handle is cached, so mmDlopen/mmDlsym/dsmi_get_board_info failure paths must run before the success
// path here.
TEST(DsmiProxyUt, AllScenariosInOrder) {
  int32_t device_id = 0;
  uint32_t slot_id = 0;

  // Scenario 1: mmDlopen fail
  {
    auto mock_mmpa = std::make_shared<MockMmpaDsmiDlOpenFail>();
    ScopedMmpaDsmiMock mmpa_guard(mock_mmpa);
    EXPECT_EQ(DsmiProxy::GetDevSlotId(device_id, slot_id), FAILED);
  }

  // Scenario 2: mmDlsym fail
  {
    auto mock_mmpa = std::make_shared<MockMmpaDsmiDlSymFail>();
    ScopedMmpaDsmiMock mmpa_guard(mock_mmpa);
    EXPECT_EQ(DsmiProxy::GetDevSlotId(device_id, slot_id), FAILED);
  }

  // Scenario 3: dlopen success mock func failed and success
  {
    auto mock_mmpa = std::make_shared<MockMmpaDsmi>(reinterpret_cast<void *>(&ut_mock_dsmi_get_board_info));
    ScopedMmpaDsmiMock mmpa_guard(mock_mmpa);
    g_mock_success = true;
    EXPECT_EQ(DsmiProxy::GetDevSlotId(device_id, slot_id), SUCCESS);
    EXPECT_EQ(slot_id, kUtMockSlotId);
    g_mock_success = false;
    EXPECT_NE(DsmiProxy::GetDevSlotId(device_id, slot_id), SUCCESS);
    // reset to mock success
    g_mock_success = true;
  }
}

}  // namespace hixl