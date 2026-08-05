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

#include "depends/ascend_hal/src/ascend_hal_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "gtest/gtest.h"
#include "proxy/ascend_hal_proxy.h"

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
    ++dl_open_calls;
    return nullptr;
  }

  uint32_t dl_open_calls = 0U;
};

class DlSymFailStub : public llm::MmpaStubApiGe {
 public:
  void *DlSym(void *handle, const char *func_name) override {
    (void)handle;
    (void)func_name;
    ++dl_sym_calls;
    return nullptr;
  }

  uint32_t dl_sym_calls = 0U;
};

}  // namespace

class AscendHalProxyUt : public ::testing::Test {
 protected:
  void SetUp() override {
    AscendHalProxy::ResetForTest();
    AscendHalStubReset();
  }

  void TearDown() override {
    AscendHalProxy::ResetForTest();
    AscendHalStubReset();
    llm::MmpaStub::GetInstance().Reset();
  }
};

TEST_F(AscendHalProxyUt, DlopenFailureIsNotCached) {
  uint8_t src = 0U;
  void *dst = nullptr;
  auto stub = std::make_shared<DlOpenFailStub>();
  ScopedMmpaStub guard(stub);
  EXPECT_EQ(AscendHalProxy::HostRegister(&src, sizeof(src), kHostMemMapDevPcieTh, 0U, &dst), FAILED);
  EXPECT_EQ(stub->dl_open_calls, 1U);
}

TEST_F(AscendHalProxyUt, DlsymFailureAfterSuccessfulLoad) {
  uint8_t src = 0U;
  void *dst = nullptr;
  ASSERT_EQ(AscendHalProxy::HostRegister(&src, sizeof(src), kHostMemMapDevPcieTh, 0U, &dst), SUCCESS);
  ASSERT_EQ(AscendHalProxy::HostUnregister(&src, 0U), SUCCESS);
  AscendHalProxy::ResetForTest();

  auto stub = std::make_shared<DlSymFailStub>();
  ScopedMmpaStub guard(stub);
  EXPECT_EQ(AscendHalProxy::HostRegister(&src, sizeof(src), kHostMemMapDevPcieTh, 0U, &dst), FAILED);
  EXPECT_EQ(stub->dl_sym_calls, 2U);
}

TEST_F(AscendHalProxyUt, SuccessfulLoadCanBeResetAndRetried) {
  uint8_t src = 0U;
  void *dst = nullptr;
  EXPECT_EQ(AscendHalProxy::HostRegister(&src, sizeof(src), kHostMemMapDevPcieTh, 0U, &dst), SUCCESS);
  EXPECT_NE(dst, nullptr);
  EXPECT_EQ(AscendHalProxy::HostUnregister(&src, 0U), SUCCESS);

  AscendHalProxy::ResetForTest();
  auto stub = std::make_shared<DlOpenFailStub>();
  ScopedMmpaStub guard(stub);
  EXPECT_EQ(AscendHalProxy::HostRegister(&src, sizeof(src), kHostMemMapDevPcieTh, 0U, &dst), FAILED);
  EXPECT_EQ(stub->dl_open_calls, 1U);
}

TEST_F(AscendHalProxyUt, HostRegisterErrorIsPropagated) {
  uint8_t src = 0U;
  void *dst = nullptr;
  AscendHalStubSetHostRegisterRet(-1);
  EXPECT_EQ(AscendHalProxy::HostRegister(&src, sizeof(src), kDevSvmMapHost, 0U, &dst), FAILED);
}

}  // namespace hixl
