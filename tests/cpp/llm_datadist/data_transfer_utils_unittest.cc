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
#include <list>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "data_transfer/data_transfer_utils.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"
#include "llm_datadist/llm_engine_types.h"

namespace llm {
namespace {
class BatchPutStub : public HcclApiStub {
 public:
  explicit BatchPutStub(uint32_t fail_batch) : fail_batch_(fail_batch) {}

  HcclResult HcclBatchPut(HcclComm comm, uint32_t remote_rank, HcclOneSideOpDesc *desc, uint32_t desc_num,
                          aclrtStream stream) override {
    (void)comm;
    (void)remote_rank;
    (void)stream;
    batch_sizes_.push_back(desc_num);
    first_task_ids_.push_back(reinterpret_cast<uintptr_t>(desc[0].localAddr));
    return (fail_batch_ != 0U && batch_sizes_.size() == fail_batch_) ? HCCL_E_INTERNAL : HCCL_SUCCESS;
  }

  void DisableFailure() {
    fail_batch_ = 0U;
  }

  const std::vector<uint32_t> &BatchSizes() const {
    return batch_sizes_;
  }

  const std::vector<uintptr_t> &FirstTaskIds() const {
    return first_task_ids_;
  }

 private:
  uint32_t fail_batch_;
  std::vector<uint32_t> batch_sizes_;
  std::vector<uintptr_t> first_task_ids_;
};

std::list<HcclOneSideOpDesc> MakeTasks(size_t task_num) {
  std::list<HcclOneSideOpDesc> tasks;
  for (size_t i = 0U; i < task_num; ++i) {
    tasks.emplace_back(HcclOneSideOpDesc{reinterpret_cast<void *>(i + 1U), nullptr, 1U, HCCL_DATA_TYPE_INT8});
  }
  return tasks;
}
}  // namespace

class DataTransferUtilsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MockMmpaForHcclApi::Install();
    AclRuntimeStub::SetInstance(std::make_shared<AclRuntimeStub>());
    auto stub = std::make_unique<BatchPutStub>(0U);
    stub_ = stub.get();
    HcclApiStub::SetStub(std::move(stub));
    ASSERT_EQ(CommAdapter::GetInstance().Initialize(), ge::SUCCESS);

    entity_.SetEntityCommInfo(
        std::make_shared<EntityCommInfo>(reinterpret_cast<HcclComm>(1U), std::vector<void *>{}, 1, 1));
  }

  void TearDown() override {
    entity_.Finalize();
    CommAdapter::GetInstance().Finalize();
    HcclApiStub::ResetStub();
    AclRuntimeStub::Reset();
    MockMmpaForHcclApi::Reset();
  }

  BatchPutStub *stub_ = nullptr;
  CommEntity entity_{0U, 1U, 0U, 0U, 0U};
};

TEST_F(DataTransferUtilsTest, SendsAllTasksWhenEveryBatchSucceeds) {
  auto tasks = MakeTasks(128U);
  aclrtEvent event = nullptr;

  EXPECT_EQ(DataTransferUtils::SendCache(nullptr, entity_, tasks, event), ge::SUCCESS);
  EXPECT_TRUE(tasks.empty());
  ASSERT_NE(event, nullptr);
  EXPECT_EQ(stub_->BatchSizes(), (std::vector<uint32_t>{64U, 64U}));
  EXPECT_EQ(stub_->FirstTaskIds(), (std::vector<uintptr_t>{1U, 65U}));
  EXPECT_EQ(aclrtDestroyEvent(event), ACL_SUCCESS);
}

TEST_F(DataTransferUtilsTest, RetainsFailedBatchAndRetriesOnlyUnsubmittedTasks) {
  auto stub = std::make_unique<BatchPutStub>(2U);
  stub_ = stub.get();
  HcclApiStub::SetStub(std::move(stub));
  auto tasks = MakeTasks(128U);
  aclrtEvent event = nullptr;

  EXPECT_NE(DataTransferUtils::SendCache(nullptr, entity_, tasks, event), ge::SUCCESS);
  EXPECT_EQ(tasks.size(), 64U);
  EXPECT_EQ(stub_->BatchSizes(), (std::vector<uint32_t>{64U, 64U}));
  EXPECT_EQ(stub_->FirstTaskIds(), (std::vector<uintptr_t>{1U, 65U}));

  stub_->DisableFailure();
  EXPECT_EQ(DataTransferUtils::SendCache(nullptr, entity_, tasks, event), ge::SUCCESS);
  EXPECT_TRUE(tasks.empty());
  ASSERT_NE(event, nullptr);
  EXPECT_EQ(stub_->BatchSizes(), (std::vector<uint32_t>{64U, 64U, 64U}));
  EXPECT_EQ(stub_->FirstTaskIds(), (std::vector<uintptr_t>{1U, 65U, 65U}));
  EXPECT_EQ(aclrtDestroyEvent(event), ACL_SUCCESS);
}
}  // namespace llm
