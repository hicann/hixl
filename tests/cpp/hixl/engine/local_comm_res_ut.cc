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

// 从文件读取内容
std::string ReadFileContent(const std::string &path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) return "";
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
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

class LocalCommResEdgeTest : public ::testing::Test {};

namespace {

RouteData MakeTwoEntryRouteData() {
  RouteData route_data;
  RouteEntry e1;
  e1.device_id = 0;
  e1.local_eid = "0xaa";
  e1.remote_eid = "0xbb";
  route_data.entries.push_back(e1);
  RouteEntry e2;
  e2.device_id = 1;
  e2.local_eid = "0xcc";
  e2.remote_eid = "0xdd";
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
  EXPECT_EQ(edges[0].comm_id, "0xaa");
  EXPECT_EQ(edges[0].placement, kPlacementHost);
  EXPECT_EQ(edges[0].dst_eid, "0xbb");
  EXPECT_EQ(edges[1].comm_id, "0xcc");
  EXPECT_EQ(edges[1].dst_eid, "0xdd");
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
  EXPECT_EQ(edges[0].comm_id, "0xbb");  // D2H: comm_id = remote_eid
  EXPECT_EQ(edges[0].placement, kPlacementDevice);
  EXPECT_EQ(edges[0].dst_eid, "0xaa");  // D2H: dst_eid = local_eid
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

class LocalCommResGenerateTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetDcmiStub();
    data_dir_ = GetTestDataDir();
  }

  void TearDown() override {
    ResetDcmiStub();
  }

  std::string data_dir_;
};

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
  EXPECT_EQ(ret, PARAM_INVALID);
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
      "pair0_chan0_local_eid=0x000000000000008000100000dfdf00f2\n"
      "pair0_chan0_remote_eid=0x000000000000008000100000dfdf0072\n";
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

}  // namespace test
}  // namespace hixl
