/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "channel_manager.h"
#include <sys/epoll.h>
#include <netinet/tcp.h>
#include <cstring>
#include <utility>
#include "common/mem_utils.h"
#include "common/llm_scope_guard.h"
#include "base/err_msg.h"

namespace adxl {
namespace {
constexpr int64_t kWaitTimeInMillis = 10000;
constexpr int64_t kSendMsgTimeout = 1000000;
constexpr int32_t kMaxEvents = 1024;
const size_t kRecvChunkSize = 4096;
constexpr int32_t kEpollWaitTimeInMillis = 1000;
}

int64_t ChannelManager::wait_time_in_millis_ = kWaitTimeInMillis;

Status ChannelManager::Initialize(BufferTransferService *buffer_transfer_service) {
  ADXL_CHK_ACL_RET(rtCtxGetCurrent(&rt_context_));
  buffer_transfer_service_ = buffer_transfer_service;
  epoll_fd_ = epoll_create1(0);
  if (epoll_fd_ == -1) {
    LLMLOGE(FAILED, "Failed to create epoll fd.");
    return FAILED;
  }
  // send heartbeat periodically
  heartbeat_sender_ = std::thread([this]() {
    std::unique_lock<std::mutex> lock(cv_mutex_);
    while (!stop_signal_.load()) {
      SendHeartbeats();
      cv_.wait_for(lock, std::chrono::milliseconds(wait_time_in_millis_), [this] { return stop_signal_.load(); });
    }
  });
  // receive msg thread
  msg_receiver_ = std::thread([this]() {
    rtCtxSetCurrent(rt_context_);
    while (!stop_signal_.load()) {
      HandleEpoolEvents();
      CheckHeartbeatTimeouts();
    }
  });
  return SUCCESS;
}

Status ChannelManager::AddSocketToEpoll(int32_t fd, ChannelPtr channel) {
  epoll_event event{};
  event.events = EPOLLIN;
  event.data.fd = fd;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
    LLMLOGE(FAILED, "Failed to add fd %d to epoll: %s", fd, strerror(errno));
    return FAILED;
  }

  std::lock_guard<std::mutex> lock(fd_mutex_);
  fd_to_channel_map_[fd] = std::move(channel);
  LLMLOGI("Successfully added fd %d to epoll", fd);
  return SUCCESS;
}

Status ChannelManager::HandleEpoolEvents() {
  struct epoll_event events[kMaxEvents];
  int nfds = epoll_wait(epoll_fd_, events, kMaxEvents, kEpollWaitTimeInMillis);
  if (nfds == -1) {
    LLMLOGE(FAILED, "epoll_wait error: %s", strerror(errno));
    return FAILED;
  }
  if (nfds == 0) {
    return SUCCESS;
  }
  for (int i = 0; i < nfds; ++i) {
    ADXL_CHK_STATUS_RET(HandleSocketEvent(events[i].data.fd), "Failed to handle socket event.");
  }
  return SUCCESS;
}

Status ChannelManager::HandleSocketEvent(int32_t fd) {
  ChannelPtr channel;
  {
    std::lock_guard<std::mutex> lock(fd_mutex_);
    auto it = fd_to_channel_map_.find(fd);
    if (it == fd_to_channel_map_.end()) {
      LLMLOGW("Channel not found for fd: %d", fd);
      return SUCCESS;
    }
    channel = it->second;
  }
  return HandleReadEvent(channel);
}

Status ChannelManager::HandleReadEvent(const ChannelPtr &channel) {
  int fd = channel->GetFd();
  if (channel->recv_buffer_.size() < channel->bytes_received_ + kRecvChunkSize) {
    channel->recv_buffer_.resize(channel->bytes_received_ + kRecvChunkSize);
  }
  ssize_t n = recv(fd, channel->recv_buffer_.data() + channel->bytes_received_,
                   channel->recv_buffer_.size() - channel->bytes_received_, 0);
  if (n == 0) {
    LLMLOGI("Connection closed by peer, fd: %d, channel:%s.", fd, channel->GetChannelId().c_str());
    return RemoveFd(fd);
  }
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return SUCCESS;
    }
    LLMLOGE(FAILED, "recv error on channel:%s, errno:%s", channel->GetChannelId().c_str(), strerror(errno));
    return RemoveFd(fd);
  }
  channel->bytes_received_ += n;
  return ProcessReceivedData(channel);
}

Status ChannelManager::ProcessReceivedData(const ChannelPtr &channel) {
  while (true) {
    if (channel->recv_state_ == RecvState::WAITING_FOR_HEADER) {
      if (channel->bytes_received_ < sizeof(ProtocolHeader)) {
        break;
      }
      auto *header = reinterpret_cast<ProtocolHeader *>(channel->recv_buffer_.data());
      if (header->magic != kMagicNumber) {
        LLMLOGE(FAILED, "Invalid magic number received on channel:%s.", channel->GetChannelId().c_str());
        return RemoveFd(channel->GetFd());
      }
      channel->expected_body_size_ = header->body_size;
      channel->recv_state_ = RecvState::WAITING_FOR_BODY;

      if (channel->bytes_received_ > sizeof(ProtocolHeader)) {
        size_t remaining = channel->bytes_received_ - sizeof(ProtocolHeader);
        memmove_s(channel->recv_buffer_.data(), remaining, channel->recv_buffer_.data() + sizeof(ProtocolHeader),
                  remaining);
        channel->bytes_received_ = remaining;
      } else {
        channel->bytes_received_ = 0;
      }
    }
    if (channel->recv_state_ == RecvState::WAITING_FOR_BODY) {
      if (channel->bytes_received_ < channel->expected_body_size_) {
        break;
      }
      ADXL_CHK_STATUS_RET(HandleControlMessage(channel),
                          "Failed to handle control message");

      if (channel->bytes_received_ > channel->expected_body_size_) {
        size_t remaining = channel->bytes_received_ - channel->expected_body_size_;
        memmove_s(channel->recv_buffer_.data(), remaining, channel->recv_buffer_.data() + channel->expected_body_size_,
                  remaining);
        channel->bytes_received_ = remaining;
        channel->recv_state_ = RecvState::WAITING_FOR_HEADER;
      } else {
        channel->bytes_received_ = 0;
        channel->recv_state_ = RecvState::WAITING_FOR_HEADER;
        break;
      }
    }
  }
  return SUCCESS;
}

Status ChannelManager::HandleControlMessage(const ChannelPtr &channel) {
  ADXL_CHK_BOOL_RET_STATUS(channel->expected_body_size_ > sizeof(ControlMsgType), FAILED,
                           "Received msg invalid, channel:%s.", channel->GetChannelId().c_str());
  auto data = channel->recv_buffer_.data();
  auto *msg_type = reinterpret_cast<ControlMsgType *>(data);
  std::string msg_str(data + sizeof(ControlMsgType), channel->expected_body_size_ - sizeof(ControlMsgType));
  if (*msg_type == ControlMsgType::kHeartBeat) {
    channel->UpdateHeartbeatTime();
    LLMLOGI("Heartbeat received from channel %s", channel->GetChannelId().c_str());
  } else if (*msg_type == ControlMsgType::kBufferReq) {
    BufferReq buffer_req{};
    ADXL_CHK_STATUS_RET(ControlMsgHandler::Deserialize(msg_str.c_str(), buffer_req), "Failed to deserialize msg");
    LLMLOGI("Recv buffer req for channel:%s", channel->GetChannelId().c_str());
    if (buffer_transfer_service_ != nullptr) {
      buffer_transfer_service_->PushBufferReq(channel, buffer_req);
    }
  } else if (*msg_type == ControlMsgType::kBufferResp) {
    BufferResp buffer_resp{};
    ADXL_CHK_STATUS_RET(ControlMsgHandler::Deserialize(msg_str.c_str(), buffer_resp), "Failed to deserialize msg");
    LLMLOGI("Recv buffer resp for channel:%s", channel->GetChannelId().c_str());
    if (buffer_transfer_service_ != nullptr) {
      buffer_transfer_service_->PushBufferResp(buffer_resp);
    }
    LLMLOGI("Recv ReverseTransferReq for channel:%s", channel->GetChannelId().c_str());
  } else {
    LLMLOGW("Unsupported msg type: %d", *msg_type);
  }
  return SUCCESS;
}

Status ChannelManager::Finalize() {
  stop_signal_.store(true);
  cv_.notify_all();
  if (heartbeat_sender_.joinable()) {
    heartbeat_sender_.join();
  }
  if (msg_receiver_.joinable()) {
    msg_receiver_.join();
  }

  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &it : channels_) {
    (void) it.second->Finalize();
  }
  channels_.clear();
  return SUCCESS;
}

void ChannelManager::SetHeartbeatWaitTime(int32_t time_in_millis) {
  wait_time_in_millis_ = time_in_millis;
}

std::vector<ChannelPtr> ChannelManager::GetAllClientChannel() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ChannelPtr> channels;
  for (const auto &it : channels_) {
    if (it.first.first == ChannelType::kClient) {
      channels.push_back(it.second);
    }
  }
  return channels;
}

std::vector<ChannelPtr> ChannelManager::GetAllServerChannel() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ChannelPtr> channels;
  for (const auto &it : channels_) {
    if (it.first.first == ChannelType::kServer) {
      channels.push_back(it.second);
    }
  }
  return channels;
}

void ChannelManager::SendHeartbeats() {
  auto channels = GetAllClientChannel();
  for (const auto &channel : channels) {
    HeartbeatMsg msg{};
    msg.msg = 'H';
    LLMLOGI("Start to send heartbeat msg to:%s.", channel->GetChannelId().c_str());
    (void)channel->SendControlMsg(
        [&msg](int32_t fd) { return ControlMsgHandler::SendMsg(fd, ControlMsgType::kHeartBeat, msg, kSendMsgTimeout);});
  }
}

Status ChannelManager::CreateChannel(const ChannelInfo &channel_info, ChannelPtr &channel_ptr) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &it = channels_.find(std::make_pair(channel_info.channel_type, channel_info.channel_id));
    ADXL_CHK_BOOL_RET_STATUS(it == channels_.cend(), ALREADY_CONNECTED,
                             "Channel already exists, channel_type = %d, channel id:%s",
                             static_cast<int32_t>(channel_info.channel_type), channel_info.channel_id.c_str());
  }
  ChannelPtr channel = llm::MakeShared<Channel>(channel_info);
  ADXL_CHECK_NOTNULL(channel);
  ADXL_CHK_STATUS_RET(channel->Initialize(), "Failed to init channel");
  LLM_DISMISSABLE_GUARD(failed_guard, ([channel]() { (void) channel->Finalize(); }));
  std::lock_guard<std::mutex> lock(mutex_);
  auto key = std::make_pair(channel_info.channel_type, channel_info.channel_id);
  const auto &it = channels_.find(key);
  ADXL_CHK_BOOL_RET_STATUS(it == channels_.cend(), ALREADY_CONNECTED,
                           "Channel already exists, channel_type = %d, channel id:%s",
                           static_cast<int32_t>(channel_info.channel_type), channel_info.channel_id.c_str());
  (void)channels_.emplace(std::make_pair(channel_info.channel_type, channel_info.channel_id), channel);
  channel_ptr = channel;
  LLMLOGI("Create channel success, channel_type = %d, channel id = %s",
         static_cast<int32_t>(channel_info.channel_type), channel_info.channel_id.c_str());
  LLM_DISMISS_GUARD(failed_guard);
  return SUCCESS;
}

void ChannelManager::CheckHeartbeatTimeouts() {
  std::vector<ChannelPtr> timeout_channels;
  auto channels = GetAllServerChannel();
  for (const auto &it : channels) {
    if (it->IsHeartbeatTimeout()) {
      timeout_channels.push_back(it);
    }
  }
  for (const auto &timeout_channel : timeout_channels) {
    LLMLOGI("Destroy timeout channel:%s.", timeout_channel->GetChannelId().c_str());
    (void) DestroyChannel(ChannelType::kServer, timeout_channel->GetChannelId());
  }
}

ChannelPtr ChannelManager::GetChannel(ChannelType channel_type, const std::string &channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto &it = channels_.find(std::make_pair(channel_type, channel_id));
  if (it != channels_.cend()) {
    return it->second;
  }
  return nullptr;
}

Status ChannelManager::DestroyChannel(ChannelType channel_type, const std::string &channel_id) {
  auto ret = SUCCESS;
  int32_t fd = -1;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto &it = channels_.find(std::make_pair(channel_type, channel_id));
    if (it != channels_.cend()) {
      auto channel = it->second;
      fd = channel->GetFd();
      (void)RemoveFd(fd);
      auto channel_ret = channel->Finalize();
      ret = channel_ret != SUCCESS ? channel_ret : ret;
      channels_.erase(it);
      LLMLOGI("Destroy channel end, channel_type = %d, channel_id = %s",
             static_cast<int32_t>(channel_type), channel_id.c_str());
    }
  }
  return ret;
}

Status ChannelManager::RemoveFd(int32_t fd) {
  auto ret = SUCCESS;
  if (fd != -1) {
    std::lock_guard<std::mutex> fd_lock(fd_mutex_);
    auto fd_it = fd_to_channel_map_.find(fd);
    if (fd_it != fd_to_channel_map_.end()) {
      fd_to_channel_map_.erase(fd_it);
      auto epoll_ret = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
      if (epoll_ret == -1) {
        LLMLOGW("Failed to remove fd %d from epoll: %s", fd, strerror(errno));
      }
    }
  }
  return ret;
}

}  // namespace adxl
