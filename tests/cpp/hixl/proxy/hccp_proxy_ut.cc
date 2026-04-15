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
#include "ascendcl_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "hccp_proxy.h"

namespace hixl {

namespace {

// mmDlsym stub rejects handles < 0x8000; align with tests/cpp/llm_datadist/llm_comm_link_manager_unittest.cc.
// Used only for paths that mmDlclose while the mock is still installed (DlSymFail); must be >= 0x8000 for DlSym.
constexpr uintptr_t kMockLibRaHandle = 0x8001ULL;

// Must match mock base VA returned by ut_mock_ra_get_notify_base_addr below.
constexpr uint64_t kUtMockNotifyBaseVa = 0x1000ULL;

extern "C" int ut_mock_ra_rdev_get_handle(unsigned int phy_id, void **out_handle) {
  (void)phy_id;
  static int kDummyRdma = 0;
  if (out_handle != nullptr) {
    *out_handle = &kDummyRdma;
  }
  return 0;
}

extern "C" int ut_mock_ra_get_notify_base_addr(void *handle, unsigned long long *va, unsigned long long *size) {
  (void)handle;
  if (va != nullptr) {
    *va = kUtMockNotifyBaseVa;
  }
  if (size != nullptr) {
    *size = 0x10000ULL;
  }
  return 0;
}

class ScopedAclStubInstall {
 public:
  explicit ScopedAclStubInstall(llm::AclRuntimeStub *stub) : stub_(stub) { llm::AclRuntimeStub::Install(stub_); }
  ~ScopedAclStubInstall() { llm::AclRuntimeStub::UnInstall(stub_); }

  ScopedAclStubInstall(const ScopedAclStubInstall &) = delete;
  ScopedAclStubInstall &operator=(const ScopedAclStubInstall &) = delete;

 private:
  llm::AclRuntimeStub *stub_;
};

class ScopedMmpaRaMock {
 public:
  explicit ScopedMmpaRaMock(const std::shared_ptr<llm::MmpaStubApiGe> &impl) {
    llm::MmpaStub::GetInstance().SetImpl(impl);
  }
  ~ScopedMmpaRaMock() { llm::MmpaStub::GetInstance().Reset(); }

  ScopedMmpaRaMock(const ScopedMmpaRaMock &) = delete;
  ScopedMmpaRaMock &operator=(const ScopedMmpaRaMock &) = delete;
};

class NegativePhyDevAclStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) override {
    (void)logicDevId;
    *phyDevId = -1;
    return ACL_ERROR_NONE;
  }
};

class OkPhyDevAclStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) override {
    (void)logicDevId;
    *phyDevId = 0;
    return ACL_ERROR_NONE;
  }
};

class MockMmpaRaOk : public llm::MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    if (file_name != nullptr && std::strcmp(file_name, "libra.so") == 0) {
      // Return a real dlopen handle so ~LibRaLoader's mmDlclose remains valid after MmpaStub::Reset() at test exit.
      return llm::MmpaStubApiGe::DlOpen("libdl.so.2", mode);
    }
    return nullptr;
  }

  void *DlSym(void *handle, const char *func_name) override {
    if (reinterpret_cast<uintptr_t>(handle) < 0x8000) {
      return nullptr;
    }
    if (func_name != nullptr && std::strcmp(func_name, "RaRdevGetHandle") == 0) {
      return reinterpret_cast<void *>(&ut_mock_ra_rdev_get_handle);
    }
    if (func_name != nullptr && std::strcmp(func_name, "RaGetNotifyBaseAddr") == 0) {
      return reinterpret_cast<void *>(&ut_mock_ra_get_notify_base_addr);
    }
    return nullptr;
  }

  int32_t DlClose(void *handle) override {
    (void)handle;
    return 0;
  }
};

// Shared DlSym/DlClose for failure-path mocks (dlopen fail vs dlsym fail).
class MockMmpaRaDlSymAlwaysFailBase : public llm::MmpaStubApiGe {
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

class MockMmpaRaDlOpenFail : public MockMmpaRaDlSymAlwaysFailBase {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    (void)file_name;
    (void)mode;
    return nullptr;
  }
};

class MockMmpaRaDlSymFail : public MockMmpaRaDlSymAlwaysFailBase {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    (void)mode;
    if (file_name != nullptr && std::strcmp(file_name, "libra.so") == 0) {
      return reinterpret_cast<void *>(kMockLibRaHandle);
    }
    return nullptr;
  }
};

}  // namespace

TEST(HccpProxyUt, RejectsNegativeLogicDeviceId) {
  uint64_t notify_addr = 0ULL;
  uint32_t notify_len = 0U;
  EXPECT_EQ(HccpProxy::RaGetNotifyAddrLen(-1, nullptr, notify_addr, notify_len), PARAM_INVALID);
}

TEST(HccpProxyUt, RejectsNegativePhysicalDeviceId) {
  NegativePhyDevAclStub acl_stub;
  ScopedAclStubInstall acl_guard(&acl_stub);
  uint64_t notify_addr = 0ULL;
  uint32_t notify_len = 0U;
  EXPECT_EQ(HccpProxy::RaGetNotifyAddrLen(0, nullptr, notify_addr, notify_len), PARAM_INVALID);
}

// Single test keeps scenarios in order: HccpProxy uses a static LibRaLoader; after a successful mmDlopen
// the library handle is cached, so mmDlopen/mmDlsym failure paths must run before the success path here.
TEST(HccpProxyUt, MmpaMmDlopenAndDlsymStubScenarios) {
  OkPhyDevAclStub acl_stub;
  ScopedAclStubInstall acl_guard(&acl_stub);
  aclrtNotify notify = reinterpret_cast<aclrtNotify>(0x1ULL);
  uint64_t notify_addr = 0ULL;
  uint32_t notify_len = 0U;

  {
    auto mock_mmpa = std::make_shared<MockMmpaRaDlOpenFail>();
    ScopedMmpaRaMock mmpa_guard(mock_mmpa);
    EXPECT_EQ(HccpProxy::RaGetNotifyAddrLen(0, notify, notify_addr, notify_len), FAILED);
  }
  {
    auto mock_mmpa = std::make_shared<MockMmpaRaDlSymFail>();
    ScopedMmpaRaMock mmpa_guard(mock_mmpa);
    EXPECT_EQ(HccpProxy::RaGetNotifyAddrLen(0, notify, notify_addr, notify_len), FAILED);
  }
  {
    auto mock_mmpa = std::make_shared<MockMmpaRaOk>();
    ScopedMmpaRaMock mmpa_guard(mock_mmpa);
    EXPECT_EQ(HccpProxy::RaGetNotifyAddrLen(0, notify, notify_addr, notify_len), SUCCESS);
    EXPECT_EQ(notify_len, 4U);
    EXPECT_EQ(notify_addr, kUtMockNotifyBaseVa);
  }
}

}  // namespace hixl
