/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_SRC_HIXL_CS_HIXL_CS_SERVER_H_
#define CANN_HIXL_SRC_HIXL_CS_HIXL_CS_SERVER_H_

#include <map>
#include <vector>
#include <mutex>
#include "common/hixl_cs.h"
#include "hixl/hixl_types.h"
#include "endpoint_store.h"
#include "msg_handler.h"
#include "msg_receiver.h"

namespace hixl {
struct EndpointMemInfo {
  EndpointHandle endpoint_handle;
  MemHandle mem_handle;
};

struct EndpointChannelInfo {
  EndpointHandle endpoint_handle;
  ChannelHandle channel_handle;
};

class HixlCSServer {
 public:
  HixlCSServer(const char *ip, uint32_t port) : ip_(ip), port_(port) {};

  ~HixlCSServer() = default;

  Status Initialize(const EndpointDesc *endpoint_list, uint32_t list_num, const HixlServerConfig *config);
  Status Finalize();
  Status RegisterMem(const char *mem_tag, const HcommMem *mem, MemHandle *mem_handle);
  Status DeregisterMem(MemHandle mem_handle);
  Status Listen(uint32_t backlog);
  Status RegProc(CtrlMsgType msg_type, MsgProcessor proc);

 private:
  template <typename T>
  static Status Serialize(const T &msg, std::string &msg_str);
  Status CreateChannel(int32_t fd, const char *msg, uint64_t msg_len);
  Status DestroyChannel(int32_t fd, const char *msg, uint64_t msg_len);
  Status ExportMem(int32_t fd, const char *msg, uint64_t msg_len);
  Status DoWait();
  void ProClientMsg(int32_t fd, std::shared_ptr<MsgReceiver> receiver);
  Status InitTransFinishedFlag();
  static Status SendCreateChannelResp(int32_t fd,
                                      const CreateChannelResp &resp);
  Status SendRemoteMemResp(int32_t fd,
                           const GetRemoteMemResp &resp);

  std::string ip_;
  uint32_t port_ = 0U;
  int32_t listen_fd_ = -1;
  int32_t epoll_fd_ = -1;
  std::atomic<bool> listener_running_{false};
  std::thread listener_;

  std::mutex client_mutex_;
  std::map<int32_t, std::shared_ptr<MsgReceiver>> clients_;

  std::mutex reg_mutex_;
  std::map<MemHandle, std::vector<EndpointMemInfo>> reg_mems_;

  std::mutex chn_mutex_;
  std::map<int32_t, EndpointChannelInfo> channels_;

  MsgHandler msg_handler_;
  EndpointStore endpoint_store_;

  void *trans_flag_ = nullptr;
  MemHandle trans_flag_handle_ = nullptr;

  void *host_trans_flag_ = nullptr;        // Host 侧 Flag 内存指针
  MemHandle host_trans_flag_handle_ = nullptr; // Host 侧 Flag 注册句柄

  void *dev_trans_flag_ = nullptr;         // Device 侧 Flag 内存指针
  MemHandle dev_trans_flag_handle_ = nullptr;  // Device 侧 Flag 注册句柄
};
}  // namespace hixl

#endif  // CANN_HIXL_SRC_HIXL_CS_HIXL_CS_SERVER_H_
