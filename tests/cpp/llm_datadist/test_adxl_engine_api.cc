/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <cstdlib>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "adxl/adxl_engine.h"
#include "slog/toolchain/slog.h"
#include "depends/llm_datadist/src/data_cache_engine_test_helper.h"

using namespace std;
using namespace llm;
using namespace ::testing;
using ::testing::Invoke;
using ::testing::Mock;

namespace adxl {
class AdxlEngineSTest : public ::testing::Test {
 protected:
  // 在测试类中设置一些准备工作，如果需要的话
  void SetUp() override {
    SetMockRtGetDeviceWay(1);
    llm::MockMmpaForHcclApi::Install();
    llm::AutoCommResRuntimeMock::Install();
  }
  // 在测试类中进行清理工作，如果需要的话
  void TearDown() override {
    llm::HcclAdapter::GetInstance().Finalize();
    llm::MockMmpaForHcclApi::Reset();
    llm::AutoCommResRuntimeMock::Reset();
    SetMockRtGetDeviceWay(0);
  }
};

TEST_F(AdxlEngineSTest, TestAdxlEngine) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  EXPECT_EQ(engine1.Initialize("127.0.0.1:26000", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  adxl::MemDesc mem{};
  mem.addr = 1234;
  mem.len = 10;
  MemHandle handle1 = nullptr;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(mem, MEM_DEVICE, handle1), SUCCESS);
  EXPECT_EQ(engine2.RegisterMem(mem, MEM_DEVICE, handle2), SUCCESS);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  int32_t src = 1;
  int32_t dst = 2;
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(&src), reinterpret_cast<uintptr_t>(&dst), sizeof(int32_t)};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  EXPECT_EQ(src, 2);
  src = 1;
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, {desc}), SUCCESS);
  EXPECT_EQ(dst, 1);
  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineSTest, TestAdxlEngineH2HWithBuffer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["adxl.BufferPool"] = "4:8";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  options2["adxl.BufferPool"] = "4:8";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);
  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }

  // still 16M data.
  size_t block_size = 256 * 1024;
  size_t block_num = 4 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineSTest, TestAdxlEngineD2HWithBuffer) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["adxl.BufferPool"] = "4:8";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  options2["adxl.BufferPool"] = "4:8";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  adxl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);

  TransferOpDesc desc{reinterpret_cast<uintptr_t>(src.data()), reinterpret_cast<uintptr_t>(dst.data()), size};
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  // still 16M data.
  dst.assign(size, 2);
  size_t block_size = 256 * 1024;
  size_t block_num = 4 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, {desc}), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineSTest, TestAdxlDefaultBufferPoolD2D) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  adxl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);
  adxl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);

  // still 16M data.
  size_t block_size = 128 * 1024;
  size_t block_num = 8 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}

TEST_F(AdxlEngineSTest, TestAdxlDisableBufferPoolD2D) {
  llm::AutoCommResRuntimeMock::SetDevice(0);
  AdxlEngine engine1;
  std::map<AscendString, AscendString> options1;
  options1[OPTION_RDMA_TRAFFIC_CLASS] = "1";
  options1[OPTION_RDMA_SERVICE_LEVEL] = "1";
  options1["adxl.BufferPool"] = "0:0";
  EXPECT_EQ(engine1.Initialize("127.0.0.1", options1), SUCCESS);

  llm::AutoCommResRuntimeMock::SetDevice(1);
  AdxlEngine engine2;
  std::map<AscendString, AscendString> options2;
  options2["adxl.BufferPool"] = "0:0";
  EXPECT_EQ(engine2.Initialize("127.0.0.1:26001", options2), SUCCESS);

  size_t size = 16 * 1024 * 1024;
  std::vector<int8_t> src(size, 1);
  std::vector<int8_t> dst(size, 2);
  adxl::MemDesc src_mem{};
  src_mem.addr = reinterpret_cast<uintptr_t>(src.data());
  src_mem.len = size;
  MemHandle handle1 = nullptr;
  EXPECT_EQ(engine1.RegisterMem(src_mem, MEM_DEVICE, handle1), SUCCESS);
  adxl::MemDesc dst_mem{};
  dst_mem.addr = reinterpret_cast<uintptr_t>(dst.data());
  dst_mem.len = size;
  MemHandle handle2 = nullptr;
  EXPECT_EQ(engine2.RegisterMem(dst_mem, MEM_DEVICE, handle2), SUCCESS);

  EXPECT_EQ(engine1.Connect("127.0.0.1:26001"), SUCCESS);

  // still 16M data.
  size_t block_size = 128 * 1024;
  size_t block_num = 8 * 16;
  std::vector<TransferOpDesc> descs;
  for (size_t i = 0; i < block_num; ++i) {
    descs.emplace_back(TransferOpDesc{reinterpret_cast<uintptr_t>(src.data()) + i * block_size,
                                      reinterpret_cast<uintptr_t>(dst.data()) + i * block_size, block_size});
  }
  src.assign(size, 1);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", WRITE, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(dst[i], 1);
  }
  dst.assign(size, 2);
  EXPECT_EQ(engine1.TransferSync("127.0.0.1:26001", READ, descs), SUCCESS);
  for (size_t i = 0; i < size; ++i) {
    EXPECT_EQ(src[i], 2);
  }

  EXPECT_EQ(engine1.Disconnect("127.0.0.1:26001"), SUCCESS);
  EXPECT_EQ(engine1.DeregisterMem(handle1), SUCCESS);
  EXPECT_EQ(engine2.DeregisterMem(handle2), SUCCESS);
  engine1.Finalize();
  engine2.Finalize();
}
}  // namespace adxl
