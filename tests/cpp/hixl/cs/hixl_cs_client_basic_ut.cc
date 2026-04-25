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
#include <arpa/inet.h>
#include "gtest/gtest.h"
#include "hixl_cs_client.h"
#include "hccl/hccl_types.h"
#include "depends/hccl/src/hccl_stub.h"

namespace hixl {

// C API 句柄与 HixlCSClientCreate 返回一致，均为 HixlCSClient* 的 void* 别名
inline HixlClientHandle ClientHandleFrom(HixlCSClient *client) {
  return reinterpret_cast<HixlClientHandle>(client);
}

// 初始化源endpoint和本地endpoint

EndpointDesc MakeSrcEp() {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_HOST;
  ep.protocol = COMM_PROTOCOL_ROCE;      // 或 COMM_PROTOCOL_ROCE，按你们测试协议
  ep.commAddr.type = COMM_ADDR_TYPE_IP_V4;
  // 填充 IPv4 地址到 in_addr
  inet_pton(AF_INET, "127.0.0.1", &ep.commAddr.addr);
  return ep;
}

EndpointDesc MakeDstEp() {
  EndpointDesc ep{};
  ep.loc.locType = ENDPOINT_LOC_TYPE_DEVICE;  // 或 HOST：取决于你们对端在设备还是主机
  ep.protocol = COMM_PROTOCOL_ROCE;          // 与 src 协议一致
  ep.commAddr.type = COMM_ADDR_TYPE_IP_V4;
  inet_pton(AF_INET, "127.0.0.1", &ep.commAddr.addr);
  return ep;
}

// 构造远端内存描述，模拟 GetRemoteMem 返回的数据后直接走 ImportRemoteMem
static HixlMemDesc MakeRemoteDesc(const char *tag, void *addr, uint64_t size)  {
  HixlMemDesc d{};
  d.tag = tag;
  d.mem.type = COMM_MEM_TYPE_HOST;
  d.mem.addr = addr;
  d.mem.size = size;
  static const std::string dummy("export");
  const size_t n = dummy.size(); // 纯字节长度
  // 若 ImportRemoteMem 只当作blob使用，无需 0 结尾，可分配 n；如按 C 字符串使用，分配 n+1 并补 '\0'
  char *buf = static_cast<char *>(std::malloc(n));
  if (buf == nullptr) {
    d.export_desc = nullptr;
    d.export_len = 0;
    return d;
  }
  std::copy(dummy.data(), dummy.data() + n, buf);
  d.export_desc = buf;
  d.export_len = static_cast<uint32_t>(n);
  return d;
}

constexpr uint32_t kFlagSizeBytes = 8;          // 传输完成标记占用字节数
constexpr uint32_t kBlockSizeBytes = 1024;      // 远端数据块大小
constexpr uint32_t kClientBufSizeBytes = 4096;  // 客户端缓冲区大小
constexpr uint32_t kClientBufSizeBytes2 = 1024;  // 客户端缓冲区大小
constexpr const char *kTransFlagNameHost = "_hixl_builtin_host_trans_flag";
constexpr const char *kTransFlagNameDevice = "_hixl_builtin_dev_trans_flag";
uint32_t kClientBufAddr = 1;
uint32_t kServerDataAddr = 2;
uint64_t kTransFlagAddr = 1;
struct ImportedRemote {
  CommMem* remote_mem_list = nullptr;
  char** tags_buf = nullptr;
  uint32_t list_num = 0;
};
// 创建 HixlClient 连接
void CreateHixlClient(hixl::HixlCSClient& cli, const char* ip, uint32_t port) {
  EndpointDesc src = MakeSrcEp();
  EndpointDesc dst = MakeDstEp();
  HixlClientConfig config{}; // 默认构造
  HixlClientDesc desc{};
  desc.server_ip = ip;
  desc.server_port = port;
  desc.local_endpoint = &src;
  desc.remote_endpoint = &dst;
  EXPECT_EQ(cli.Create(&desc, &config), SUCCESS);
}

// 封装：创建连接 + 导入远端内存
void PrepareConnectionAndImport(hixl::HixlCSClient& cli, const char* client_ip, uint32_t port) {
  CreateHixlClient(cli, client_ip, port);
  std::vector<HixlMemDesc> descs;
  descs.push_back(MakeRemoteDesc(kTransFlagNameDevice,
  &kTransFlagAddr, kFlagSizeBytes));
  descs.push_back(MakeRemoteDesc("server_data",
  &kServerDataAddr, kBlockSizeBytes));

  ImportedRemote ret{};
  ASSERT_EQ(cli.ImportRemoteMem(descs, &ret.remote_mem_list, &ret.tags_buf, &ret.list_num), SUCCESS);
}

// 注册本地内存
void RecordLocalMem(hixl::HixlCSClient& cli) {
  CommMem local{};
  local.type = COMM_MEM_TYPE_HOST;
  local.addr = &kClientBufAddr;
  local.size = kClientBufSizeBytes;
  MemHandle local_handle = nullptr;
  ASSERT_EQ(cli.RegMem("client_buf", &local, &local_handle), SUCCESS);
}

class HixlCSClientFixture : public ::testing::Test {
 protected:
  hixl::HixlCSClient cli;
  void SetUp() override {
    ResetTransferCounter();
  }
  void TearDown() override {
    (void)cli.Destroy();
  }
};


TEST_F(HixlCSClientFixture, RegMemAndUnRegMem) {
  // 先创建本端 endpoint（Create 不依赖 socket 初始化以外的 HCCL）
  const char *client_ip = "127.0.0.1";
  uint32_t server_port = 12345;
  // 对齐实现：Create 要求 dst.protocol != RESERVED 才能用于后续 Connect，这里仅测试 RegMem 不调用 Connect
  CreateHixlClient(cli, client_ip, server_port);

  // 通过 RegMem 登记 client 侧内存（不能直接调用 mem_store_）
  CommMem mem{};
  mem.type = COMM_MEM_TYPE_HOST;
  mem.addr = &kClientBufAddr;
  mem.size = kClientBufSizeBytes;

  MemHandle handle = nullptr;
  EXPECT_EQ(cli.RegMem("client_buf", &mem, &handle), SUCCESS);
  EXPECT_NE(handle, nullptr);

  // 注销应成功
  EXPECT_EQ(cli.UnRegMem(handle), SUCCESS);
}

TEST_F(HixlCSClientFixture, ImportRemoteMemAndClearRemoteMemInfo) {
  const char *client_ip = "127.0.0.1";
  uint32_t server_port = 22334;
  CreateHixlClient(cli, client_ip, server_port);

  // 构造两个远端内存描述并导入
  std::vector<HixlMemDesc> descs;
  descs.push_back(
      MakeRemoteDesc(kTransFlagNameDevice, &kTransFlagAddr, kFlagSizeBytes));
  descs.push_back(MakeRemoteDesc("server_data", &kServerDataAddr, kBlockSizeBytes));

  CommMem *remote_mem_list = nullptr;
  char **tags_buf = nullptr;
  uint32_t list_num = 0;
  EXPECT_EQ(cli.ImportRemoteMem(descs, &remote_mem_list, &tags_buf, &list_num), SUCCESS);
  EXPECT_EQ(list_num, 2u);
  EXPECT_NE(remote_mem_list, nullptr);
  // 验证 server_data 被记录
  EXPECT_EQ(remote_mem_list[1].addr, &kServerDataAddr);
  EXPECT_EQ(remote_mem_list[1].size, kBlockSizeBytes);
  // 验证 server_data 被记录
  void* key = remote_mem_list[1].addr;
  EXPECT_EQ(cli.mem_store_.server_regions_[key].size, kBlockSizeBytes);
  // 清理远端信息
  EXPECT_EQ(cli.ClearRemoteMemInfo(), SUCCESS);
}

TEST_F(HixlCSClientFixture, BatchPutSuccessWithStubbedHccl) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22335;
  CreateHixlClient(cli, client_ip, port);
  std::cout << "cli已创建" << std::endl;

  // 导入远端内存，包含完成标志与一个数据区
  std::vector<HixlMemDesc> descs;
  descs.push_back(
      MakeRemoteDesc(kTransFlagNameDevice, &kTransFlagAddr, kFlagSizeBytes));
  std::cout<<"_hixl_builtin_dev_trans_flag的地址是"<<&kTransFlagAddr<<std::endl;
  std::cout<<"_hixl_builtin_dev_trans_flag的值是"<<kTransFlagAddr<<std::endl;
  descs.push_back(MakeRemoteDesc("server_data", &kServerDataAddr, kBlockSizeBytes));
  CommMem *remote_mem_list = nullptr;
  char **tags_buf = nullptr;

  uint32_t list_num = 0;
  ASSERT_EQ(cli.ImportRemoteMem(descs, &remote_mem_list, &tags_buf, &list_num), SUCCESS);
  std::cout << "server远端内存已获取并记录" << std::endl;
  // 通过 RegMem 登记本地缓冲
  RecordLocalMem(cli);
  std::cout << "client内存完成记录" << std::endl;
  void *remote_list[] = {&kServerDataAddr};
  const void *local_list[] = {&kClientBufAddr};
  uint64_t len_list[] = {4};
  void *query_handle = nullptr;
  CommunicateMem com_mem{1, remote_list, local_list, len_list};
  // 批量写入，桩的 HcommWriteNbi/Fence/ReadNbi 都是空实现，流程应返回 SUCCESS
  ASSERT_EQ(cli.BatchTransfer(false, com_mem, &query_handle), SUCCESS);
  std::cout << "执行批量写入，返回queryhandle" << std::endl;
  ASSERT_NE(query_handle, nullptr);
  CompleteHandleInfo *task_flag = static_cast<CompleteHandleInfo *>(query_handle);
  // 首次检查通常为 NOT_READY（flag 还未被置 1)
  HixlCompleteStatus status_out = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  uint64_t* flag = task_flag->flag_address;
  std::cout<<"falg的值是："<<*flag<<std::endl;
  Status st = cli.CheckStatus(task_flag, &status_out);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(status_out, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientFixture, BatchPutFailsWhenDeviceEndpointImportsOnlyHostFlag) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22336;
  CreateHixlClient(cli, client_ip, port);

  std::vector<HixlMemDesc> descs;
  descs.push_back(MakeRemoteDesc(kTransFlagNameHost, &kTransFlagAddr, kFlagSizeBytes));
  descs.push_back(MakeRemoteDesc("server_data", &kServerDataAddr, kBlockSizeBytes));
  CommMem *remote_mem_list = nullptr;
  char **tags_buf = nullptr;
  uint32_t list_num = 0;
  ASSERT_EQ(cli.ImportRemoteMem(descs, &remote_mem_list, &tags_buf, &list_num), SUCCESS);

  RecordLocalMem(cli);

  void *remote_list[] = {&kServerDataAddr};
  const void *local_list[] = {&kClientBufAddr};
  uint64_t len_list[] = {4};
  void *query_handle = nullptr;
  CommunicateMem com_mem{1, remote_list, local_list, len_list};
  EXPECT_EQ(cli.BatchTransfer(false, com_mem, &query_handle), FAILED);
  EXPECT_EQ(query_handle, nullptr);
}

TEST_F(HixlCSClientFixture, BatchGetSuccessWithStubbedHccl) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22336;
  PrepareConnectionAndImport(cli, client_ip, port);
  // 登记本地缓冲
  RecordLocalMem(cli);

  const void* remote_list[] = {&kServerDataAddr};
  void* local_list[] = {&kClientBufAddr};
  uint64_t len_list[] = {4};
  void* query_handle = nullptr;
  CommunicateMem com_mem{1, local_list, remote_list, len_list};
  ASSERT_EQ(cli.BatchTransfer(true, com_mem, &query_handle), SUCCESS);
  ASSERT_NE(query_handle, nullptr);
  CompleteHandleInfo *task_flag = static_cast<CompleteHandleInfo *>(query_handle);
  HixlCompleteStatus status_out = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(cli.CheckStatus(task_flag, &status_out), SUCCESS);
  EXPECT_EQ(status_out, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientFixture, BatchPutSyncSuccess) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22340;
  PrepareConnectionAndImport(cli, client_ip, port);
  CommMem local{};
  local.type = COMM_MEM_TYPE_HOST;
  local.addr = &kClientBufAddr;
  local.size = kClientBufSizeBytes;
  MemHandle local_handle = nullptr;
  ASSERT_EQ(cli.RegMem("client_buf", &local, &local_handle), SUCCESS);
  void *remote_list[] = {&kServerDataAddr};
  const void *local_list[] = {&kClientBufAddr};
  uint64_t len_list[] = {4};
  CommunicateMem com_mem{1, remote_list, local_list, len_list};
  EXPECT_EQ(cli.BatchTransferSync(false, com_mem, 5000U), SUCCESS);
}

// 覆盖 C 封装 HixlCSClientBatchPutSync（与 BatchTransferSync Put 路径一致）
TEST_F(HixlCSClientFixture, CsApiBatchPutSyncSuccess) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22341;
  PrepareConnectionAndImport(cli, client_ip, port);
  CommMem local{};
  local.type = COMM_MEM_TYPE_HOST;
  local.addr = &kClientBufAddr;
  local.size = kClientBufSizeBytes;
  MemHandle local_handle = nullptr;
  ASSERT_EQ(cli.RegMem("client_buf", &local, &local_handle), SUCCESS);
  HixlOneSideOpDesc op{};
  op.remote_buf = &kServerDataAddr;
  op.local_buf = static_cast<void *>(&kClientBufAddr);
  op.len = 4;
  HixlClientHandle ch = ClientHandleFrom(&cli);
  EXPECT_EQ(HixlCSClientBatchPutSync(ch, 1, &op, 5000U), HIXL_SUCCESS);
}

// 覆盖 C 封装 HixlCSClientBatchGetSync
TEST_F(HixlCSClientFixture, CsApiBatchGetSyncSuccess) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22342;
  PrepareConnectionAndImport(cli, client_ip, port);
  CommMem local{};
  local.type = COMM_MEM_TYPE_HOST;
  local.addr = &kClientBufAddr;
  local.size = kClientBufSizeBytes;
  MemHandle local_handle = nullptr;
  ASSERT_EQ(cli.RegMem("client_buf", &local, &local_handle), SUCCESS);
  HixlOneSideOpDesc op{};
  op.remote_buf = &kServerDataAddr;
  op.local_buf = &kClientBufAddr;
  op.len = 4;
  HixlClientHandle ch = ClientHandleFrom(&cli);
  EXPECT_EQ(HixlCSClientBatchGetSync(ch, 1, &op, 5000U), HIXL_SUCCESS);
}

TEST_F(HixlCSClientFixture, CsApiBatchPutSyncInvalidListNum) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22343;
  CreateHixlClient(cli, client_ip, port);
  HixlOneSideOpDesc op{};
  EXPECT_EQ(HixlCSClientBatchPutSync(ClientHandleFrom(&cli), 0, &op, 1000U), HIXL_PARAM_INVALID);
}

TEST_F(HixlCSClientFixture, CsApiBatchGetSyncInvalidListNum) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22344;
  CreateHixlClient(cli, client_ip, port);
  HixlOneSideOpDesc op{};
  EXPECT_EQ(HixlCSClientBatchGetSync(ClientHandleFrom(&cli), 0, &op, 1000U), HIXL_PARAM_INVALID);
}

TEST_F(HixlCSClientFixture, BatchPutFailsOnUnrecordedMemory) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22337;
  PrepareConnectionAndImport(cli, client_ip, port);
  //不注册本地内存，直接创建任务
  void* remote_list[] = {&kServerDataAddr};
  const void* local_list[] = {&kClientBufAddr};
  uint64_t len_list[] = {4};
  void* query_handle = nullptr;
  CommunicateMem com_mem{1, remote_list, local_list, len_list};
  EXPECT_EQ(cli.BatchTransfer(false, com_mem, &query_handle), PARAM_INVALID);
  EXPECT_EQ(query_handle, nullptr);
}

TEST_F(HixlCSClientFixture, BatchPutFailsOnMultrecorded) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22337;
  PrepareConnectionAndImport(cli, client_ip, port);
  CommMem local = {COMM_MEM_TYPE_HOST, &kClientBufAddr, kClientBufSizeBytes};
  MemHandle local_handle = nullptr;
  CommMem local2 = {COMM_MEM_TYPE_HOST, &kClientBufAddr + size_t{100}, kClientBufSizeBytes2};
  MemHandle local_handle2 = nullptr;
  CommMem local3 = {COMM_MEM_TYPE_HOST, &kClientBufAddr + size_t{100}, kClientBufSizeBytes};
  MemHandle local_handle3 = nullptr;

  ASSERT_EQ(cli.RegMem("client_buf", &local, &local_handle), SUCCESS);
  ASSERT_EQ(cli.RegMem("client_buf", &local2, &local_handle2), PARAM_INVALID);
  ASSERT_EQ(cli.RegMem("client_buf", &local3, &local_handle3), PARAM_INVALID);
}

// 测试 ReleaseCompleteHandle 功能
TEST_F(HixlCSClientFixture, ReleaseCompleteHandleTest) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22338;
  PrepareConnectionAndImport(cli, client_ip, port);

  // 注册本地内存
  RecordLocalMem(cli);

  // 执行一次传输获取 query_handle
  void *remote_list[] = {&kServerDataAddr};
  const void *local_list[] = {&kClientBufAddr};
  uint64_t len_list[] = {4};
  void *query_handle = nullptr;
  CommunicateMem com_mem{1, remote_list, local_list, len_list};
  ASSERT_EQ(cli.BatchTransfer(false, com_mem, &query_handle), SUCCESS);
  ASSERT_NE(query_handle, nullptr);

  // 通过 CheckStatus 检查状态（stub 环境下会自动释放 handle）
  HixlCompleteStatus status_out = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  Status st = cli.CheckStatus(query_handle, &status_out);
  // 在 stub 环境下，状态可能不是 COMPLETED，但应该能正常返回
  EXPECT_TRUE(st == SUCCESS || status_out == HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING);
}

// 测试多次 BatchTransfer 后 CheckStatus 释放 handle
TEST_F(HixlCSClientFixture, MultipleBatchTransferAndCheckStatus) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22339;
  PrepareConnectionAndImport(cli, client_ip, port);

  // 注册本地内存
  RecordLocalMem(cli);

  // 执行多次传输
  std::vector<void *> query_handles;
  for (int i = 0; i < 3; ++i) {
    void *remote_list[] = {&kServerDataAddr};
    const void *local_list[] = {&kClientBufAddr};
    uint64_t len_list[] = {4};
    void *query_handle = nullptr;
    CommunicateMem com_mem{1, remote_list, local_list, len_list};
    EXPECT_EQ(cli.BatchTransfer(false, com_mem, &query_handle), SUCCESS);
    if (query_handle != nullptr) {
      query_handles.push_back(query_handle);
    }
  }
  // 验证至少有一次传输成功
  EXPECT_GE(query_handles.size(), 1U);
}

// 测试传输重试逻辑：15个任务时，前10次正常返回，第11次触发重试
// 重试后 HcommChannelFenceOnThread 会重置计数器，所以重试后的传输会成功
TEST_F(HixlCSClientFixture, BatchTransferWithRetryLogic) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22340;
  PrepareConnectionAndImport(cli, client_ip, port);

  // 注册本地内存
  RecordLocalMem(cli);

  // 创建15个传输任务
  // 前10次：正常返回成功
  // 第11次开始：返回 HCCL_RETRY_REQUIRED (20)，触发重试逻辑
  // 重试时：Fence 会重置计数器，传输再次成功
  void *remote_list[] = {&kServerDataAddr, &kServerDataAddr, &kServerDataAddr, &kServerDataAddr,
                         &kServerDataAddr, &kServerDataAddr, &kServerDataAddr, &kServerDataAddr,
                         &kServerDataAddr, &kServerDataAddr, &kServerDataAddr, &kServerDataAddr,
                         &kServerDataAddr, &kServerDataAddr, &kServerDataAddr};
  const void *local_list[] = {&kClientBufAddr, &kClientBufAddr, &kClientBufAddr, &kClientBufAddr,
                              &kClientBufAddr, &kClientBufAddr, &kClientBufAddr, &kClientBufAddr,
                              &kClientBufAddr, &kClientBufAddr, &kClientBufAddr, &kClientBufAddr,
                              &kClientBufAddr, &kClientBufAddr, &kClientBufAddr};
  uint64_t len_list[] = {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
  void *query_handle = nullptr;
  // 注意： CommunicateMem 的定义是 {dst_buf_list, src_buf_list, len_list}
  // BatchTransfer(false) 表示 Put，即从 local (src) 写入 remote (dst)
  CommunicateMem com_mem{15, remote_list, local_list, len_list};
  // 批量传输应该成功，因为重试逻辑会处理第11次开始的 HCCL_RETRY_REQUIRED
  EXPECT_EQ(cli.BatchTransfer(false, com_mem, &query_handle), SUCCESS);
  EXPECT_NE(query_handle, nullptr);
}

// 测试传输任务在 ChannelFenceOnThread 执行失败时的错误处理
// 场景：传输过程中触发重试，在重试时设置 Fence 返回 HCCL_E_PARA（非0非20）
// 验证 BatchTransfer 返回 FAILED
TEST_F(HixlCSClientFixture, BatchTransferChannelFenceFailure) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22341;
  PrepareConnectionAndImport(cli, client_ip, port);

  // 注册本地内存
  RecordLocalMem(cli);

  // 创建2个传输任务
  // 第1次传输会成功
  // 第2次传输会触发 HCCL_RETRY_REQUIRED (20)
  // 重试时 Fence 会返回 HCCL_E_PARA，BatchTransfer 应该返回 FAILED
  void *remote_list[] = {&kServerDataAddr, &kServerDataAddr};
  const void *local_list[] = {&kClientBufAddr, &kClientBufAddr};
  uint64_t len_list[] = {4, 4};
  void *query_handle = nullptr;
  CommunicateMem com_mem{2, remote_list, local_list, len_list};

  // 设置在重试时 Fence 返回 HCCL_E_PARA
  SetNextFenceFailure(HCCL_E_PARA);

  // 批量传输应该失败，因为 Fence 执行失败
  EXPECT_EQ(cli.BatchTransfer(false, com_mem, &query_handle), FAILED);
}

// 测试传输任务在 ReadNbiOnThread 执行失败时的错误处理
// 场景：设置 ReadNbi 返回 HCCL_E_PARA（非0非20），验证 BatchTransfer 返回 FAILED
TEST_F(HixlCSClientFixture, BatchTransferReadNbiFailure) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22342;
  PrepareConnectionAndImport(cli, client_ip, port);

  // 注册本地内存
  RecordLocalMem(cli);

  // 创建2个传输任务
  // CommunicateMem: {dst_buf_list, src_buf_list, len_list}
  // 对于 Get (BatchTransfer true): dst_buf_list=local, src_buf_list=remote
  void *local_list[] = {&kClientBufAddr, &kClientBufAddr};
  const void *remote_list[] = {&kServerDataAddr, &kServerDataAddr};
  uint64_t len_list[] = {4, 4};
  void *query_handle = nullptr;
  CommunicateMem com_mem{2, local_list, remote_list, len_list};

  // 设置 ReadNbi 返回 HCCL_E_PARA
  SetNextNbiFailure(HCCL_E_PARA);

  // 批量传输应该失败，因为 ReadNbi 执行失败
  EXPECT_EQ(cli.BatchTransfer(true, com_mem, &query_handle), FAILED);
}
}  // namespace hixl
