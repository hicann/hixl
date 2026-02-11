/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hixl/hixl_types.h"
#include "hixl_cs_client.h"
#include "common/ctrl_msg.h"

#include <securec.h>

namespace hixl {
static constexpr uint32_t kPort = 16000;
static constexpr int kMemReqCnt = 0U;
static constexpr uint32_t kSrcEpId = 1U;
static constexpr uint32_t kDstEpId = 2U;
static constexpr uint32_t kZero = 0U;
static constexpr int kMilliSeconds10 = 10;
static constexpr int kMilliSeconds1 = 11;
static constexpr int kListenN = 16;
static constexpr int kGetMemReqCnt = 2;
static constexpr uint32_t kDefaultConnectTimeoutMs = 2000U;
static constexpr uint32_t kConnectTime = 100U;
static constexpr uint32_t kConnectTime1 = 50U;
static constexpr uint32_t kConnectTime2 = 200U;
static constexpr uint32_t kTimeOutOne = 1000U;
static constexpr char kGetRemoteMemStr0[] = "_hixl_builtin_dev_trans_flag";
static constexpr char kGetRemoteMemStr1[] = "a";
static constexpr char kGetRemoteMemStr2[] = "b";
static HcommMem gRemoteSentinel{};
static char *gTagsSentinel[1] = {nullptr};
static constexpr uint32_t kNumSentinel = 123U;
enum class MiniSrvMode : uint32_t {
  kNormal = 0,

  // --- Connect(CreateChannelResp) 相关异常 ---
  kConnectResp_BadMagic,
  kConnectResp_BadBodySize,
  kConnectResp_BadMsgType,
  kConnectResp_FailResult,

  // --- GetRemoteMemResp 相关异常 ---
  kGetRemoteMemResp_BadMagic,
  kGetRemoteMemResp_BadMsgType,
  kGetRemoteMemResp_BadBodySizeTooSmall,  // body_size <= sizeof(CtrlMsgType)
  kGetRemoteMemResp_BadJson,
  kGetRemoteMemResp_MissingFieldResult,
  kGetRemoteMemResp_MissingFieldMemDescs,
  kGetRemoteMemResp_FailResult,

  // --- ImportRemoteMem 触发的校验/冲突 ---
  kGetRemoteMemResp_EmptyList,        // result=0, mem_descs=[]
  kGetRemoteMemResp_ExportDescEmpty,  // export_desc="" -> handler 会让解析后 export_len=0
  kGetRemoteMemResp_DuplicateAddr,    // 两个 mem 用同 addr -> RecordMemory(true) 冲突
  kGetRemoteMemResp_MemImportFail
};

class MiniServer {
 public:
  MiniServer() = default;
  ~MiniServer() {
    Stop();
  }

  void EnableAltMemResp(bool on) {
    mem_resp_use_alt_ = on;
  }
  void SetConnectMode(MiniSrvMode m) {
    connect_mode_ = m;
  }
  void SetGetRemoteMemMode(MiniSrvMode m) {
    mem_mode_ = m;
  }

  uint16_t Start() {
    Stop();

    stop_.store(false);
    port_ = kPort;
    get_mem_req_cnt_ = 0;

    worker_ = std::thread(
      [this]() {
        ThreadMain();
      });
    std::this_thread::sleep_for(std::chrono::milliseconds(kMilliSeconds10));
    return port_;
  }

  void Stop() {
    stop_.store(true);

    if (listen_fd_ >= 0) {
      (void)shutdown(listen_fd_, SHUT_RDWR);
      (void)close(listen_fd_);
      listen_fd_ = -1;
    }
    if (conn_fd_ >= 0) {
      (void)shutdown(conn_fd_, SHUT_RDWR);
      (void)close(conn_fd_);
      conn_fd_ = -1;
    }

    if (worker_.joinable()) {
      worker_.join();
    }
    port_ = kZero;
    get_mem_req_cnt_ = kMemReqCnt;
  }

 private:
  static void SetNonBlock(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
  }

  static bool IsEagainOrWouldBlock(int err) noexcept {
    return (err == EAGAIN) || (err == EWOULDBLOCK);
  }

  static bool HandleRetryableErrno(int err) {
    if (err == EINTR) {
      return true;  // 直接重试
    }
    if (IsEagainOrWouldBlock(err)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(kMilliSeconds1));
      return true;  // sleep 后重试
    }
    return false;  // 不可重试
  }

  template <typename IoFn>
  static bool IoAllImpl(int fd, void *buf, size_t n, const IoFn &io_fn) {
    auto *p = static_cast<char *>(buf);
    size_t left = n;

    while (left > 0U) {
      const ssize_t ret = io_fn(fd, p, left);
      if (ret > 0) {
        const size_t done = static_cast<size_t>(ret);
        left -= done;
        p += static_cast<std::ptrdiff_t>(done);
        continue;
      }
      if (ret == 0) {
        return false;
      }
      const int err = errno;
      if (HandleRetryableErrno(err)) {
        continue;
      }
      return false;
    }
    return true;
  }

  static bool SendAll(int fd, void *buf, size_t n) {
    auto fn = [](int f, const char *p, size_t len) -> ssize_t {
      return ::send(f, p, len, MSG_NOSIGNAL);
    };
    return IoAllImpl(fd, buf, n,
      [fn](int f, char *p, size_t len) {
      return fn(f, p, len);
    });
  }

  static bool RecvAll(int fd, void *buf, size_t n) {
    auto fn = [](int f, char *p, size_t len) -> ssize_t {
      return ::recv(f, p, len, 0);
    };
    return IoAllImpl(fd, buf, n, fn);
  }

  bool SetupListenSocket() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
      return false;
    }

    int opt = 1;
    (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
      return false;
    }
    if (::listen(listen_fd_, kListenN) != 0) {
      return false;
    }

    SetNonBlock(listen_fd_);
    return true;
  }

  bool AcceptOnce() {
    while (!stop_.load()) {
      sockaddr_in caddr{};
      socklen_t clen = sizeof(caddr);
      const int cfd = ::accept(listen_fd_, reinterpret_cast<sockaddr *>(&caddr), &clen);
      if (cfd >= 0) {
        conn_fd_ = cfd;
        return true;
      }
      const int err = errno;
      if (HandleRetryableErrno(err)) {
        continue;
      }
      return false;
    }
    return false;
  }

  bool RecvOneMessage(CtrlMsgHeader &hdr, std::vector<uint8_t> &body) {
    if (!RecvAll(conn_fd_, &hdr, sizeof(hdr))) {
      return false;
    }

    if (hdr.body_size == 0 || hdr.body_size > (4ULL * 1024ULL * 1024ULL)) {
      return false;
    }

    body.assign(static_cast<size_t>(hdr.body_size), 0);
    if (!RecvAll(conn_fd_, body.data(), body.size())) {
      return false;
    }

    if (hdr.body_size < sizeof(CtrlMsgType)) {
      return false;
    }
    return true;
  }

  bool DispatchMessage(const std::vector<uint8_t> &body) {
    CtrlMsgType msg_type{};
    const void *src = static_cast<const void *>(body.data());
    error_t rc = memcpy_s(&msg_type, sizeof(msg_type), src, sizeof(msg_type));
    if (rc != EOK) {
      return false;
    }

    if (msg_type == CtrlMsgType::kCreateChannelReq) {
      HandleCreateChannel(conn_fd_);
      return true;
    }
    if (msg_type == CtrlMsgType::kGetRemoteMemReq) {
      HandleGetRemoteMem(conn_fd_);
      return true;
    }
    return false;
  }

  void HandleRequestsLoop() {
    while (!stop_.load()) {
      CtrlMsgHeader hdr{};
      std::vector<uint8_t> body;
      if (!RecvOneMessage(hdr, body)) {
        break;
      }
      if (!DispatchMessage(body)) {
        break;
      }
    }
  }

  void ThreadMain() {
    if (!SetupListenSocket()) {
      return;
    }
    if (!AcceptOnce()) {
      return;
    }
    if (stop_.load() || conn_fd_ < 0) {
      return;
    }
    HandleRequestsLoop();
  }

  void HandleCreateChannel(int fd) {
    CtrlMsgHeader resp_hdr{};
    resp_hdr.magic = kMagicNumber;
    resp_hdr.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + sizeof(CreateChannelResp));

    CtrlMsgType resp_type = CtrlMsgType::kCreateChannelResp;

    CreateChannelResp resp{};
    resp.result = SUCCESS;
    resp.dst_ep_handle = 0x12345678ULL;

    switch (connect_mode_) {
      case MiniSrvMode::kConnectResp_BadMagic:
        resp_hdr.magic = 0xDEADBEEF;
        break;
      case MiniSrvMode::kConnectResp_BadBodySize:
        resp_hdr.body_size += 1;
        break;
      case MiniSrvMode::kConnectResp_BadMsgType:
        resp_type = CtrlMsgType::kGetRemoteMemResp;
        break;
      case MiniSrvMode::kConnectResp_FailResult:
        resp.result = FAILED;
        break;
      default:
        break;
    }

    std::vector<uint8_t> body(static_cast<size_t>(resp_hdr.body_size), 0);
    size_t off = 0;

    errno_t rc = memcpy_s(body.data() + off, body.size() - off, &resp_type, sizeof(resp_type));
    if (rc != EOK) {
      return;
    }
    off += sizeof(resp_type);

    rc = memcpy_s(body.data() + off, body.size() - off, &resp, sizeof(resp));
    if (rc != EOK) {
      return;
    }

    (void)SendAll(fd, &resp_hdr, sizeof(resp_hdr));
    (void)SendAll(fd, body.data(), body.size());
  }

  std::string BuildGetRemoteMemJson() const {
    std::string json_str = GetRemoteMemJsonNormal();

    if (mem_resp_use_alt_ && mem_mode_ == MiniSrvMode::kNormal && get_mem_req_cnt_ >= kGetMemReqCnt) {
      json_str = GetRemoteMemJsonAlt();
    }

    ApplySpecialMemJsonByMode(json_str);
    return json_str;
  }

  static std::string GetRemoteMemJsonNormal() {
    return R"({
    "result": 0,
    "mem_descs": [
      { "tag": "_hixl_builtin_dev_trans_flag", "export_desc": [97,98,99,100], "mem": { "type": 0, "addr": 4096, "size": 8 } },
      { "tag": "a", "export_desc": [101,102,103,104], "mem": { "type": 0, "addr": 1, "size": 1 } },
      { "tag": "b", "export_desc": [105,106,107,108], "mem": { "type": 1, "addr": 2, "size": 1 } }
    ]
  })";
  }

  static std::string GetRemoteMemJsonAlt() {
    return R"({
    "result": 0,
    "mem_descs": [
      { "tag": "_hixl_builtin_dev_trans_flag", "export_desc": [109,110,111,112], "mem": { "type": 0, "addr": 8192, "size": 8 } },
      { "tag": "a", "export_desc": [113,114,115,116], "mem": { "type": 0, "addr": 11, "size": 2 } },
      { "tag": "b", "export_desc": [117,118,119,120], "mem": { "type": 1, "addr": 22, "size": 3 } }
    ]
  })";
  }

  void ApplySpecialMemJsonByMode(std::string &json_str) const {
    if (mem_mode_ == MiniSrvMode::kGetRemoteMemResp_EmptyList) {
      json_str = R"({"result":0,"mem_descs":[]})";
      return;
    }
    if (mem_mode_ == MiniSrvMode::kGetRemoteMemResp_ExportDescEmpty) {
      json_str = R"({"result":0,"mem_descs":[{"tag":"a","export_desc":[],"mem":{"type":0,"addr":1,"size":1}}]})";
      return;
    }
    if (mem_mode_ == MiniSrvMode::kGetRemoteMemResp_DuplicateAddr) {
      json_str =
          R"({"result":0,"mem_descs":[{"tag":"a","export_desc":[120,120],"mem":{"type":0,"addr":1,"size":1}},{"tag":"b","export_desc":[121,121],"mem":{"type":0,"addr":1,"size":1}}]})";
      return;
    }
    if (mem_mode_ == MiniSrvMode::kGetRemoteMemResp_MemImportFail) {
      json_str = R"({"result":0,"mem_descs":[{"tag":"a","export_desc":[70,65,73,76],"mem":{"type":0,"addr":1,"size":1}}]})";
    }
  }

  bool HandleGetRemoteMemExceptionModes(int fd, CtrlMsgHeader &resp_hdr, CtrlMsgType &resp_type,
                                        std::string &json_str) {
    switch (mem_mode_) {
      case MiniSrvMode::kGetRemoteMemResp_BadMagic:
        resp_hdr.magic = 0xABCD1234U;
        return false;

      case MiniSrvMode::kGetRemoteMemResp_BadMsgType:
        resp_type = CtrlMsgType::kCreateChannelResp;
        return false;

      case MiniSrvMode::kGetRemoteMemResp_BadBodySizeTooSmall:
        SendBodySizeTooSmall(fd, resp_hdr, resp_type);
        return true;

      case MiniSrvMode::kGetRemoteMemResp_BadJson:
        json_str = "{ this is not json";
        return false;

      case MiniSrvMode::kGetRemoteMemResp_MissingFieldResult:
        json_str = R"({ "mem_descs": [] })";
        return false;

      case MiniSrvMode::kGetRemoteMemResp_MissingFieldMemDescs:
        json_str = R"({ "result": 0 })";
        return false;

      case MiniSrvMode::kGetRemoteMemResp_FailResult:
        json_str = std::string("{\"result\":") + std::to_string(static_cast<uint32_t>(FAILED)) + ",\"mem_descs\":[]}";
        return false;

      default:
        return false;
    }
  }

  void SendBodySizeTooSmall(int fd, CtrlMsgHeader &resp_hdr, const CtrlMsgType &resp_type) {
    resp_hdr.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType));

    std::vector<uint8_t> body(sizeof(CtrlMsgType), 0);
    const errno_t rc = memcpy_s(body.data(), body.size(), &resp_type, sizeof(resp_type));
    if (rc != EOK) {
      return;
    }

    (void)SendAll(fd, &resp_hdr, sizeof(resp_hdr));
    (void)SendAll(fd, body.data(), body.size());
  }

  void SendGetRemoteMemResponse(int fd, CtrlMsgHeader &resp_hdr, const CtrlMsgType &resp_type,
                                const std::string &json_str) {
    resp_hdr.body_size = static_cast<uint64_t>(sizeof(CtrlMsgType) + json_str.size());

    std::vector<uint8_t> body(static_cast<size_t>(resp_hdr.body_size), 0);
    size_t off = 0;

    errno_t rc = memcpy_s(body.data() + off, body.size() - off, &resp_type, sizeof(resp_type));
    if (rc != EOK) {
      return;
    }
    off += sizeof(resp_type);

    if (!json_str.empty()) {
      rc = memcpy_s(body.data() + off, body.size() - off, json_str.data(), json_str.size());
      if (rc != EOK) {
        return;
      }
    }
    (void)SendAll(fd, &resp_hdr, sizeof(resp_hdr));
    (void)SendAll(fd, body.data(), body.size());
  }
  void HandleGetRemoteMem(int fd) {
    ++get_mem_req_cnt_;
    CtrlMsgHeader resp_hdr{};
    resp_hdr.magic = kMagicNumber;
    CtrlMsgType resp_type = CtrlMsgType::kGetRemoteMemResp;
    std::string json_str = BuildGetRemoteMemJson();
    if (HandleGetRemoteMemExceptionModes(fd, resp_hdr, resp_type, json_str)) {
      return;  // 已经发送完（例如 BadBodySizeTooSmall）
    }
    SendGetRemoteMemResponse(fd, resp_hdr, resp_type, json_str);
  }

 private:
  std::atomic<bool> stop_{false};
  uint32_t port_{0};
  std::thread worker_;
  int listen_fd_{-1};
  int conn_fd_{-1};

  int get_mem_req_cnt_{0};
  bool mem_resp_use_alt_{false};

  MiniSrvMode connect_mode_{MiniSrvMode::kNormal};
  MiniSrvMode mem_mode_{MiniSrvMode::kNormal};
};

static EndpointDesc MakeIdEp(uint32_t id) {
  EndpointDesc ep{};
  ep.protocol = COMM_PROTOCOL_UBC_CTP;
  ep.commAddr.type = COMM_ADDR_TYPE_ID;
  ep.commAddr.id = id;
  return ep;
}

class HixlCSClientUT : public ::testing::Test {
 protected:
  void SetUp() override {
    src_ = MakeIdEp(kSrcEpId);
    dst_ = MakeIdEp(kDstEpId);
  }

  void TearDown() override {
    (void)client_.Destroy();
    server_.Stop();
    port_ = kZero;
  }

  void StartServer(MiniSrvMode c, MiniSrvMode m) {
    server_.SetConnectMode(c);
    server_.SetGetRemoteMemMode(m);
    port_ = server_.Start();
    ASSERT_NE(port_, 0);
  }

  // [修改] 辅助函数增加 config 参数构造
  void CreateClient(const char *ip = "127.0.0.1") {
    ASSERT_NE(port_, 0);
    HixlClientConfig config{}; // 默认构造
    ASSERT_EQ(client_.Create(ip, port_, &src_, &dst_, &config), SUCCESS);
  }

  void ConnectClient(uint64_t timeout_us = kDefaultConnectTimeoutMs) {
    ASSERT_EQ(client_.Connect(timeout_us), SUCCESS);
  }

 protected:
  EndpointDesc src_{};
  EndpointDesc dst_{};
  MiniServer server_;
  uint16_t port_{0};
  HixlCSClient client_;
};

// ======================= Create 覆盖 =======================

TEST_F(HixlCSClientUT, CreateSuccess) {
  port_ = kPort;
  HixlClientConfig config{}; // [修改]
  EXPECT_EQ(client_.Create("127.0.0.1", port_, &src_, &dst_, &config), SUCCESS);
}

TEST_F(HixlCSClientUT, CreateFailNullServerIp) {
  port_ = kPort;
  HixlClientConfig config{}; // [修改]
  EXPECT_NE(client_.Create(nullptr, port_, &src_, &dst_, &config), SUCCESS);
}

TEST_F(HixlCSClientUT, CreateFailNullSrcEndpoint) {
  port_ = kPort;
  HixlClientConfig config{}; // [修改]
  EXPECT_NE(client_.Create("127.0.0.1", port_, nullptr, &dst_, &config), SUCCESS);
}

TEST_F(HixlCSClientUT, CreateFailNullDstEndpoint) {
  port_ = kPort;
  HixlClientConfig config{}; // [修改]
  EXPECT_NE(client_.Create("127.0.0.1", port_, &src_, nullptr, &config), SUCCESS);
}

TEST_F(HixlCSClientUT, ConnectFailWithoutCreate) {
  EXPECT_NE(client_.Connect(kConnectTime), SUCCESS);
}

TEST_F(HixlCSClientUT, ConnectFailDstEndpointReserved) {
  port_ = kPort;
  dst_.protocol = COMM_PROTOCOL_RESERVED;
  HixlClientConfig config{}; // [修改]
  ASSERT_EQ(client_.Create("127.0.0.1", port_, &src_, &dst_, &config), SUCCESS);
  EXPECT_EQ(client_.Connect(kConnectTime), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, ConnectFailNoServer) {
  // 只 Create，不启动 server，让 CtrlMsgPlugin::Connect 走失败/超时路径
  port_ = kPort;
  HixlClientConfig config{}; // [修改]
  ASSERT_EQ(client_.Create("127.0.0.1", port_, &src_, &dst_, &config), SUCCESS);
  EXPECT_NE(client_.Connect(kConnectTime1), SUCCESS);  // 50ms
}

TEST_F(HixlCSClientUT, ConnectSuccessNormal) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  EXPECT_EQ(client_.Connect(kDefaultConnectTimeoutMs), SUCCESS);
}

// -- CreateChannelResp 异常：magic/body_size/msg_type/result --

TEST_F(HixlCSClientUT, ConnectFailCreateChannelRespBadMagic) {
  StartServer(MiniSrvMode::kConnectResp_BadMagic, MiniSrvMode::kNormal);
  CreateClient();
  EXPECT_EQ(client_.Connect(kDefaultConnectTimeoutMs), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, ConnectFailCreateChannelRespBadBodySize) {
  StartServer(MiniSrvMode::kConnectResp_BadBodySize, MiniSrvMode::kNormal);
  CreateClient();
  EXPECT_EQ(client_.Connect(2ULL * 1000ULL * 1000ULL), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, ConnectFailCreateChannelRespBadMsgType) {
  StartServer(MiniSrvMode::kConnectResp_BadMsgType, MiniSrvMode::kNormal);
  CreateClient();
  EXPECT_EQ(client_.Connect(kDefaultConnectTimeoutMs), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, ConnectFailCreateChannelRespFailResult) {
  StartServer(MiniSrvMode::kConnectResp_FailResult, MiniSrvMode::kNormal);
  CreateClient();
  EXPECT_NE(client_.Connect(kDefaultConnectTimeoutMs), SUCCESS);
}

// Destroy 后再 Connect：覆盖 Destroy 清理后路径
TEST_F(HixlCSClientUT, ConnectFailAfterDestroy) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  EXPECT_EQ(client_.Destroy(), SUCCESS);
  EXPECT_NE(client_.Connect(kConnectTime2), SUCCESS);
}

// ======================= GetRemoteMem 覆盖 =======================

TEST_F(HixlCSClientUT, GetRemoteMemFailWithoutConnect) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  // 未 Connect，socket_ 还没连上 -> send/recv 会失败
  EXPECT_NE(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), SUCCESS);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailNullRemoteMemList) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  ConnectClient();

  char **tags = nullptr;
  uint32_t num = kZero;
  EXPECT_NE(client_.GetRemoteMem(nullptr, &tags, &num, kTimeOutOne), SUCCESS);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailNullListNum) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  EXPECT_NE(client_.GetRemoteMem(&remote, &tags, nullptr, kTimeOutOne), SUCCESS);
}

TEST_F(HixlCSClientUT, GetRemoteMemSuccessNormalWithTags) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), SUCCESS);
  EXPECT_NE(remote, nullptr);
  EXPECT_EQ(num, 3U);
  ASSERT_NE(tags, nullptr);
  EXPECT_STREQ(tags[0], kGetRemoteMemStr0);//0：第一个
  EXPECT_STREQ(tags[1], kGetRemoteMemStr1);//1：第二个
  EXPECT_STREQ(tags[2], kGetRemoteMemStr2);//2：第三个
}

TEST_F(HixlCSClientUT, GetRemoteMemSuccessNormalNoTagsOutParam) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  uint32_t num = kZero;

  EXPECT_NE(client_.GetRemoteMem(&remote, nullptr, &num, kTimeOutOne), SUCCESS);
}

TEST_F(HixlCSClientUT, GetRemoteMemSuccessEmptyList) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_EmptyList);
  CreateClient();
  ConnectClient();

  HcommMem *remote = &gRemoteSentinel;
  char **tags = gTagsSentinel;
  uint32_t num = kNumSentinel;

  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), SUCCESS);
  EXPECT_EQ(remote, nullptr);
  EXPECT_EQ(tags, nullptr);
  EXPECT_EQ(num, 0U);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailBadMagic) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_BadMagic);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), PARAM_INVALID);
  EXPECT_EQ(tags, nullptr);  // 失败时不应分配 tag
}

TEST_F(HixlCSClientUT, GetRemoteMemFailBadMsgType) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_BadMsgType);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailBadBodySizeTooSmall) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_BadBodySizeTooSmall);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailBadJson) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_BadJson);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailMissingFieldResult) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_MissingFieldResult);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailMissingFieldMemDescs) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_MissingFieldMemDescs);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), PARAM_INVALID);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailResultNotSuccess) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_FailResult);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  EXPECT_NE(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), SUCCESS);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailExportDescEmptyTriggersImportCheck) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_ExportDescEmpty);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  // 注意：如果你 ImportRemoteMem 在 export_desc 校验失败时没有释放 mems，会在 ASAN 下报 leak。
  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), PARAM_INVALID);
  EXPECT_EQ(remote, nullptr);
  EXPECT_EQ(tags, nullptr);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailDuplicateAddrRecordMemoryConflict) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_DuplicateAddr);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;
  // 传入重复server地址时，不报错，跳过内存记录
  EXPECT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), SUCCESS);
}

TEST_F(HixlCSClientUT, GetRemoteMemSuccessTwiceCoversClearRemoteMemInfo) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  ConnectClient();

  HcommMem *remote1 = nullptr;
  char **tags1 = nullptr;
  uint32_t num1 = 0;
  ASSERT_EQ(client_.GetRemoteMem(&remote1, &tags1, &num1, kTimeOutOne), SUCCESS);

  HcommMem *remote2 = nullptr;
  char **tags2 = nullptr;
  uint32_t num2 = 0;
  ASSERT_EQ(client_.GetRemoteMem(&remote2, &tags2, &num2, kTimeOutOne), SUCCESS);

  EXPECT_EQ(num2, 3U);
}

// 新增：第 2 次 GetRemoteMem 返回 alt JSON，校验“确实刷新了”
TEST_F(HixlCSClientUT, GetRemoteMemTwiceAltMemResponse) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  server_.EnableAltMemResp(true);
  CreateClient();
  ConnectClient();

  HcommMem *remote1 = nullptr;
  char **tags1 = nullptr;
  uint32_t num1 = 0;
  ASSERT_EQ(client_.GetRemoteMem(&remote1, &tags1, &num1, kTimeOutOne), SUCCESS);
  ASSERT_EQ(num1, 3U);
  ASSERT_NE(remote1, nullptr);
  // 第一次：flag addr=4096
  EXPECT_EQ(reinterpret_cast<uint64_t>(remote1[0].addr), 4096ULL);

  HcommMem *remote2 = nullptr;
  char **tags2 = nullptr;
  uint32_t num2 = kZero;
  ASSERT_EQ(client_.GetRemoteMem(&remote2, &tags2, &num2, kTimeOutOne), SUCCESS);
  ASSERT_EQ(num2, 3U);
  ASSERT_NE(remote2, nullptr);
  // 第二次：flag addr=8192（alt）
  EXPECT_EQ(reinterpret_cast<uint64_t>(remote2[0].addr), 8192ULL);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailAfterDestroy) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  ConnectClient();
  ASSERT_EQ(client_.Destroy(), SUCCESS);

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;
  EXPECT_NE(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), SUCCESS);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailMemImportFailedRollbackCovered) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_MemImportFail);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;

  Status st = client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne);

  // 期望：走到 MemImport failed 分支，返回非 SUCCESS（很多实现会直接把 st 原样返回）
  EXPECT_NE(st, SUCCESS);

  // 回滚路径应清理 out（避免半成品泄漏给上层）
  EXPECT_EQ(remote, nullptr);
  EXPECT_EQ(tags, nullptr);
}

TEST_F(HixlCSClientUT, GetRemoteMemFailTagCStringAllocFailedRollbackCovered) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kGetRemoteMemResp_MemImportFail);
  CreateClient();
  ConnectClient();

  HcommMem *remote = &gRemoteSentinel;
  char **tags = gTagsSentinel;
  uint32_t num = kNumSentinel;

  const Status st = client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne);
  EXPECT_NE(st, SUCCESS);
  EXPECT_EQ(remote, nullptr);
  EXPECT_EQ(tags, nullptr);
}

// ======================= Destroy 覆盖 =======================

TEST_F(HixlCSClientUT, DestroySuccessWithoutCreate) {
  EXPECT_EQ(client_.Destroy(), SUCCESS);
}

TEST_F(HixlCSClientUT, DestroySuccessIdempotent) {
  StartServer(MiniSrvMode::kNormal, MiniSrvMode::kNormal);
  CreateClient();
  ConnectClient();

  HcommMem *remote = nullptr;
  char **tags = nullptr;
  uint32_t num = kZero;
  ASSERT_EQ(client_.GetRemoteMem(&remote, &tags, &num, kTimeOutOne), SUCCESS);

  EXPECT_EQ(client_.Destroy(), SUCCESS);
  EXPECT_EQ(client_.Destroy(), SUCCESS);
}

}  // namespace hixl