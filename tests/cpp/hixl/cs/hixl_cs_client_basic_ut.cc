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
namespace hixl {

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
  d.mem.type = HCCL_MEM_TYPE_HOST;
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
uint32_t kClientBufAddr = 1;
uint32_t kServerDataAddr = 2;
uint64_t kTransFlagAddr = 1;
struct ImportedRemote {
  HcommMem* remote_mem_list = nullptr;
  char** tags_buf = nullptr;
  uint32_t list_num = 0;
};
// 封装：创建连接 + 导入远端内存
void PrepareConnectionAndImport(hixl::HixlCSClient& cli, const char* client_ip, uint32_t port) {
  EndpointDesc src = MakeSrcEp();
  EndpointDesc dst = MakeDstEp();
  HixlClientConfig config{}; // 默认构造
  ASSERT_EQ(cli.Create(client_ip, port, &src, &dst, &config), SUCCESS);

  std::vector<HixlMemDesc> descs;
  descs.push_back(MakeRemoteDesc("_hixl_builtin_host_trans_flag",
  &kTransFlagAddr, kFlagSizeBytes));
  descs.push_back(MakeRemoteDesc("server_data",
  &kServerDataAddr, kBlockSizeBytes));

  ImportedRemote ret{};
  ASSERT_EQ(cli.ImportRemoteMem(descs, &ret.remote_mem_list, &ret.tags_buf, &ret.list_num), SUCCESS);
}
class HixlCSClientFixture : public ::testing::Test {
 protected:
  hixl::HixlCSClient cli;
  void TearDown() override {
    (void)cli.Destroy();
  }
};


TEST_F(HixlCSClientFixture, RegMemAndUnRegMem) {
  // 先创建本端 endpoint（Create 不依赖 socket 初始化以外的 HCCL）
  const char *client_ip = "127.0.0.1";
  uint32_t server_port = 12345;
  EndpointDesc src = MakeSrcEp();
  EndpointDesc dst = MakeDstEp();
  HixlClientConfig config{}; // 默认构造
  // 对齐实现：Create 要求 dst.protocol != RESERVED 才能用于后续 Connect，这里仅测试 RegMem 不调用 Connect
  EXPECT_EQ(cli.Create(client_ip, server_port, &src, &dst, &config), SUCCESS);

  // 通过 RegMem 登记 client 侧内存（不能直接调用 mem_store_）
  HcommMem mem{};
  mem.type = HCCL_MEM_TYPE_HOST;
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
  EndpointDesc src = MakeSrcEp();
  EndpointDesc dst = MakeDstEp();
  HixlClientConfig config{}; // 默认构造
  EXPECT_EQ(cli.Create(client_ip, server_port, &src, &dst, &config), SUCCESS);

  // 构造两个远端内存描述并导入
  std::vector<HixlMemDesc> descs;
  descs.push_back(
      MakeRemoteDesc("_hixl_builtin_host_trans_flag", &kTransFlagAddr, kFlagSizeBytes));
  descs.push_back(MakeRemoteDesc("server_data", &kServerDataAddr, kBlockSizeBytes));

  HcommMem *remote_mem_list = nullptr;
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
  EndpointDesc src = MakeSrcEp();
  EndpointDesc dst = MakeDstEp();
  HixlClientConfig config{}; // 默认构造
  EXPECT_EQ(cli.Create(client_ip, port, &src, &dst, &config), SUCCESS);
  std::cout << "cli已创建" << std::endl;

  // 导入远端内存，包含完成标志与一个数据区
  std::vector<HixlMemDesc> descs;
  descs.push_back(
      MakeRemoteDesc("_hixl_builtin_host_trans_flag", &kTransFlagAddr, kFlagSizeBytes));
  std::cout<<"_hixl_builtin_host_trans_flag的地址是"<<&kTransFlagAddr<<std::endl;
  std::cout<<"_hixl_builtin_host_trans_flag的值是"<<kTransFlagAddr<<std::endl;
  descs.push_back(MakeRemoteDesc("server_data", &kServerDataAddr, kBlockSizeBytes));
  HcommMem *remote_mem_list = nullptr;
  char **tags_buf = nullptr;

  uint32_t list_num = 0;
  ASSERT_EQ(cli.ImportRemoteMem(descs, &remote_mem_list, &tags_buf, &list_num), SUCCESS);
  std::cout << "server远端内存已获取并记录" << std::endl;
  // 通过 RegMem 登记本地缓冲
  HcommMem local{};
  local.type = HCCL_MEM_TYPE_HOST;
  local.addr = &kClientBufAddr;
  local.size = kClientBufSizeBytes;
  MemHandle local_handle = nullptr;
  ASSERT_EQ(cli.RegMem("client_buf", &local, &local_handle), SUCCESS);
  std::cout << "client内存完成记录" << std::endl;
  void *remote_list[] = {&kServerDataAddr};
  const void *local_list[] = {&kClientBufAddr};
  uint64_t len_list[] = {4};
  void *query_handle = nullptr;
  CommunicateMem com_mem{1, remote_list, local_list, len_list};
  // 批量写入，桩的 HcommWriteNbi/Fence/ReadNbi 都是空实现，流程应返回 SUCCESS
  EXPECT_EQ(cli.BatchTransfer(false, com_mem, &query_handle), SUCCESS);
  std::cout << "执行批量写入，返回queryhandle" << std::endl;
  EXPECT_NE(query_handle, nullptr);
  CompleteHandle *task_flag = static_cast<CompleteHandle *>(query_handle);
  // 首次检查通常为 NOT_READY（flag 还未被置 1)
  HixlCompleteStatus status_out = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  uint64_t* flag = task_flag->flag_address;
  std::cout<<"falg的值是："<<*flag<<std::endl;
  Status st = cli.CheckStatus(task_flag, &status_out);
  EXPECT_EQ(st, SUCCESS);
  EXPECT_EQ(status_out, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
}

TEST_F(HixlCSClientFixture, BatchGetSuccessWithStubbedHccl) {
  const char *client_ip = "127.0.0.1";
  uint32_t port = 22336;
  PrepareConnectionAndImport(cli, client_ip, port);
  // 登记本地缓冲
  HcommMem local{};
  local.type = HCCL_MEM_TYPE_HOST;
  local.addr = &kClientBufAddr;
  local.size = kClientBufSizeBytes;
  MemHandle local_handle = nullptr;
  ASSERT_EQ(cli.RegMem("client_buf", &local, &local_handle), SUCCESS);

  const void* remote_list[] = {&kServerDataAddr};
  void* local_list[] = {&kClientBufAddr};
  uint64_t len_list[] = {4};
  void* query_handle = nullptr;
  CommunicateMem com_mem{1, local_list, remote_list, len_list};
  EXPECT_EQ(cli.BatchTransfer(true, com_mem, &query_handle), SUCCESS);
  EXPECT_NE(query_handle, nullptr);
  CompleteHandle *task_flag = static_cast<CompleteHandle *>(query_handle);
  HixlCompleteStatus status_out = HixlCompleteStatus::HIXL_COMPLETE_STATUS_WAITING;
  EXPECT_EQ(cli.CheckStatus(task_flag, &status_out), SUCCESS);
  EXPECT_EQ(status_out, HixlCompleteStatus::HIXL_COMPLETE_STATUS_COMPLETED);
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
  HcommMem local = {HCCL_MEM_TYPE_HOST, &kClientBufAddr, kClientBufSizeBytes};
  MemHandle local_handle = nullptr;
  HcommMem local2 = {HCCL_MEM_TYPE_HOST, &kClientBufAddr + size_t{100}, kClientBufSizeBytes2};
  MemHandle local_handle2 = nullptr;
  HcommMem local3 = {HCCL_MEM_TYPE_HOST, &kClientBufAddr + size_t{100}, kClientBufSizeBytes};
  MemHandle local_handle3 = nullptr;

  ASSERT_EQ(cli.RegMem("client_buf", &local, &local_handle), SUCCESS);
  ASSERT_EQ(cli.RegMem("client_buf", &local2, &local_handle2), PARAM_INVALID);
  ASSERT_EQ(cli.RegMem("client_buf", &local3, &local_handle3), PARAM_INVALID);
}
}  // namespace hixl