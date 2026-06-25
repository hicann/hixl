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
#include <memory>
#include "gtest/gtest.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/dsmi/src/dsmi_stub.h"
#include "proxy/dsmi_proxy.h"

namespace hixl {

namespace {

class ScopedMmpaStub {
 public:
  explicit ScopedMmpaStub(const std::shared_ptr<llm::MmpaStubApiGe> &impl) {
    llm::MmpaStub::GetInstance().SetImpl(impl);
  }
  ~ScopedMmpaStub() {
    llm::MmpaStub::GetInstance().Reset();
  }
  ScopedMmpaStub(const ScopedMmpaStub &) = delete;
  ScopedMmpaStub &operator=(const ScopedMmpaStub &) = delete;
};

class DlOpenFailStub : public llm::MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    (void)file_name;
    (void)mode;
    return nullptr;
  }
};

class DlSymFailStub : public llm::MmpaStubApiGe {
 public:
  void *DlSym(void *handle, const char *func_name) override {
    (void)handle;
    (void)func_name;
    return nullptr;
  }
};

}  // namespace

// Failure scenarios must run before success scenario because EnsureLibDrvdsmiHostLoaded caches handle on success.
TEST(DsmiProxyUt, LoadFailureThenSuccess) {
  int32_t device_id = 0;
  uint32_t slot_id = 0;

  // Scenario 1: dlopen fail — no caching
  {
    auto stub = std::make_shared<DlOpenFailStub>();
    ScopedMmpaStub guard(stub);
    EXPECT_EQ(DsmiProxy::GetDevSlotId(device_id, slot_id), FAILED);
  }

  // Scenario 2: dlsym fail — no caching
  {
    auto stub = std::make_shared<DlSymFailStub>();
    ScopedMmpaStub guard(stub);
    EXPECT_EQ(DsmiProxy::GetDevSlotId(device_id, slot_id), FAILED);
  }

  // Scenario 3: success via DSMI stub — caches stub function pointers
  // Default MmpaStubApiGe::DlOpen returns dlopen(nullptr) for libdrvdsmi_host.so,
  // dlsym finds the stub's symbols in global scope (loaded via DT_NEEDED).
  {
    DsmiStubSetSlotId(3, 0);
    EXPECT_EQ(DsmiProxy::GetDevSlotId(device_id, slot_id), SUCCESS);
    EXPECT_EQ(slot_id, 3U);

    DsmiStubSetInterconType(4U);
    uint32_t intercon_type = 0;
    EXPECT_EQ(DsmiProxy::GetInterconType(device_id, intercon_type), SUCCESS);
    EXPECT_EQ(intercon_type, 4U);

    DsmiStubSetDeviceInfoRet(-1);
    EXPECT_NE(DsmiProxy::GetInterconType(device_id, intercon_type), SUCCESS);
    DsmiStubSetDeviceInfoRet(0);

    DsmiStubSetSlotId(0, -1);
    EXPECT_NE(DsmiProxy::GetDevSlotId(device_id, slot_id), SUCCESS);
    DsmiStubSetSlotId(0, 0);
  }
}

TEST(DsmiProxyUt, IsInterconTypeSupportedAfterLoad) {
  // After LoadFailureThenSuccess, singleton has cached stub function pointers.
  // IsInterconTypeSupported should return true (dsmi_get_device_info != nullptr).
  EXPECT_TRUE(DsmiProxy::IsInterconTypeSupported());
}

}  // namespace hixl
