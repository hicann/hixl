/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include <array>
#include <cstdint>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <vector>
#include "gtest/gtest.h"
#include "gmock/gmock.h"
#include "ascendcl_stub.h"
#include "depends/mmpa/src/mmpa_stub.h"
#include "load_kernel.h"
#define private public
#define protected public
#include "hixl_cs_client.h"
#include "complete_pool.h"
#undef protected
#undef private

namespace hixl {

class MockAclRuntimeStub : public llm::AclRuntimeStub {
 public:
  MOCK_METHOD(aclError, aclrtBinaryLoadFromFile, (const char*, aclrtBinaryLoadOptions*, aclrtBinHandle*), (override));
  MOCK_METHOD(aclError, aclrtBinaryGetFunction, (aclrtBinHandle, const char*, aclrtFuncHandle*), (override));
  MOCK_METHOD(aclError, aclrtWaitAndResetNotify, (aclrtNotify, aclrtStream, uint32_t), (override));
};

namespace {

constexpr uint32_t kUbDevId = 2U;
constexpr uint32_t kDummyPort = 12345U;

constexpr uint32_t kListNum1 = 1U;
constexpr uint64_t kLen8 = 8ULL;

constexpr const char *kTransFlagNameDevice = "_hixl_builtin_dev_trans_flag";

EndpointDesc MakeUbDeviceEp(CommProtocol protocol, uint32_t dev_id) {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;
  ep.protocol = protocol;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = dev_id;
  return ep;
}

void PrepareKernelReadyForUt(HixlCSClient &cli) {
  cli.ub_kernel_loaded_ = true;
  static uint8_t kNonNullStub = 0U;
  cli.ub_func_get_ = static_cast<void *>(&kNonNullStub);
  cli.ub_func_put_ = static_cast<void *>(&kNonNullStub);
}

void RecordMemForBatchTransfer(HixlCSClient &cli, void *remote_addr, size_t remote_size, void *local_addr,
                               size_t local_size) {
  (void)cli.mem_store_.RecordMemory(true, remote_addr, remote_size);
  (void)cli.mem_store_.RecordMemory(false, local_addr, local_size);
}

void FillTagMem(HixlCSClient &cli, const char *tag, void *addr, uint64_t size) {
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_DEVICE;
  mem.addr = addr;
  mem.size = size;
  cli.tag_mem_descs_[tag] = mem;
}

Status PollUntilCompleted(HixlCSClient &cli, void *qh, HixlCompleteStatus *out_status) {
  HIXL_CHECK_NOTNULL(out_status);
  *out_status = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;

  for (int i = 0; i < 10; ++i) {
    const Status ret = cli.CheckStatus(qh, out_status);
    if (ret != SUCCESS) {
      return ret;
    }
    if (*out_status == HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED) {
      return SUCCESS;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return SUCCESS;
}

}  // namespace

class MockMmpaStub : public llm::MmpaStubApiGe {
public:
  std::string fake_real_path_;
  bool real_path_ok_ = false;
  bool access_ok_ = false;
  INT32 RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    (void)path;
    if (!real_path_ok_) {
      return EN_ERROR;
    }
    if (fake_real_path_.empty() || realPathLen <= 0) {
      return EN_ERROR;
    }
    size_t destMax = static_cast<size_t>(realPathLen);
    errno_t ret = strncpy_s(realPath, destMax, fake_real_path_.c_str(), destMax - 1);
    if (ret != EOK) {
      return EN_ERROR;
    }
    return EN_OK;
  }
  INT32 Access(const CHAR *path_name) override {
    (void)path_name;
    return access_ok_ ? EN_OK : EN_ERROR;
  }
};

class HixlCSClientUbFixture : public ::testing::Test {
 protected:
  void SetUp() override {
    const EndpointDesc src = MakeUbDeviceEp(COMM_PROTOCOL_UBC_TP, kUbDevId);
    const EndpointDesc dst = MakeUbDeviceEp(COMM_PROTOCOL_UBC_TP, kUbDevId);

    HixlClientConfig config{};
    ASSERT_EQ(cli_.Create("127.0.0.1", kDummyPort, &src, &dst, &config), SUCCESS);

    cli_.client_channel_handle_ = static_cast<ChannelHandle>(1ULL);
    cli_.ub_remote_flag_inited_ = false;
    remote_flag_dev_ = 0ULL;
    FillTagMem(cli_, kTransFlagNameDevice, static_cast<void *>(&remote_flag_dev_), sizeof(uint64_t));

    PrepareKernelReadyForUt(cli_);
  }

  void TearDown() override {
    (void)cli_.Destroy();
    unsetenv("HIXL_UT_UB_FLAG_HACK");
  }

  CommunicateMem SetupBatchTransfer(bool is_get) {
    RecordMemForBatchTransfer(cli_, remote_buf_.data(), remote_buf_.size(),
                              local_buf_.data(), local_buf_.size());

    if (is_get) {
      remote_list_const_[0] = remote_buf_.data();
      local_list_[0]        = local_buf_.data();
      mem_.src_buf_list     = remote_list_const_;
      mem_.dst_buf_list     = local_list_;
    } else {
      local_list_const_[0]  = local_buf_.data();
      remote_list_[0]       = remote_buf_.data();
      mem_.src_buf_list     = local_list_const_;
      mem_.dst_buf_list     = remote_list_;
    }

    len_list_[0]  = kLen8;
    mem_.len_list = len_list_;
    mem_.list_num = kListNum1;

    return mem_;
  }

  HixlCSClient cli_{};
  uint64_t remote_flag_dev_{0ULL};

  std::array<uint8_t, 8> local_buf_{};
  std::array<uint8_t, 8> remote_buf_{};
  void* local_list_[1]{};
  void* remote_list_[1]{};
  const void* local_list_const_[1]{};
  const void* remote_list_const_[1]{};
  uint64_t len_list_[1]{};
  CommunicateMem mem_{};
};

TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);
  CommunicateMem mem = SetupBatchTransfer(false);

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(false, mem, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientUbFixture, BatchGetUbDeviceSuccessUseMemcpyHackFlag) {
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);
  CommunicateMem mem = SetupBatchTransfer(true);

  void *qh = nullptr;
  const Status ret = cli_.BatchTransfer(true, mem, &qh);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_NE(qh, nullptr);

  HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  (void)PollUntilCompleted(cli_, qh, &st);
  EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientUbFixture, PrepareUbRemoteFlagAndKernelMissingTagFail) {
  cli_.ub_remote_flag_inited_ = false;
  cli_.tag_mem_descs_.clear();

  void *remote_flag = nullptr;
  EXPECT_EQ(cli_.PrepareUbRemoteFlagAndKernel(remote_flag), PARAM_INVALID);
  EXPECT_EQ(remote_flag, nullptr);
}

TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceNotifyWaitFail) {
  CommunicateMem mem = SetupBatchTransfer(false);
  void *qh = nullptr;

  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);

  EXPECT_CALL(mock_acl, aclrtWaitAndResetNotify(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));

  const Status ret = cli_.BatchTransfer(false, mem, &qh);

  llm::AclRuntimeStub::UnInstall(&mock_acl);

  EXPECT_EQ(ret, FAILED);
  EXPECT_EQ(CompletePool::GetInstance().GetInUseCount(), 0U);
}

TEST_F(HixlCSClientUbFixture, BatchPutUbDeviceSlotExhaustedFail) {
  setenv("HIXL_UT_UB_FLAG_HACK", "1", 1);
  CommunicateMem mem = SetupBatchTransfer(false);
  std::vector<void *> handles;
  handles.reserve(CompletePool::kMaxSlots);

  for (uint32_t i = 0; i < CompletePool::kMaxSlots; ++i) {
    void *qh = nullptr;
    const Status ret = cli_.BatchTransfer(false, mem, &qh);
    ASSERT_EQ(ret, SUCCESS);
    ASSERT_NE(qh, nullptr);
    handles.emplace_back(qh);
  }

  void *qh_extra = nullptr;
  const Status ret_extra = cli_.BatchTransfer(false, mem, &qh_extra);
  EXPECT_NE(ret_extra, SUCCESS);
  EXPECT_EQ(qh_extra, nullptr);

  for (void *h : handles) {
    HixlCompleteStatus st = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
    (void)PollUntilCompleted(cli_, h, &st);
    EXPECT_EQ(st, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
  }
}

class LoadKernelFixture : public ::testing::Test {
protected:
  void SetUp() override {
    llm::MmpaStub::GetInstance().Reset();
    const char *env = std::getenv("ASCEND_HOME_PATH");
    if (env != nullptr) {
      original_env_ = env;
      has_env_ = true;
    }
    system("mkdir -p ./test_opp/opp/built-in/op_impl/aicpu/config");
  }
  void TearDown() override {
    if (has_env_) {
      setenv("ASCEND_HOME_PATH", original_env_.c_str(), 1);
    } else {
      unsetenv("ASCEND_HOME_PATH");
    }
    system("rm -rf ./test_opp");
    llm::MmpaStub::GetInstance().Reset();
  }
  void CreateDummyJson(const std::string &path, bool readable) {
    std::string cmd = "echo '{}' > " + path;
    system(cmd.c_str());
    if (!readable) {
      cmd = "chmod 000 " + path;
      system(cmd.c_str());
    }
  }
  std::string original_env_;
  bool has_env_ = false;
};

TEST_F(LoadKernelFixture, NoEnvAndFileNotFound) {
  auto mock_mmpa = std::make_shared<MockMmpaStub>();
  mock_mmpa->real_path_ok_ = false;
  mock_mmpa->access_ok_ = false;
  llm::MmpaStub::GetInstance().SetImpl(mock_mmpa);
  unsetenv("ASCEND_HOME_PATH");
  aclrtBinHandle bin_handle = nullptr;
  UbFuncHandles func_handles{};
  Status ret = LoadUbKernelAndGetHandles("GetFunc", "PutFunc", bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LoadKernelFixture, AclLoadBinaryFailed) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  std::string file_path = "./test_opp/opp/built-in/op_impl/aicpu/config/libcann_hixl_kernel.json";
  CreateDummyJson(file_path, true);
  aclrtBinHandle bin_handle = nullptr;
  UbFuncHandles func_handles{};
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtBinaryLoadFromFile(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));
  Status ret = LoadUbKernelAndGetHandles("GetFunc", "PutFunc", bin_handle, func_handles);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, FAILED);
}

TEST_F(LoadKernelFixture, GetFuncHandleInvalidParams) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  aclrtBinHandle dummy_bin_handle = reinterpret_cast<aclrtBinHandle>(0xDEADBEEF);
  UbFuncHandles func_handles{};
  Status ret = LoadUbKernelAndGetHandles(nullptr, "PutFunc", dummy_bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
  ret = LoadUbKernelAndGetHandles("GetFunc", nullptr, dummy_bin_handle, func_handles);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LoadKernelFixture, GetFuncHandleAclGetFuncFailed) {
  setenv("ASCEND_HOME_PATH", "./test_opp", 1);
  aclrtBinHandle dummy_bin_handle = reinterpret_cast<aclrtBinHandle>(0xDEADBEEF);
  UbFuncHandles func_handles{};
  MockAclRuntimeStub mock_acl;
  llm::AclRuntimeStub::Install(&mock_acl);
  EXPECT_CALL(mock_acl, aclrtBinaryGetFunction(testing::_, testing::_, testing::_))
      .WillOnce(testing::Return(static_cast<aclError>(FAILED)));
  Status ret = LoadUbKernelAndGetHandles("GetFunc", "PutFunc", dummy_bin_handle, func_handles);
  llm::AclRuntimeStub::UnInstall(&mock_acl);
  EXPECT_EQ(ret, FAILED);
}

}  // namespace hixl