/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "depends/mmpa/src/mmpa_stub.h"
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "depends/llm_datadist/src/hccn_conf_helper.h"
#include "hccl/hccl_mem_comm.h"
#include "acl/acl.h"
#include "hccl_stub.h"
#include "depends/llm_datadist/src/hccl_test_helper.h"
#include "common/llm_log.h"
#include "common/llm_checker.h"

CommMem hccl_mems[9];
HcclResult HcclBatchPut1(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                         aclrtStream stream) {
  LLMLOGI("remote_rank = %u, num_tasks = %u", remoteRank, descNum);
  for (uint32_t i = 0; i < descNum; ++i) {
    auto src = desc[i].localAddr;
    auto dst = desc[i].remoteAddr;
    auto size = desc[i].count;
    (void)memcpy_s(dst, size, src, size);
  }
  return HCCL_SUCCESS;
}
namespace llm {
namespace {
uintptr_t mock_handle = 0x8001;

HcclResult HcclBatchGet1(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                         aclrtStream stream) {
  LLMLOGI("remote_rank = %u, num_tasks = %u", remoteRank, descNum);
  for (uint32_t i = 0; i < descNum; ++i) {
    auto src = desc[i].localAddr;
    auto dst = desc[i].remoteAddr;
    auto size = desc[i].count;
    (void)memcpy_s(src, size, dst, size);
  }
  return HCCL_SUCCESS;
}

}  // namespace
class MockMmpa : public MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    return reinterpret_cast<void *>(mock_handle);
  }

  void *DlSym(void *handle, const char *func_name) override {
    auto func_map = GetBaseHcclFuncMap();
    func_map["HcclBatchPut"] = reinterpret_cast<void *>(&HcclBatchPut1);
    func_map["HcclBatchGet"] = reinterpret_cast<void *>(&HcclBatchGet1);
    return LookupFuncByName(func_map, func_name);
  }

  int32_t DlClose(void *handle) override {
    return 0;
  }

  int32_t RealPath(const CHAR *path, CHAR *realPath, INT32 realPathLen) override {
    std::string stub_path = path;
    if (stub_path == "/etc/hccn.conf") {
      stub_path = "/tmp/hccn.conf";
    }
    memcpy_s(realPath, realPathLen, stub_path.c_str(), stub_path.length());
    return 0;
  }
};

class RuntimeMock : public llm::AclRuntimeStub {
 public:
  aclError aclrtQueryEventStatus(aclrtEvent evt, aclrtEventRecordedStatus *status) {
    count++;
    if ((count % RUTIME_MOCK_QUERY_EVENT_INTERVAL) == 0) {
      *status = ACL_EVENT_RECORDED_STATUS_COMPLETE;
    } else {
      *status = ACL_EVENT_RECORDED_STATUS_NOT_READY;
    }
    return ACL_ERROR_NONE;
  }

  const char *aclrtGetSocName() override {
    return "Ascend910_9391";
  }

 private:
  int count;
};
class StartMock {
 public:
  StartMock() {
    MmpaStub::GetInstance().SetImpl(std::make_shared<MockMmpa>());
    llm::AclRuntimeStub::SetInstance(std::make_shared<RuntimeMock>());
    WriteHccnConfFile();
  }

  ~StartMock() {
    RemoveHccnConfFile();
  }
};
static StartMock start_mock;
}  // namespace llm
