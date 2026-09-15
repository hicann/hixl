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
#include "gtest/gtest.h"
#include "aclrt_proxy.h"
#include "ascendcl_stub.h"

namespace hixl {
namespace {

constexpr int32_t kUserPhyOffset = 100;

class ScopedAclStubInstall {
 public:
  explicit ScopedAclStubInstall(llm::AclRuntimeStub *stub) : stub_(stub) {
    llm::AclRuntimeStub::Install(stub_);
  }
  ~ScopedAclStubInstall() {
    llm::AclRuntimeStub::UnInstall(stub_);
  }

  ScopedAclStubInstall(const ScopedAclStubInstall &) = delete;
  ScopedAclStubInstall &operator=(const ScopedAclStubInstall &) = delete;

 private:
  llm::AclRuntimeStub *stub_;
};

class DistinctPhyAclStub : public llm::AclRuntimeStub {
 public:
  aclError aclrtGetPhyDevIdByUserDevId(const int32_t userDevId, int32_t *const phyDevId) override {
    ++user_calls;
    if (user_ret_ != ACL_SUCCESS) {
      return user_ret_;
    }
    if (phyDevId == nullptr) {
      return ACL_ERROR_INVALID_PARAM;
    }
    *phyDevId = userDevId + kUserPhyOffset;
    return ACL_SUCCESS;
  }

  aclError aclrtGetPhyDevIdByLogicDevId(const int32_t logicDevId, int32_t *const phyDevId) override {
    (void)logicDevId;
    (void)phyDevId;
    ++logic_calls;
    return ACL_ERROR_FAILURE;
  }

  void set_user_ret(aclError ret) {
    user_ret_ = ret;
  }

  uint32_t user_calls = 0U;
  uint32_t logic_calls = 0U;

 private:
  aclError user_ret_ = ACL_SUCCESS;
};

}  // namespace

TEST(AclrtProxyUt, UsesUserDevIdWhenSymbolExists) {
  DistinctPhyAclStub stub;
  ScopedAclStubInstall install(&stub);
  int32_t phy_id = -1;
  EXPECT_EQ(AclrtProxy::GetPhyDevIdByUserDevId(3, &phy_id), ACL_SUCCESS);
  EXPECT_EQ(phy_id, 3 + kUserPhyOffset);
  EXPECT_EQ(stub.user_calls, 1U);
  EXPECT_EQ(stub.logic_calls, 0U);
}

TEST(AclrtProxyUt, PropagatesUserApiError) {
  DistinctPhyAclStub stub;
  stub.set_user_ret(ACL_ERROR_FAILURE);
  ScopedAclStubInstall install(&stub);
  int32_t phy_id = -1;
  EXPECT_EQ(AclrtProxy::GetPhyDevIdByUserDevId(1, &phy_id), ACL_ERROR_FAILURE);
  EXPECT_EQ(stub.user_calls, 1U);
  EXPECT_EQ(stub.logic_calls, 0U);
}

}  // namespace hixl
