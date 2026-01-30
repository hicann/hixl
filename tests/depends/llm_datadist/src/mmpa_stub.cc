/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "depends/mmpa/src/mmpa_stub.h"
#include <fstream>
#include <cstdio>  // for std::remove
#include <iostream>
#include "depends/ascendcl/src/ascendcl_stub.h"
#include "hccl/hccl_mem_comm.h"
#include "acl/acl.h"
#include "hccl_stub.h"
#include "common/llm_log.h"
#include "common/llm_checker.h"

HcclMem hccl_mems[9];
HcclResult HcclExchangeMemDesc1(HcclComm comm, uint32_t remoteRank, HcclMemDescs *local, int timeout,
                                HcclMemDescs *remote, uint32_t *actualNum) {
  for (uint32_t i = 0U; i < local->arrayLength; ++i) {
    strcpy(remote->array[i].desc, local->array[i].desc);
  }
  *actualNum = local->arrayLength;
  remote->arrayLength = local->arrayLength;
  return HcclResult::HCCL_SUCCESS;
}

HcclResult HcclBatchPut1(HcclComm comm, uint32_t remoteRank, HcclOneSideOpDesc *desc, uint32_t descNum,
                         aclrtStream stream) {
  LLMLOGI("remote_rank = %u, num_tasks = %u", remoteRank, descNum);
  for (uint32_t i = 0; i < descNum; ++i) {
    auto src = desc[i].localAddr;
    auto dst = desc[i].remoteAddr;
    auto size = desc[i].count;
    (void)memcpy(dst, src, size);
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
    (void) memcpy(src, dst, size);
  }
  return HCCL_SUCCESS;
}

void WriteHccnConfFile() {
  const std::string file_path = "/tmp/hccn.conf";
  std::ofstream file(file_path);
  if (!file.is_open()) {
    std::cout << "Failed to create file:" << file_path << std::endl;
    return;
  }

  file << "netmask_0=1.2.3.4\n"
        << "address_0=1.1.1.0\n"
        << "netmask_1=1.2.3.4\n"
        << "address_1=1.1.1.1\n"
        << "netmask_2=1.2.3.4\n"
        << "address_2=1.1.1.2\n"
        << "netmask_3=1.2.3.4\n"
        << "address_3=1.1.1.3\n"
        << "netmask_4=1.2.3.4\n"
        << "address_4=1.1.1.4\n"
        << "netmask_5=1.2.3.4\n"
        << "address_5=1.1.1.5\n"
        << "netmask_6=1.2.3.4\n"
        << "address_6=1.1.1.6\n"
        << "netmask_7=1.2.3.4\n"
        << "address_7=1.1.1.7\n";

  file.close();
}

void RemoveHccnConfFile() {
  const std::string file_path = "/tmp/hccn.conf";
  if (std::remove(file_path.c_str()) != 0) {
    std::cout << "Failed to delete file:" << file_path.c_str() << std::endl;
  }
}
}
class MockMmpa : public MmpaStubApiGe {
 public:
  void *DlOpen(const char *file_name, int32_t mode) override {
    return reinterpret_cast<void *>(mock_handle);
  }

  void *DlSym(void *handle, const char *func_name) override {
    static const std::map<std::string, void*> func_map = {
        {"HcclCommInitClusterInfoMemConfig", reinterpret_cast<void*>(&HcclCommInitClusterInfoMemConfig)},
        {"HcclExchangeMemDesc", reinterpret_cast<void*>(&HcclExchangeMemDesc1)},
        {"HcclCommDestroy", reinterpret_cast<void*>(&HcclCommDestroy)},
        {"HcclBatchPut", reinterpret_cast<void*>(&HcclBatchPut1)},
        {"HcclBatchGet", reinterpret_cast<void*>(&HcclBatchGet1)},
        {"HcclRemapRegistedMemory", reinterpret_cast<void*>(&HcclRemapRegistedMemory)},
        {"HcclRegisterGlobalMem", reinterpret_cast<void*>(&HcclRegisterGlobalMem)},
        {"HcclDeregisterGlobalMem", reinterpret_cast<void*>(&HcclDeregisterGlobalMem)},
        {"HcclCommBindMem", reinterpret_cast<void*>(&HcclCommBindMem)},
        {"HcclCommUnbindMem", reinterpret_cast<void*>(&HcclCommUnbindMem)},
        {"HcclCommPrepare", reinterpret_cast<void*>(&HcclCommPrepare)},
    };
    auto it = func_map.find(func_name);
    if (it != func_map.end()) {
      LLMLOGI("%s addr:%lu", func_name, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(it->second)));
      return it->second;
    }
    return nullptr;
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
  aclError aclrtQueryEventStatus(aclrtEvent evt, aclrtEventRecordedStatus *status)  {
    count++;
    if ((count % RUTIME_MOCK_QUERY_EVENT_INTERVAL) == 0) {
      *status = ACL_EVENT_RECORDED_STATUS_COMPLETE;
    } else {
      *status = ACL_EVENT_RECORDED_STATUS_NOT_READY;
    }
    return ACL_ERROR_NONE;
  }

  const char* aclrtGetSocName() override {
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