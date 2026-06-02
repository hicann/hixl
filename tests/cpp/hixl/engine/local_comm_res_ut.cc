/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file local_comm_res_ut.cc
 * @brief LocalCommRes 模块单元测试
 *
 * 测试覆盖：
 * - ParseTopoFile / ParseRouteFile 纯文件解析
 * - GenerateH2DEdges / GenerateD2HEdges / GenerateD2DEdges 边生成
 * - GenerateLocalCommRes 集成路径（通过 DCMI 桩函数）
 */

#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "local_comm_res_generator_v1.h"
#include "test_mmpa_utils.h"

// DCMI 桩函数控制接口（定义在 tests/depends/dcmi/src/dcmi_stub.cc）
extern "C" {
void DcmiStubSetInitRet(int ret);
void DcmiStubSetMainboardId(unsigned int id, int ret);
void DcmiStubSetLogicId(unsigned int id, int ret);
void DcmiStubSetUrmaDeviceCnt(unsigned int cnt, int ret);
void DcmiStubSetSuperPodId(unsigned int id, int ret);
void DcmiStubSetEidCount(int count);
}

namespace hixl {
namespace test {

namespace {

// 获取测试数据目录（通过 CMake 传入）
std::string GetTestDataDir() {
#ifdef HIXL_TEST_SRC_DIR
  return std::string(HIXL_TEST_SRC_DIR) + "/engine/";
#else
  return "./";
#endif
}

// urma_admin 路径常量（与 local_comm_res_generator_v1.cc 保持一致）
constexpr const char *kUrmaAdminPath = "/usr/local/sbin/urma_admin";

// 自定义 MmpaStub：拦截 urma_admin 路径检查，使代码回退到 PATH 查找
class LocalCommResMmpaStub : public hixl::test::KernelJsonMmpaStub {
 public:
  INT32 Access(const CHAR *path_name) override {
    std::string path_str(path_name);
    // 让 /usr/local/sbin/urma_admin 看起来不存在，触发 PATH 回退逻辑
    if (path_str == kUrmaAdminPath) {
      return EN_ERROR;
    }
    return KernelJsonMmpaStub::Access(path_name);
  }
};

// urma_admin show mock 输出数据
constexpr const char *kUrmaAdminMockOutput =
    "num  ubep_dev            tp_type     eid                                             link\n"
    "---  ----------------    --------    --------------------------------------------    --------\n"
    "0    udma10              UB          eid0 0000:0000:007f:0400:0010:0000:df00:9001    ACTIVE  \n"
    "1    udma11              UB          eid0 0000:0000:007f:0300:0010:0000:df00:9001    ACTIVE  \n"
    "2    udma2               UB          eid0 0000:0000:003f:0200:0010:0000:df00:1001    ACTIVE  \n"
    "3    udma3               UB          eid0 0000:0000:0000:0600:0010:0000:df00:1d01    ACTIVE  \n"
    "4    udma3               UB          eid1 0000:0000:003f:0600:0010:0000:df00:1001    ACTIVE  \n"
    "5    udma3               UB          eid2 0000:0000:0007:0600:0010:0000:df00:fd01    ACTIVE  \n"
    "6    udma3               UB          eid3 0000:0000:0006:0600:0010:0000:df00:dd01    ACTIVE  \n"
    "7    udma3               UB          eid4 0000:0000:0005:0600:0010:0000:df00:bd01    ACTIVE  \n"
    "8    udma3               UB          eid5 0000:0000:0004:0600:0010:0000:df00:9d01    ACTIVE  \n"
    "9    udma3               UB          eid6 0000:0000:0003:0600:0010:0000:df00:7d01    ACTIVE  \n"
    "10   udma3               UB          eid7 0000:0000:0002:0600:0010:0000:df00:5d01    ACTIVE  \n"
    "11   udma3               UB          eid8 0000:0000:0001:0600:0010:0000:df00:3d01    ACTIVE  \n"
    "12   udma4               UB          eid0 0000:0000:003f:0500:0010:0000:df00:1001    ACTIVE  \n"
    "13   udma5               UB          eid0 0000:0000:003f:0400:0010:0000:df00:1001    ACTIVE  \n"
    "14   udma6               UB          eid0 0000:0000:003f:0300:0010:0000:df00:1001    ACTIVE  \n"
    "15   udma7               UB          eid0 0000:0000:007f:0200:0010:0000:df00:9001    ACTIVE  \n"
    "16   udma8               UB          eid0 0000:0000:0040:0600:0010:0000:df00:1e01    ACTIVE  \n"
    "17   udma8               UB          eid1 0000:0000:007f:0600:0010:0000:df00:9001    ACTIVE  \n"
    "18   udma8               UB          eid2 0000:0000:0047:0600:0010:0000:df00:fe01    ACTIVE  \n"
    "19   udma8               UB          eid3 0000:0000:0046:0600:0010:0000:df00:de01    ACTIVE  \n"
    "20   udma8               UB          eid4 0000:0000:0045:0600:0010:0000:df00:be01    ACTIVE  \n"
    "21   udma8               UB          eid5 0000:0000:0044:0600:0010:0000:df00:9e01    ACTIVE  \n"
    "22   udma8               UB          eid6 0000:0000:0043:0600:0010:0000:df00:7e01    ACTIVE  \n"
    "23   udma8               UB          eid7 0000:0000:0042:0600:0010:0000:df00:5e01    ACTIVE  \n"
    "24   udma8               UB          eid8 0000:0000:0041:0600:0010:0000:df00:3e01    ACTIVE  \n"
    "25   udma9               UB          eid0 0000:0000:007f:0500:0010:0000:df00:9001    ACTIVE  \n";

// 创建 fake urma_admin 脚本到指定目录
void CreateFakeUrmaAdmin(const std::string &dir) {
  std::string script_path = dir + "/urma_admin";
  std::ofstream script(script_path.c_str());
  script << "#!/bin/bash\n";
  script << "echo '" << kUrmaAdminMockOutput << "'\n";
  script.close();
  chmod(script_path.c_str(), 0755);
}

// 创建临时目录用于 fake urma_admin
std::string CreateTempDirForUrmaAdmin() {
  std::string temp_dir = "/tmp/hixl_ut_urma_XXXXXX";
  char *result = mkdtemp(&temp_dir[0]);
  if (result == nullptr) {
    return "";
  }
  CreateFakeUrmaAdmin(temp_dir);
  return temp_dir;
}

// 设置 PATH 使 fake urma_admin 优先被找到，返回原 PATH
std::string SetUrmaAdminPath(const std::string &temp_dir) {
  const char *old_path = getenv("PATH");
  std::string new_path = temp_dir + ":" + (old_path ? old_path : "");
  setenv("PATH", new_path.c_str(), 1);
  return old_path ? old_path : "";
}

// 恢复 PATH
void RestorePath(const std::string &old_path) {
  if (old_path.empty()) {
    unsetenv("PATH");
  } else {
    setenv("PATH", old_path.c_str(), 1);
  }
}

// 清理临时目录
void CleanupTempDir(const std::string &temp_dir) {
  if (!temp_dir.empty()) {
    std::string script_path = temp_dir + "/urma_admin";
    unlink(script_path.c_str());
    rmdir(temp_dir.c_str());
  }
}

// 重置 DCMI 桩到默认成功状态
void ResetDcmiStub() {
  DcmiStubSetInitRet(0);
  DcmiStubSetMainboardId(0x3, 0);  // Pod1
  DcmiStubSetLogicId(0, 0);
  DcmiStubSetUrmaDeviceCnt(1, 0);
  DcmiStubSetSuperPodId(0, 0);
  DcmiStubSetEidCount(2);  // 默认返回 2 个 EID
}

// 字符串常量（与 local_comm_res_tool.cc 匿名命名空间中的定义保持一致）
constexpr const char *kLinkTypePeer2Peer = "PEER2PEER";
constexpr const char *kLinkTypePeer2Net = "PEER2NET";
constexpr const char *kTopoType1DMesh = "1DMESH";
constexpr const char *kTopoTypeClos = "CLOS";

}  // anonymous namespace

// ============================================================================
// 纯数据驱动测试（无需 DCMI 桩）
// ============================================================================

class LocalCommResParseTest : public ::testing::Test {
 protected:
  void SetUp() override {
    data_dir_ = GetTestDataDir();
  }
  std::string data_dir_;
};

// --- ParseTopoFile ---

TEST_F(LocalCommResParseTest, ParseTopoFileSuccess) {
  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  TopoData topo_data;
  int32_t ret = ParseTopoFile(topo_path, topo_data);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(topo_data.links.size(), 52U);
  // 验证第一条 link
  EXPECT_EQ(topo_data.links[0].net_layer, 0);
  EXPECT_EQ(topo_data.links[0].link_type, kLinkTypePeer2Peer);
  EXPECT_EQ(topo_data.links[0].topo_type, kTopoType1DMesh);
  EXPECT_EQ(topo_data.links[0].local_a, 0);
  EXPECT_EQ(topo_data.links[0].local_b, 1);
}

TEST_F(LocalCommResParseTest, ParseTopoFileNotFound) {
  TopoData topo_data;
  int32_t ret = ParseTopoFile("/nonexistent/path/topo.json", topo_data);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LocalCommResParseTest, ParseTopoFileInvalidJson) {
  std::string tmp = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", "not valid json {{{");
  ASSERT_FALSE(tmp.empty());
  TopoData topo_data;
  int32_t ret = ParseTopoFile(tmp, topo_data);
  EXPECT_NE(ret, SUCCESS);
  unlink(tmp.c_str());
}

TEST_F(LocalCommResParseTest, ParseTopoFileEmptyEdgeList) {
  std::string json = R"({"version":"2.0","peer_count":0,"peer_list":[],"edge_count":0,"edge_list":[]})";
  std::string tmp = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", json);
  ASSERT_FALSE(tmp.empty());
  TopoData topo_data;
  int32_t ret = ParseTopoFile(tmp, topo_data);
  // ParseTopoFile 将空 edge_list 视为解析失败
  EXPECT_EQ(ret, FAILED);
  unlink(tmp.c_str());
}

TEST_F(LocalCommResParseTest, ParseTopoFileEmptyContent) {
  // 空文件内容 → FAILED
  std::string tmp = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", "");
  ASSERT_FALSE(tmp.empty());
  TopoData topo_data;
  int32_t ret = ParseTopoFile(tmp, topo_data);
  EXPECT_EQ(ret, FAILED);
  unlink(tmp.c_str());
}

TEST_F(LocalCommResParseTest, ParseTopoFileMissingNetLayer) {
  // edge 对象缺少 net_layer 字段 → 该 edge 被跳过，links 为空
  std::string json =
      R"({"version":"2.0","edge_list":[{"link_type":"PEER2PEER","topo_type":"1DMESH","local_a":0,"local_b":1}]})";
  std::string tmp = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", json);
  ASSERT_FALSE(tmp.empty());
  TopoData topo_data;
  int32_t ret = ParseTopoFile(tmp, topo_data);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(topo_data.links.empty());
  unlink(tmp.c_str());
}

// --- ParseRouteFile ---

TEST_F(LocalCommResParseTest, ParseRouteFileSuccess) {
  std::string route_path = data_dir_ + "route.conf";
  RouteData route_data;
  int32_t ret = ParseRouteFile(route_path, route_data);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(route_data.entries.size(), 8U);
  // 验证第一条 entry
  EXPECT_EQ(route_data.entries[0].device_id, 0);
  EXPECT_FALSE(route_data.entries[0].local_eid.empty());
  EXPECT_FALSE(route_data.entries[0].remote_eid.empty());
}

TEST_F(LocalCommResParseTest, ParseRouteFileNotFound) {
  RouteData route_data;
  int32_t ret = ParseRouteFile("/nonexistent/path/route.conf", route_data);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LocalCommResParseTest, ParseRouteFileMalformed) {
  std::string content = "pair_device_num=1\npair0_dev_id=0\n";  // 缺少 chan 信息
  std::string tmp = CreateTempFileWithContent("/tmp/route_ut_XXXXXX", content);
  ASSERT_FALSE(tmp.empty());
  RouteData route_data;
  int32_t ret = ParseRouteFile(tmp, route_data);
  // pair_device_num 存在但 chan 信息不全，解析应成功但 entries 可能为空
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(route_data.entries.empty());
  unlink(tmp.c_str());
}

TEST_F(LocalCommResParseTest, ParseRouteFileMissingPairDeviceNum) {
  // 缺少 pair_device_num → BuildRouteEntries 返回 FAILED
  std::string content = "pair0_dev_id=0\npair0_chan0_local_eid=0xaa\n";
  std::string tmp = CreateTempFileWithContent("/tmp/route_ut_XXXXXX", content);
  ASSERT_FALSE(tmp.empty());
  RouteData route_data;
  int32_t ret = ParseRouteFile(tmp, route_data);
  EXPECT_EQ(ret, FAILED);
  unlink(tmp.c_str());
}

// ============================================================================
// 边生成测试（纯数据结构操作，无需 DCMI）
// ============================================================================

// MmpaStub 测试基类（公共 SetUp/TearDown，用于需要 PATH 注入的测试）
class LocalCommResMmpaTestBase : public ::testing::Test {
 protected:
  void SetUp() override {
    // 设置 MmpaStub 使 urma_admin 绝对路径检查失败，回退到 PATH 查找
    llm::MmpaStub::GetInstance().SetImpl(std::make_shared<LocalCommResMmpaStub>());
    temp_dir_ = CreateTempDirForUrmaAdmin();
    if (!temp_dir_.empty()) {
      old_path_ = SetUrmaAdminPath(temp_dir_);
    }
  }
  void TearDown() override {
    if (!temp_dir_.empty()) {
      RestorePath(old_path_);
      CleanupTempDir(temp_dir_);
    }
    // 恢复默认 MmpaStub（使用 Reset 而非 SetImpl(nullptr)，避免后续 mmAccess 调用崩溃）
    llm::MmpaStub::GetInstance().Reset();
  }
  std::string temp_dir_;
  std::string old_path_;
};

class LocalCommResEdgeTest : public LocalCommResMmpaTestBase {};

namespace {

RouteData MakeTwoEntryRouteData() {
  RouteData route_data;
  RouteEntry e1;
  e1.device_id = 0;
  e1.local_eid = "000000000002008000100000dfdf0091";  // byte6=0x02, die_id=0
  e1.remote_eid = "0000000000f2008000100000dfdf0001";
  route_data.entries.push_back(e1);
  RouteEntry e2;
  e2.device_id = 1;
  e2.local_eid = "000000000052008000100000dfdf0091";  // byte6=0x52, die_id=1
  e2.remote_eid = "000000000072008000100000dfdf0001";
  route_data.entries.push_back(e2);
  return route_data;
}

TopoLink MakeStandardTopoLink(int32_t net_layer, const std::string &link_type, const std::string &topo_type) {
  TopoLink link;
  link.net_layer = net_layer;
  link.link_type = link_type;
  link.topo_type = topo_type;
  link.local_a = 0;
  link.local_b = 1;
  link.local_a_ports = {"0/1"};
  link.local_b_ports = {"0/2"};
  return link;
}

TopoData MakeSingleLinkTopoData(const TopoLink &link) {
  TopoData topo_data;
  topo_data.links.push_back(link);
  return topo_data;
}

NpuRootInfo MakeRootInfo(const std::string &port, const std::string &eid) {
  NpuRootInfo info;
  info.port_to_eid[port] = eid;
  return info;
}

std::map<int32_t, NpuRootInfo> MakeNpuRootinfos(int32_t id0, const NpuRootInfo &info0, int32_t id1,
                                                const NpuRootInfo &info1) {
  std::map<int32_t, NpuRootInfo> m;
  m[id0] = info0;
  m[id1] = info1;
  return m;
}

}  // anonymous namespace

// --- GenerateH2DEdges ---

TEST_F(LocalCommResEdgeTest, GenerateH2DEdgesSuccess) {
  RouteData route_data = MakeTwoEntryRouteData();

  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateH2DEdges(route_data, edges);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_EQ(edges.size(), 2U);
  EXPECT_EQ(edges[0].protocol, kProtocolUbCtp);
  EXPECT_EQ(edges[0].comm_id, "000000000002008000100000dfdf0091");
  EXPECT_EQ(edges[0].placement, kPlacementHost);
  EXPECT_EQ(edges[0].dst_eid, "0000000000f2008000100000dfdf0001");
  EXPECT_EQ(edges[1].comm_id, "000000000052008000100000dfdf0091");
  EXPECT_EQ(edges[1].dst_eid, "000000000072008000100000dfdf0001");
}

TEST_F(LocalCommResEdgeTest, GenerateH2DEdgesEmptyRoute) {
  RouteData route_data;
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateH2DEdges(route_data, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

// --- GenerateD2HEdges ---

TEST_F(LocalCommResEdgeTest, GenerateD2HEdgesSuccess) {
  RouteData route_data = MakeTwoEntryRouteData();

  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2HEdges(route_data, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_EQ(edges.size(), 1U);  // 只取 device_id=0 的条目
  EXPECT_EQ(edges[0].protocol, kProtocolUbCtp);
  EXPECT_EQ(edges[0].comm_id, "0000000000f2008000100000dfdf0001");  // D2H: comm_id = remote_eid
  EXPECT_EQ(edges[0].placement, kPlacementDevice);
  EXPECT_EQ(edges[0].dst_eid, "000000000002008000100000dfdf0091");  // D2H: dst_eid = local_eid
}

TEST_F(LocalCommResEdgeTest, GenerateD2HEdgesEmptyRoute) {
  RouteData route_data;
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2HEdges(route_data, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2HEdgesNoMatch) {
  // 所有 entry 的 device_id 都不匹配 phy_dev_id%8
  RouteData route_data;
  RouteEntry e1;
  e1.device_id = 1;
  e1.local_eid = "aa";
  e1.remote_eid = "bb";
  route_data.entries.push_back(e1);
  RouteEntry e2;
  e2.device_id = 2;
  e2.local_eid = "cc";
  e2.remote_eid = "dd";
  route_data.entries.push_back(e2);

  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2HEdges(route_data, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2HEdgesPhyIdGreaterThan7) {
  // phy_dev_id=8 → 8%8=0，应匹配 device_id=0 的条目
  RouteData route_data;
  RouteEntry e1;
  e1.device_id = 0;
  e1.local_eid = "aa";
  e1.remote_eid = "bb";
  route_data.entries.push_back(e1);
  RouteEntry e2;
  e2.device_id = 1;
  e2.local_eid = "cc";
  e2.remote_eid = "dd";
  route_data.entries.push_back(e2);

  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2HEdges(route_data, 8, edges);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_EQ(edges.size(), 1U);
  EXPECT_EQ(edges[0].comm_id, "bb");
  EXPECT_EQ(edges[0].dst_eid, "aa");
}

// Note: GenerateD2UEdges / GenerateH2UEdges 在 .cc 中的实际签名与 .h 声明不一致，
// 无法从 UT 直接调用，通过 GenerateLocalCommRes 集成路径间接覆盖。

// --- GenerateD2UEdges (Change #1/#3: plane_pg EID 边生成) ---

TEST_F(LocalCommResEdgeTest, GenerateD2UEdgesBothPlanes) {
  std::vector<EndpointConfig> edges;
  GenerateD2UEdges("pg0_eid", "pg1_eid", edges);
  ASSERT_EQ(edges.size(), 2U);
  EXPECT_EQ(edges[0].comm_id, "pg0_eid");
  EXPECT_EQ(edges[0].placement, kPlacementDevice);
  EXPECT_EQ(edges[0].plane, "plane_pg_0");
  EXPECT_EQ(edges[1].comm_id, "pg1_eid");
  EXPECT_EQ(edges[1].plane, "plane_pg_1");
}

TEST_F(LocalCommResEdgeTest, GenerateD2UEdgesOnlyPlane0) {
  std::vector<EndpointConfig> edges;
  GenerateD2UEdges("pg0_eid", "", edges);
  ASSERT_EQ(edges.size(), 1U);
  EXPECT_EQ(edges[0].comm_id, "pg0_eid");
  EXPECT_EQ(edges[0].plane, "plane_pg_0");
}

TEST_F(LocalCommResEdgeTest, GenerateD2UEdgesEmpty) {
  std::vector<EndpointConfig> edges;
  GenerateD2UEdges("", "", edges);
  EXPECT_TRUE(edges.empty());
}

// --- GenerateH2UEdges (Change #3: Host PG EID 作为 comm_id) ---
// 注意：GenerateH2UEdges 内部调用 GetHostPgEid（依赖 popen urma_admin show），
// 在 UT 环境中 popen 会失败，因此返回 FAILED。

TEST_F(LocalCommResEdgeTest, GenerateH2UEdgesSuccess) {
  // urma_admin show 桩函数返回有效输出，GetHostPgEid 应成功
  RouteData route_data = MakeTwoEntryRouteData();
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateH2UEdges(0, route_data, "pg0_eid", "pg1_eid", edges);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_EQ(edges.size(), 2U);
  EXPECT_EQ(edges[0].plane, "plane_pg_0");
  EXPECT_EQ(edges[1].plane, "plane_pg_1");
}

// --- GenerateD2DEdges ---

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesEmptyTopo) {
  TopoData topo_data;
  std::map<int32_t, NpuRootInfo> npu_rootinfos;
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesNoRootinfoForSelf) {
  // npu_rootinfos 中没有 phy_id=0 的条目 → 返回空
  TopoData topo_data = MakeSingleLinkTopoData(MakeStandardTopoLink(0, kLinkTypePeer2Peer, kTopoType1DMesh));
  std::map<int32_t, NpuRootInfo> npu_rootinfos;
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesSkipNetLayer1) {
  // net_layer=1 的 link 应被跳过
  TopoData topo_data = MakeSingleLinkTopoData(MakeStandardTopoLink(1, kLinkTypePeer2Peer, kTopoType1DMesh));
  auto npu_rootinfos = MakeNpuRootinfos(0, MakeRootInfo("0/1", "eid_self"), 1, MakeRootInfo("0/2", "eid_peer"));
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesSkipNonPeer2Peer) {
  // link_type=PEER2NET 应被跳过
  TopoData topo_data = MakeSingleLinkTopoData(MakeStandardTopoLink(0, kLinkTypePeer2Net, kTopoType1DMesh));
  NpuRootInfo info = MakeRootInfo("0/1", "eid_self");
  auto npu_rootinfos = MakeNpuRootinfos(0, info, 1, info);
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesSkipNon1DMESH) {
  // topo_type=CLOS 应被跳过
  TopoData topo_data = MakeSingleLinkTopoData(MakeStandardTopoLink(0, kLinkTypePeer2Peer, kTopoTypeClos));
  NpuRootInfo info = MakeRootInfo("0/1", "eid_self");
  auto npu_rootinfos = MakeNpuRootinfos(0, info, 1, info);
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesSkipPhyIdNotInLink) {
  // phy_id=2 不在 link(local_a=0, local_b=1) 中 → 跳过
  TopoData topo_data = MakeSingleLinkTopoData(MakeStandardTopoLink(0, kLinkTypePeer2Peer, kTopoType1DMesh));
  NpuRootInfo info = MakeRootInfo("0/1", "eid_a");
  std::map<int32_t, NpuRootInfo> npu_rootinfos;
  npu_rootinfos[0] = info;
  npu_rootinfos[1] = info;
  npu_rootinfos[2] = info;

  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 2, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesSkipEmptyPorts) {
  // local_a_ports 为空 → 跳过
  TopoLink link = MakeStandardTopoLink(0, kLinkTypePeer2Peer, kTopoType1DMesh);
  link.local_a_ports = {};
  TopoData topo_data = MakeSingleLinkTopoData(link);
  NpuRootInfo info = MakeRootInfo("0/1", "eid");
  auto npu_rootinfos = MakeNpuRootinfos(0, info, 1, info);
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResEdgeTest, GenerateD2DEdgesMatchSuccess) {
  // 正常匹配：local_a=0 有 port 0/1 → eid_aaa，local_b=1 有 port 0/2 → eid_bbb
  TopoData topo_data = MakeSingleLinkTopoData(MakeStandardTopoLink(0, kLinkTypePeer2Peer, kTopoType1DMesh));
  auto npu_rootinfos = MakeNpuRootinfos(0, MakeRootInfo("0/1", "eid_aaa"), 1, MakeRootInfo("0/2", "eid_bbb"));

  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateD2DEdges(topo_data, npu_rootinfos, 0, edges);
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_EQ(edges.size(), 1U);
  EXPECT_EQ(edges[0].protocol, kProtocolUbCtp);
  EXPECT_EQ(edges[0].comm_id, "eid_aaa");
  EXPECT_EQ(edges[0].placement, kPlacementDevice);
  EXPECT_EQ(edges[0].dst_eid, "eid_bbb");
}

// ============================================================================
// GenerateLocalCommRes 集成测试（需要 DCMI 桩）
// ============================================================================

// LocalCommRes 测试基类（公共 SetUp/TearDown）
class LocalCommResTestBase : public LocalCommResMmpaTestBase {
 protected:
  void SetUp() override {
    // 先调用基类 SetUp，完成 MmpaStub + temp_dir 初始化
    LocalCommResMmpaTestBase::SetUp();
    // 添加 TestBase 特有的初始化
    ResetDcmiStub();
    data_dir_ = GetTestDataDir();
  }

  void TearDown() override {
    // 先执行 TestBase 特有的清理
    ResetDcmiStub();
    // 调用基类 TearDown，完成 temp_dir 清理 + MmpaStub Reset
    LocalCommResMmpaTestBase::TearDown();
  }

  std::string data_dir_;
};

class LocalCommResGenerateTest : public LocalCommResTestBase {};

TEST_F(LocalCommResGenerateTest, GenerateSuccess) {
  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(res.version, "1.3");
  EXPECT_FALSE(res.endpoint_list.empty());
  // 所有 endpoint 应有 net_instance_id
  for (const auto &ep : res.endpoint_list) {
    EXPECT_FALSE(ep.net_instance_id.empty());
  }
}

TEST_F(LocalCommResGenerateTest, GenerateTopoNotFound) {
  std::string topo_path = "/nonexistent/topo.json";
  std::string route_path = data_dir_ + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LocalCommResGenerateTest, GenerateRouteNotFound) {
  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = "/nonexistent/route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  // route.conf 不存在时会尝试 procfs fallback，procfs 也不存在则返回 FAILED
  EXPECT_NE(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateGetMainboardIdFailed) {
  DcmiStubSetMainboardId(0, -1);  // 模拟失败

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_NE(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateGetClosNetInstanceIdFailed) {
  DcmiStubSetSuperPodId(0, -1);  // 模拟 SPOD 查询失败

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_NE(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GeneratePodMainboardId) {
  DcmiStubSetMainboardId(0x3, 0);  // Pod1

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());
}

TEST_F(LocalCommResGenerateTest, GenerateServerMainboardId) {
  DcmiStubSetMainboardId(0x21, 0);  // Server

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateBuildNpuRootinfosFailed) {
  // URMA 设备数为 0 → BuildNpuRootInfo 返回 FAILED → BuildNpuRootinfos 失败
  DcmiStubSetUrmaDeviceCnt(0, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, FAILED);
}

TEST_F(LocalCommResGenerateTest, GenerateEmptyAllEdges) {
  // DCMI 仅返回非 PG EID（无 PG EID → clos_pg_eids 为空）
  // BuildNpuRootInfo 因 clos_pg_eids 为空返回 FAILED
  DcmiStubSetEidCount(1);  // 仅返回非 PG EID，plane_pg EID 为空

  std::string topo_json =
      R"({"version":"2.0","edge_list":[{"net_layer":1,"link_type":"PEER2PEER","topo_type":"1DMESH","local_a":0,"local_b":1}]})";
  std::string tmp_topo = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", topo_json);
  ASSERT_FALSE(tmp_topo.empty());

  std::string tmp_route = CreateTempFileWithContent("/tmp/route_ut_XXXXXX", "pair_device_num=0\n");
  ASSERT_FALSE(tmp_route.empty());

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, tmp_topo, tmp_route, res);
  EXPECT_EQ(ret, FAILED);

  unlink(tmp_topo.c_str());
  unlink(tmp_route.c_str());
}

// --- 产品形态覆盖（IsProductServer / IsProductPod / GetMeshDieId） ---

TEST_F(LocalCommResGenerateTest, GenerateServerOddMainboardId) {
  // mainboard_id=0x23（奇数，在 [0x21,0x2B] 范围内）→ IsProductServer=true
  DcmiStubSetMainboardId(0x23, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());
}

TEST_F(LocalCommResGenerateTest, GenerateServerEvenMainboardIdInRange2) {
  // mainboard_id=0x42（偶数，在 [0x40,0x46] 范围内）→ IsProductServer=true
  DcmiStubSetMainboardId(0x42, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());
}

TEST_F(LocalCommResGenerateTest, GenerateNotServerEvenInRange1) {
  // mainboard_id=0x22（偶数，在 [0x21,0x2B] 范围内但不满足 %2==1）→ IsProductServer=false, IsProductPod=false
  DcmiStubSetMainboardId(0x22, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateNotServerOddInRange2) {
  // mainboard_id=0x41（奇数，在 [0x40,0x46] 范围内但不满足 %2==0）→ IsProductServer=false
  DcmiStubSetMainboardId(0x41, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateNotServerBelowRange) {
  // mainboard_id=0x20（低于 [0x21,0x2B]）→ IsProductServer=false
  DcmiStubSetMainboardId(0x20, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GenerateNotServerAboveRange) {
  // mainboard_id=0x47（高于 [0x40,0x46]）→ IsProductServer=false
  DcmiStubSetMainboardId(0x47, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
}

TEST_F(LocalCommResGenerateTest, GeneratePod2MainboardId) {
  // mainboard_id=0x5 → IsProductPod=true (Pod2)
  DcmiStubSetMainboardId(0x5, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());
}

TEST_F(LocalCommResGenerateTest, GeneratePod3MainboardId) {
  // mainboard_id=0x7 → IsProductPod=true (Pod3)
  DcmiStubSetMainboardId(0x7, 0);

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());
}

// --- CollectRelatedNpuIds / GetMeshDieId 分组覆盖 ---

TEST_F(LocalCommResGenerateTest, GeneratePhyIdInSecondGroup) {
  // phy_dev_id=9 → group_start=8, NPU 8-15; GetMeshDieId(9, false) → 9%8=1 → die_id=1
  DcmiStubSetMainboardId(0x3, 0);  // Pod

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(9, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());
}

TEST_F(LocalCommResGenerateTest, GenerateServerMeshDieId) {
  // Server 产品形态 → GetMeshDieId 始终返回 1
  DcmiStubSetMainboardId(0x21, 0);  // Server

  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());
}

// --- 0x 前缀剥离测试 ---

TEST_F(LocalCommResGenerateTest, GenerateRouteEidStrips0xPrefix) {
  // route.conf 中的 EID 带 0x 前缀 → 最终 endpoint 中应无 0x
  std::string topo_json =
      R"({"version":"2.0","edge_list":[{"net_layer":0,"link_type":"PEER2PEER","topo_type":"1DMESH","local_a":0,"local_b":1,"local_a_ports":["1/0"],"local_b_ports":["1/1"]}]})";
  std::string tmp_topo = CreateTempFileWithContent("/tmp/topo_ut_XXXXXX", topo_json);
  ASSERT_FALSE(tmp_topo.empty());

  std::string route_content =
      "pair_device_num=1\n"
      "pair0_dev_id=0\n"
      "pair0_chan_num=1\n"
      "pair0_chan0_local_eid=0x0000000000f2008000100000dfdf0091\n"
      "pair0_chan0_remote_eid=0x000000000072008000100000dfdf0091\n";
  std::string tmp_route = CreateTempFileWithContent("/tmp/route_ut_XXXXXX", route_content);
  ASSERT_FALSE(tmp_route.empty());

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, tmp_topo, tmp_route, res);
  EXPECT_EQ(ret, SUCCESS);

  // 验证 H2D/D2H 边中的 EID 不含 0x 前缀
  for (const auto &ep : res.endpoint_list) {
    EXPECT_EQ(ep.comm_id.find("0x"), std::string::npos) << "comm_id has 0x prefix: " << ep.comm_id;
    EXPECT_EQ(ep.dst_eid.find("0x"), std::string::npos) << "dst_eid has 0x prefix: " << ep.dst_eid;
  }

  unlink(tmp_topo.c_str());
  unlink(tmp_route.c_str());
}

// --- GetMainboardId / GetClosNetInstanceId 接口覆盖 ---

TEST_F(LocalCommResGenerateTest, GetMainboardIdSuccess) {
  DcmiStubSetMainboardId(0x42, 0);
  unsigned int mainboard_id = 0;
  int32_t ret = GetMainboardId(0, mainboard_id);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(mainboard_id, 0x42U);
}

TEST_F(LocalCommResGenerateTest, GetClosNetInstanceIdSuccess) {
  DcmiStubSetSuperPodId(5, 0);
  std::string net_instance_id;
  int32_t ret = GetClosNetInstanceId(0, net_instance_id);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_EQ(net_instance_id, "superpod_5");
}

// --- ParseEidByte6 覆盖（rootinfo_builder 模块） ---

TEST(LocalCommResRootinfoTest, ParseEidByte6ShortEid) {
  // EID 长度 < 12 → 返回默认值（全 0）
  EidByte6Info info = ParseEidByte6("0000");
  EXPECT_EQ(info.byte6, 0);
  EXPECT_EQ(info.high_nibble, 0);
  EXPECT_EQ(info.low_nibble, 0);
  EXPECT_EQ(info.die_id, 0);
  EXPECT_FALSE(info.is_pg_eid);
  EXPECT_EQ(info.port, 0);
}

TEST(LocalCommResRootinfoTest, ParseEidByte6EmptyEid) {
  EidByte6Info info = ParseEidByte6("");
  EXPECT_EQ(info.byte6, 0);
  EXPECT_FALSE(info.is_pg_eid);
}

TEST(LocalCommResRootinfoTest, ParseEidByte6NonPgEid) {
  // byte6=0xf2: high=0xf → die_id=1, is_pg=false, port=2
  // byte6 在 eid.substr(10, 2) 位置，即第 10-11 个字符
  std::string eid = "0000000000f200000000000000000000";
  EidByte6Info info = ParseEidByte6(eid);
  EXPECT_EQ(info.byte6, 0xf2);
  EXPECT_EQ(info.high_nibble, 0xf);
  EXPECT_EQ(info.low_nibble, 0x2);
  EXPECT_EQ(info.die_id, 1);
  EXPECT_FALSE(info.is_pg_eid);
  EXPECT_EQ(info.port, 2);
}

TEST(LocalCommResRootinfoTest, ParseEidByte6PgEid) {
  // byte6=0x72: high=0x7 → die_id=1, is_pg=true, port=2
  std::string eid = "00000000007200000000000000000000";
  EidByte6Info info = ParseEidByte6(eid);
  EXPECT_EQ(info.byte6, 0x72);
  EXPECT_EQ(info.high_nibble, 0x7);
  EXPECT_TRUE(info.is_pg_eid);
  EXPECT_EQ(info.die_id, 1);
  EXPECT_EQ(info.port, 2);
}

TEST(LocalCommResRootinfoTest, ParseEidByte6Die0) {
  // byte6=0x32: high=0x3 → die_id=0, is_pg=true, port=2
  std::string eid = "00000000003200000000000000000000";
  EidByte6Info info = ParseEidByte6(eid);
  EXPECT_EQ(info.byte6, 0x32);
  EXPECT_EQ(info.die_id, 0);
  EXPECT_TRUE(info.is_pg_eid);
  EXPECT_EQ(info.port, 2);
}

// ============================================================================
// Change #1 测试：Topo 文件路径调整（MatchProductForm / FindTopoFileByMainboardId）
// 通过默认 GenerateLocalCommRes 重载间接测试产品形态匹配逻辑
// ============================================================================

class LocalCommResTopoPathTest : public ::testing::Test {
 protected:
  void SetUp() override {
    DcmiStubSetInitRet(0);
    DcmiStubSetLogicId(0, 0);
    DcmiStubSetUrmaDeviceCnt(1, 0);
    DcmiStubSetSuperPodId(0, 0);
    DcmiStubSetEidCount(2);
  }
  void TearDown() override {
    ResetDcmiStub();
  }
};

TEST_F(LocalCommResTopoPathTest, DefaultOverloadPodMainboardId) {
  // Pod 产品形态（0x3）→ MatchProductForm 匹配 atlas_950_* 前缀
  // 在 UT 环境中 /usr/local/Ascend/driver/topo/950/ 不存在，应返回 PARAM_INVALID
  DcmiStubSetMainboardId(0x3, 0);
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, res);
  // topo 目录不存在 → FindTopoFileByMainboardId 返回空 → PARAM_INVALID
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LocalCommResTopoPathTest, DefaultOverloadServerMainboardId) {
  // Server 产品形态（0x21）→ MatchProductForm 匹配 atlas_850_* 前缀
  DcmiStubSetMainboardId(0x21, 0);
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, res);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LocalCommResTopoPathTest, DefaultOverloadUnknownMainboardId) {
  // 未知 mainboard_id（0x99）→ MatchProductForm 返回 false → PARAM_INVALID
  DcmiStubSetMainboardId(0x99, 0);
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, res);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LocalCommResTopoPathTest, DefaultOverloadGetMainboardIdFailed) {
  // GetMainboardId 失败 → 直接返回错误
  DcmiStubSetMainboardId(0, -1);
  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, res);
  EXPECT_NE(ret, SUCCESS);
}

// ============================================================================
// Change #2 测试：route.conf 不存在时的 procfs fallback
// ============================================================================

class LocalCommResProcfsFallbackTest : public LocalCommResTestBase {};

TEST_F(LocalCommResProcfsFallbackTest, RouteNotFoundProcfsNotAvailable) {
  // route.conf 不存在 + procfs 不可用 → 返回 FAILED
  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = "/nonexistent/route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_NE(ret, SUCCESS);
}

TEST_F(LocalCommResProcfsFallbackTest, RouteExistsNoFallback) {
  // route.conf 存在 → 不触发 procfs fallback → 正常流程
  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string route_path = data_dir_ + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());
}

TEST_F(LocalCommResProcfsFallbackTest, RouteMalformedProcfsNotAvailable) {
  // route.conf 内容格式错误（缺少 pair_device_num）→ ParseRouteFile 返回 FAILED
  // → 触发 procfs fallback → procfs 不可用 → 返回 FAILED
  std::string topo_path = data_dir_ + "server_8p_noroce.json";
  std::string tmp_route = CreateTempFileWithContent("/tmp/route_ut_XXXXXX", "invalid_content=no_pair_device_num\n");
  ASSERT_FALSE(tmp_route.empty());

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, tmp_route, res);
  EXPECT_NE(ret, SUCCESS);

  unlink(tmp_route.c_str());
}

// ============================================================================
// Change #3 测试：H2U 边 comm_id 使用 Host PG EID
// 通过 GenerateH2UEdges 直接测试（函数已在 header 中声明）
// ============================================================================

class LocalCommResH2UTest : public LocalCommResMmpaTestBase {};

TEST_F(LocalCommResH2UTest, H2UEdgesSuccess) {
  // urma_admin show 桩函数返回有效输出，GetHostPgEid 应成功
  RouteData route_data;
  RouteEntry e;
  e.device_id = 0;
  e.local_eid = "00000000003200000000000000df0091";
  e.remote_eid = "0000000000f200000000000000df0001";
  route_data.entries.push_back(e);

  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateH2UEdges(0, route_data, "pg0_eid", "pg1_eid", edges);
  // 桩函数返回有效数据 → 成功生成 H2U 边
  EXPECT_EQ(ret, SUCCESS);
  ASSERT_EQ(edges.size(), 2U);
  EXPECT_EQ(edges[0].plane, "plane_pg_0");
  EXPECT_EQ(edges[1].plane, "plane_pg_1");
}

TEST_F(LocalCommResH2UTest, H2UEdgesEmptyRouteData) {
  // 空 route_data → GetHostPgEid 中找不到匹配的 device_id → FAILED
  RouteData route_data;
  std::vector<EndpointConfig> edges;
  int32_t ret = GenerateH2UEdges(0, route_data, "pg0_eid", "pg1_eid", edges);
  EXPECT_EQ(ret, FAILED);
  EXPECT_TRUE(edges.empty());
}

TEST_F(LocalCommResH2UTest, D2UEdgesSuccessWithBothPlanes) {
  // GenerateD2UEdges 不依赖外部命令，可正常测试
  std::vector<EndpointConfig> edges;
  GenerateD2UEdges("plane_pg_0_eid", "plane_pg_1_eid", edges);
  ASSERT_EQ(edges.size(), 2U);
  EXPECT_EQ(edges[0].comm_id, "plane_pg_0_eid");
  EXPECT_EQ(edges[0].placement, kPlacementDevice);
  EXPECT_EQ(edges[0].plane, "plane_pg_0");
  EXPECT_EQ(edges[1].comm_id, "plane_pg_1_eid");
  EXPECT_EQ(edges[1].plane, "plane_pg_1");
}

TEST_F(LocalCommResH2UTest, D2UEdgesOnlyPlanePg0) {
  std::vector<EndpointConfig> edges;
  GenerateD2UEdges("pg0_eid", "", edges);
  ASSERT_EQ(edges.size(), 1U);
  EXPECT_EQ(edges[0].comm_id, "pg0_eid");
  EXPECT_EQ(edges[0].plane, "plane_pg_0");
}

TEST_F(LocalCommResH2UTest, D2UEdgesNoPlanes) {
  std::vector<EndpointConfig> edges;
  GenerateD2UEdges("", "", edges);
  EXPECT_TRUE(edges.empty());
}

// ============================================================================
// 集成测试：H2U 边失败时的错误传播
// 验证 CollectAllEdges 在 GetHostPgEid 失败时正确传播错误
// ============================================================================

TEST_F(LocalCommResH2UTest, IntegrationH2USuccess) {
  // urma_admin show 桩函数返回有效数据 → H2U 边生成成功 → 整体成功
  DcmiStubSetInitRet(0);
  DcmiStubSetMainboardId(0x3, 0);
  DcmiStubSetLogicId(0, 0);
  DcmiStubSetUrmaDeviceCnt(1, 0);
  DcmiStubSetSuperPodId(0, 0);
  DcmiStubSetEidCount(2);

  std::string data_dir = GetTestDataDir();
  std::string topo_path = data_dir + "server_8p_noroce.json";
  std::string route_path = data_dir + "route.conf";

  LocalCommRes res;
  int32_t ret = GenerateLocalCommRes(0, topo_path, route_path, res);
  EXPECT_EQ(ret, SUCCESS);
  EXPECT_FALSE(res.endpoint_list.empty());

  ResetDcmiStub();
}

// ============================================================================
// ProcfsRouteHandler UT
// 真实文件系统版本：通过构造时注入临时目录，移除所有 mock 抽象
// ============================================================================

// Helper to create valid pair_info content
std::string MakePairInfoContent(const std::string &slot_id, const std::vector<std::string> &local_eids,
                                const std::vector<std::string> &remote_eids) {
  std::ostringstream oss;
  for (size_t i = 0; i < local_eids.size() && i < remote_eids.size(); ++i) {
    oss << "dev_id=0 slot_id=" << slot_id << "\n";
    oss << "local_eid: " << local_eids[i] << "\n";
    oss << "remote_eid: " << remote_eids[i] << "\n";
  }
  return oss.str();
}

class ProcfsRouteHandlerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::string temp_dir = "/tmp/hixl_procfs_ut_XXXXXX";
    char *result = mkdtemp(&temp_dir[0]);
    ASSERT_NE(result, nullptr);
    proc_base_ = temp_dir;
  }

  void TearDown() override {
    if (!proc_base_.empty()) {
      std::string cmd = "rm -rf " + proc_base_;
      (void)system(cmd.c_str());
      proc_base_.clear();
    }
  }

  // 在临时目录下写入指定文件的内容；name 相对于 proc_base_
  void WriteProcFile(const std::string &name, const std::string &content) {
    std::string path = proc_base_ + "/" + name;
    std::ofstream of(path.c_str());
    ASSERT_TRUE(of.is_open());
    of << content;
    of.close();
  }

  // 显式注入的 proc 根目录
  std::string proc_base_;
};

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataProcPathNotFound) {
  // 注入的目录存在但 dev_id 不存在 → FindProcBasePath 返回空 → 失败
  hixl::ProcfsRouteHandler handler(proc_base_);

  std::set<int32_t> related_npu_ids = {0, 1};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::FAILED);
  EXPECT_TRUE(route_data.entries.empty());
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataAscendUbFound) {
  WriteProcFile("dev_id", "");
  WriteProcFile("pair_info", MakePairInfoContent("0", {"0x0000000000f2008000100000dfdf0091"},
                                                 {"0x000000000072008000100000dfdf0001"}));

  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {0};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::SUCCESS);
  ASSERT_EQ(route_data.entries.size(), 1U);
  EXPECT_EQ(route_data.entries[0].device_id, 0);  // 0 % 8 = 0
  EXPECT_EQ(route_data.entries[0].local_eid, "0000000000f2008000100000dfdf0091");
  EXPECT_EQ(route_data.entries[0].remote_eid, "000000000072008000100000dfdf0001");
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataAsdrvUbFound) {
  // 复用同一注入路径，行为与 ascend_ub 一致（注入路径优先）
  WriteProcFile("dev_id", "");
  WriteProcFile("pair_info", MakePairInfoContent("1", {"0x0000000000f2008000100000dfdf0091"},
                                                 {"0x000000000072008000100000dfdf0001"}));

  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {1};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::SUCCESS);
  ASSERT_EQ(route_data.entries.size(), 1U);
  EXPECT_EQ(route_data.entries[0].device_id, 1);  // 1 % 8 = 1
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataWriteFails) {
  // dev_id 不存在 → open() 失败 → WriteStringToFile 失败 → ProcessNpuProcfsRoute 返回 FAILED
  WriteProcFile("pair_info", MakePairInfoContent("0", {"0x0000000000f2008000100000dfdf0091"},
                                                 {"0x000000000072008000100000dfdf0001"}));

  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {0};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::FAILED);
  EXPECT_TRUE(route_data.entries.empty());
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataReadPairInfoFails) {
  // dev_id 存在，pair_info 缺失 → 读取失败
  WriteProcFile("dev_id", "");

  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {0};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::FAILED);
  EXPECT_TRUE(route_data.entries.empty());
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataMalformedPairInfo) {
  WriteProcFile("dev_id", "");
  WriteProcFile("pair_info", "not valid pair info content\n");

  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {0};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::FAILED);
  EXPECT_TRUE(route_data.entries.empty());
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataMultipleNpus) {
  WriteProcFile("dev_id", "");
  WriteProcFile("pair_info",
                MakePairInfoContent("2", {"0x0000000000f2008000100000dfdf0091", "0x0000000000f2008000100000dfdf0092"},
                                    {"0x000000000072008000100000dfdf0001", "0x000000000072008000100000dfdf0002"}));

  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {0, 1, 2, 3};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::SUCCESS);
  // device_id = npu_id % 8, so 0,1,2,3 should all generate entries
  ASSERT_EQ(route_data.entries.size(), 4U);
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataEmptyNpuIds) {
  WriteProcFile("dev_id", "");
  WriteProcFile("pair_info", "");

  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids;  // empty
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  // No NPUs to process, no entries generated → returns FAILED
  EXPECT_EQ(ret, hixl::FAILED);
  EXPECT_TRUE(route_data.entries.empty());
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataNpuIdGreaterThan7) {
  WriteProcFile("dev_id", "");
  WriteProcFile("pair_info", MakePairInfoContent("0", {"0x0000000000f2008000100000dfdf0091"},
                                                 {"0x000000000072008000100000dfdf0001"}));

  // npu_id = 10, device_id = 10 % 8 = 2
  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {10};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::SUCCESS);
  ASSERT_EQ(route_data.entries.size(), 1U);
  EXPECT_EQ(route_data.entries[0].device_id, 2);  // 10 % 8 = 2
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataEid0xPrefixStripped) {
  WriteProcFile("dev_id", "");
  WriteProcFile("pair_info", MakePairInfoContent("0", {"0xaa", "0xbb"}, {"0xcc", "0xdd"}));

  // npu_id=0 → group_offset=0 → eid_idx=0; npu_id=4 → group_offset=4 → eid_idx=1
  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {0, 4};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::SUCCESS);
  ASSERT_EQ(route_data.entries.size(), 2U);
  // Verify 0x prefix is stripped
  EXPECT_EQ(route_data.entries[0].local_eid, "aa");
  EXPECT_EQ(route_data.entries[0].remote_eid, "cc");
  EXPECT_EQ(route_data.entries[1].local_eid, "bb");
  EXPECT_EQ(route_data.entries[1].remote_eid, "dd");
}

TEST_F(ProcfsRouteHandlerTest, GenerateRouteDataEidColonStripped) {
  WriteProcFile("dev_id", "");
  WriteProcFile("pair_info", MakePairInfoContent("0", {"0xaa:bb:cc", "dd:ee:ff"}, {"11:22:33", "44:55:66"}));

  // npu_id=0 → group_offset=0 → eid_idx=0; npu_id=4 → group_offset=4 → eid_idx=1
  hixl::ProcfsRouteHandler handler(proc_base_);
  std::set<int32_t> related_npu_ids = {0, 4};
  hixl::RouteData route_data;
  int32_t ret = handler.GenerateRouteData(related_npu_ids, route_data);

  EXPECT_EQ(ret, hixl::SUCCESS);
  ASSERT_EQ(route_data.entries.size(), 2U);
  // Verify colons are stripped
  EXPECT_EQ(route_data.entries[0].local_eid, "aabbcc");
  EXPECT_EQ(route_data.entries[0].remote_eid, "112233");
  EXPECT_EQ(route_data.entries[1].local_eid, "ddeeff");
  EXPECT_EQ(route_data.entries[1].remote_eid, "445566");
}

// ============================================================================
// TopoFileFinder UT
// ============================================================================

class TopoFileFinderTest : public ::testing::Test {};

// Helper: Create temp dir with topo files
std::string CreateTempTopoDir(const std::string &prefix, bool with_850_file, bool with_950_file) {
  std::string temp_dir = "/tmp/hixl_topo_ut_XXXXXX";
  char *result = mkdtemp(&temp_dir[0]);
  if (result == nullptr) {
    return "";
  }
  if (with_850_file) {
    std::string file_path = temp_dir + "/" + prefix + "_850_server.json";
    std::ofstream of(file_path.c_str());
    of << "{}";
    of.close();
  }
  if (with_950_file) {
    std::string file_path = temp_dir + "/" + prefix + "_950_pod.json";
    std::ofstream of(file_path.c_str());
    of << "{}";
    of.close();
  }
  return temp_dir;
}

// Helper: Cleanup temp dir
void CleanupTopoTempDir(const std::string &temp_dir) {
  if (!temp_dir.empty()) {
    std::string cmd = "rm -rf " + temp_dir;
    system(cmd.c_str());
  }
}

TEST_F(TopoFileFinderTest, FindTopoFileServerProduct) {
  // Server 产品 (mainboard_id=0x21) 应匹配 atlas_850_* 前缀
  std::string temp_dir = CreateTempTopoDir("atlas", true, true);
  ASSERT_FALSE(temp_dir.empty());

  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile(temp_dir, 0x21);

  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("850"), std::string::npos);

  CleanupTopoTempDir(temp_dir);
}

TEST_F(TopoFileFinderTest, FindTopoFilePodProduct) {
  // Pod 产品 (mainboard_id=0x3) 应匹配 atlas_950_* 前缀
  std::string temp_dir = CreateTempTopoDir("atlas", true, true);
  ASSERT_FALSE(temp_dir.empty());

  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile(temp_dir, 0x3);

  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("950"), std::string::npos);

  CleanupTopoTempDir(temp_dir);
}

TEST_F(TopoFileFinderTest, FindTopoFilePod2Product) {
  // Pod2 产品 (mainboard_id=0x5) 应匹配 atlas_950_* 前缀
  std::string temp_dir = CreateTempTopoDir("atlas", true, true);
  ASSERT_FALSE(temp_dir.empty());

  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile(temp_dir, 0x5);

  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("950"), std::string::npos);

  CleanupTopoTempDir(temp_dir);
}

TEST_F(TopoFileFinderTest, FindTopoFilePod3Product) {
  // Pod3 产品 (mainboard_id=0x7) 应匹配 atlas_950_* 前缀
  std::string temp_dir = CreateTempTopoDir("atlas", true, true);
  ASSERT_FALSE(temp_dir.empty());

  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile(temp_dir, 0x7);

  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("950"), std::string::npos);

  CleanupTopoTempDir(temp_dir);
}

TEST_F(TopoFileFinderTest, FindTopoFileServerEvenRange2) {
  // Server 产品偶数范围 (mainboard_id=0x42) 应匹配 atlas_850_* 前缀
  std::string temp_dir = CreateTempTopoDir("atlas", true, true);
  ASSERT_FALSE(temp_dir.empty());

  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile(temp_dir, 0x42);

  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("850"), std::string::npos);

  CleanupTopoTempDir(temp_dir);
}

TEST_F(TopoFileFinderTest, FindTopoFileDirectoryNotExist) {
  // 目录不存在应返回空
  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile("/nonexistent/path", 0x21);

  EXPECT_TRUE(result.empty());
}

TEST_F(TopoFileFinderTest, FindTopoFileNoMatchingFile) {
  // 目录存在但没有匹配的文件应返回空
  std::string temp_dir = CreateTempTopoDir("atlas", false, false);  // 不创建任何文件
  ASSERT_FALSE(temp_dir.empty());

  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile(temp_dir, 0x21);

  EXPECT_TRUE(result.empty());

  CleanupTopoTempDir(temp_dir);
}

TEST_F(TopoFileFinderTest, FindTopoFileUnknownMainboardId) {
  // 未知 mainboard_id 应返回空
  std::string temp_dir = CreateTempTopoDir("atlas", true, true);
  ASSERT_FALSE(temp_dir.empty());

  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile(temp_dir, 0x99);

  EXPECT_TRUE(result.empty());

  CleanupTopoTempDir(temp_dir);
}

TEST_F(TopoFileFinderTest, FindTopoFileServerOddMainboardId) {
  // Server 产品奇数 mainboard_id (0x23) 应匹配 atlas_850_* 前缀
  std::string temp_dir = CreateTempTopoDir("atlas", true, true);
  ASSERT_FALSE(temp_dir.empty());

  hixl::TopoFileFinder finder;
  std::string result = finder.FindTopoFile(temp_dir, 0x23);

  EXPECT_FALSE(result.empty());
  EXPECT_NE(result.find("850"), std::string::npos);

  CleanupTopoTempDir(temp_dir);
}

// ============================================================================
// GenerateLocalCommResJson 测试
// ============================================================================
//
// GenerateLocalCommResJson 内部调用 GenerateLocalCommRes（无参版本）使用默认
// 路径 /usr/local/Ascend/driver/topo/950/，UT 环境下该路径不存在，因此无法构造
// "成功" 路径（需要真实系统环境）；但可以验证 "失败" 路径下的错误码传递。
// （成功路径下 std::string → AscendString 转换逻辑被 lcrgen 工具本地测试覆盖，
//  无需在单元测试中重复验证。）

class LocalCommResJsonTest : public LocalCommResTopoPathTest {};

TEST_F(LocalCommResJsonTest, PropagatesGetMainboardIdFailure) {
  // GetMainboardId 失败 → GenerateLocalCommRes 内部立即返回错误码 →
  // GenerateLocalCommResJson 应透传该错误码。
  DcmiStubSetMainboardId(0, -1);

  LocalCommResView view;
  int32_t ret = GenerateLocalCommResJson(0, view);
  EXPECT_NE(ret, SUCCESS);
}

TEST_F(LocalCommResJsonTest, ParamInvalidWhenDefaultTopoMissing) {
  // mainboard_id 成功获取 → 进入 FindTopoFile 逻辑；
  // 默认 topo 目录 /usr/local/Ascend/driver/topo/950/ 在 UT 环境不存在 →
  // 返回 PARAM_INVALID。
  DcmiStubSetMainboardId(0x3, 0);  // Pod1

  LocalCommResView view;
  int32_t ret = GenerateLocalCommResJson(0, view);
  EXPECT_EQ(ret, PARAM_INVALID);
}

TEST_F(LocalCommResJsonTest, ParamInvalidOnUnknownMainboardId) {
  // 未知 mainboard_id（0x99）→ MatchProductForm 返回 false → PARAM_INVALID
  DcmiStubSetMainboardId(0x99, 0);

  LocalCommResView view;
  int32_t ret = GenerateLocalCommResJson(0, view);
  EXPECT_EQ(ret, PARAM_INVALID);
}

// ============================================================================
// ConvertLocalCommResToView 测试
// ============================================================================
//
// 纯 std::string → AscendString 字段转换测试，覆盖 GenerateLocalCommResJson
// 中"成功路径"的转换逻辑（GenerateLocalCommRes 失败时这些代码不会执行）。

TEST(ConvertLocalCommResToViewTest, AllFieldsPopulated) {
  LocalCommRes src;
  src.version = "1.3";
  src.net_instance_id = "superpod_1";

  EndpointConfig ep;
  ep.protocol = "TCP";
  ep.comm_id = "comm0";
  ep.placement = "HOST";
  ep.plane = "plane_pg_0";
  ep.dst_eid = "0x1234";
  ep.net_instance_id = "superpod_1";
  ep.device_info.phy_device_id = 0;
  ep.device_info.super_device_id = 1;
  ep.device_info.super_pod_id = 2;
  src.endpoint_list.push_back(ep);

  LocalCommResView dst;
  ConvertLocalCommResToView(src, dst);

  EXPECT_STREQ(dst.version.GetString(), "1.3");
  EXPECT_STREQ(dst.net_instance_id.GetString(), "superpod_1");
  ASSERT_EQ(dst.endpoint_list.size(), 1u);
  EXPECT_STREQ(dst.endpoint_list[0].protocol.GetString(), "TCP");
  EXPECT_STREQ(dst.endpoint_list[0].comm_id.GetString(), "comm0");
  EXPECT_STREQ(dst.endpoint_list[0].placement.GetString(), "HOST");
  EXPECT_STREQ(dst.endpoint_list[0].plane.GetString(), "plane_pg_0");
  EXPECT_STREQ(dst.endpoint_list[0].dst_eid.GetString(), "0x1234");
  EXPECT_STREQ(dst.endpoint_list[0].net_instance_id.GetString(), "superpod_1");
  EXPECT_EQ(dst.endpoint_list[0].device_info.phy_device_id, 0);
  EXPECT_EQ(dst.endpoint_list[0].device_info.super_device_id, 1);
  EXPECT_EQ(dst.endpoint_list[0].device_info.super_pod_id, 2);
}

TEST(ConvertLocalCommResToViewTest, EmptyEndpointList) {
  LocalCommRes src;
  src.version = "1.3";
  src.net_instance_id = "superpod_2";
  // src.endpoint_list 留空

  LocalCommResView dst;
  ConvertLocalCommResToView(src, dst);

  EXPECT_STREQ(dst.version.GetString(), "1.3");
  EXPECT_STREQ(dst.net_instance_id.GetString(), "superpod_2");
  EXPECT_TRUE(dst.endpoint_list.empty());
}

TEST(ConvertLocalCommResToViewTest, EmptyStringFields) {
  LocalCommRes src;
  src.version = "";
  src.net_instance_id = "";
  EndpointConfig ep;  // 所有 std::string 字段留空
  src.endpoint_list.push_back(ep);

  LocalCommResView dst;
  ConvertLocalCommResToView(src, dst);

  EXPECT_EQ(dst.version.GetLength(), 0u);
  EXPECT_EQ(dst.net_instance_id.GetLength(), 0u);
  ASSERT_EQ(dst.endpoint_list.size(), 1u);
  EXPECT_EQ(dst.endpoint_list[0].protocol.GetLength(), 0u);
  EXPECT_EQ(dst.endpoint_list[0].comm_id.GetLength(), 0u);
  EXPECT_EQ(dst.endpoint_list[0].placement.GetLength(), 0u);
  EXPECT_EQ(dst.endpoint_list[0].plane.GetLength(), 0u);
  EXPECT_EQ(dst.endpoint_list[0].dst_eid.GetLength(), 0u);
  EXPECT_EQ(dst.endpoint_list[0].net_instance_id.GetLength(), 0u);
}

TEST(ConvertLocalCommResToViewTest, OverwritesExistingView) {
  // 目标 dst 已有内容，调用后应被整体覆盖（不能残留旧数据）
  LocalCommResView dst;
  dst.version = AscendString("old_version");
  dst.net_instance_id = AscendString("old_net_id");
  EndpointConfigView old_ep;
  old_ep.protocol = AscendString("old_proto");
  dst.endpoint_list.push_back(std::move(old_ep));

  LocalCommRes src;
  src.version = "new_version";
  src.net_instance_id = "new_net_id";
  EndpointConfig new_ep;
  new_ep.protocol = "new_proto";
  src.endpoint_list.push_back(new_ep);

  ConvertLocalCommResToView(src, dst);

  EXPECT_STREQ(dst.version.GetString(), "new_version");
  EXPECT_STREQ(dst.net_instance_id.GetString(), "new_net_id");
  ASSERT_EQ(dst.endpoint_list.size(), 1u);
  EXPECT_STREQ(dst.endpoint_list[0].protocol.GetString(), "new_proto");
}

}  // namespace test
}  // namespace hixl
