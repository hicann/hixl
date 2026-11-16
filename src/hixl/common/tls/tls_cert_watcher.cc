/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "tls_cert_watcher.h"

#include <chrono>
#include <cstring>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/inotify.h>
#include <libgen.h>
#include <vector>

#include "common/hixl_checker.h"
#include "common/hixl_log.h"
#include "common/tls/tls_context.h"

namespace hixl {
namespace {
constexpr int kDebounceMs = 100;
constexpr int kPollTimeoutMs = 200;
constexpr size_t kInotifyBufSize = 4096U;

struct WatchedFileNames {
  std::string cert_base;
  std::string key_base;
  std::string ca_base;
};

struct FileChangeState {
  bool cert_or_key_changed = false;
  bool ca_changed = false;
  std::chrono::steady_clock::time_point last_event = std::chrono::steady_clock::now();
};

std::string DirNameOf(const std::string &path) {
  std::vector<char> buf(path.begin(), path.end());
  buf.push_back('\0');
  const char *dir = dirname(buf.data());
  return dir != nullptr ? std::string(dir) : std::string(".");
}

std::string BaseNameOf(const std::string &path) {
  std::vector<char> buf(path.begin(), path.end());
  buf.push_back('\0');
  const char *base = basename(buf.data());
  return base != nullptr ? std::string(base) : path;
}

void ConsumeInotify(int inotify_fd, const WatchedFileNames &files, FileChangeState &changes) {
  char buf[kInotifyBufSize];
  const ssize_t len = read(inotify_fd, buf, sizeof(buf));
  ssize_t offset = 0;
  while (len > 0 && offset < len) {
    const auto *event = reinterpret_cast<const struct inotify_event *>(buf + offset);
    const std::string name = event->len > 0U ? std::string(event->name) : "";
    if (name == files.cert_base || name == files.key_base) {
      changes.cert_or_key_changed = true;
      changes.last_event = std::chrono::steady_clock::now();
    }
    if (!files.ca_base.empty() && name == files.ca_base) {
      changes.ca_changed = true;
      changes.last_event = std::chrono::steady_clock::now();
    }
    offset += static_cast<ssize_t>(sizeof(struct inotify_event) + event->len);
  }
}
}  // namespace

Status TlsCertWatcher::Create(const TlsConfig &config, ContextUpdate update, std::unique_ptr<TlsCertWatcher> &watcher) {
  auto created = std::unique_ptr<TlsCertWatcher>(new (std::nothrow) TlsCertWatcher());
  HIXL_CHK_BOOL_RET_STATUS(created != nullptr, FAILED, "[TlsCertWatcher] Failed to allocate watcher");
  created->config_ = config;
  created->update_ = std::move(update);
  watcher = std::move(created);
  return SUCCESS;
}

TlsCertWatcher::~TlsCertWatcher() {
  Stop();
}

Status TlsCertWatcher::AddDirWatch(const std::string &dir, int &watch_desc) {
  const uint32_t mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_ATTRIB;
  watch_desc = inotify_add_watch(inotify_fd_, dir.c_str(), mask);
  if (watch_desc >= 0) {
    HIXL_LOGD("[TlsCertWatcher] Watching directory, path:%s, watch_desc:%d", dir.c_str(), watch_desc);
    return SUCCESS;
  }
  const int saved_errno = errno;
  CloseWatchFds();
  HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "Call api:inotify_add_watch failed, path:%s, errno:%d, error_msg:%s",
                           dir.c_str(), saved_errno, strerror(saved_errno));
}

Status TlsCertWatcher::InitWatchFds() {
  if (pipe(wake_fd_) != 0) {
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "Call api:pipe failed, errno:%d, error_msg:%s", errno, strerror(errno));
  }
  inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
  if (inotify_fd_ < 0) {
    const int saved_errno = errno;
    CloseWatchFds();
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "Call api:inotify_init1 failed, errno:%d, error_msg:%s", saved_errno,
                             strerror(saved_errno));
  }
  HIXL_CHK_STATUS_RET(AddDirWatch(DirNameOf(config_.cert_file), cert_wd_), "[TlsCertWatcher] Failed to watch cert dir");
  HIXL_CHK_STATUS_RET(AddDirWatch(DirNameOf(config_.key_file), key_wd_), "[TlsCertWatcher] Failed to watch key dir");
  if (!config_.ca_file.empty()) {
    HIXL_CHK_STATUS_RET(AddDirWatch(DirNameOf(config_.ca_file), ca_wd_), "[TlsCertWatcher] Failed to watch CA dir");
  }
  return SUCCESS;
}

void TlsCertWatcher::CloseWatchFds() {
  if (inotify_fd_ >= 0) {
    (void)::close(inotify_fd_);
    inotify_fd_ = -1;
  }
  if (wake_fd_[0] >= 0) {
    (void)::close(wake_fd_[0]);
    wake_fd_[0] = -1;
  }
  if (wake_fd_[1] >= 0) {
    (void)::close(wake_fd_[1]);
    wake_fd_[1] = -1;
  }
}

Status TlsCertWatcher::Start() {
  HIXL_CHK_STATUS_RET(InitWatchFds(), "[TlsCertWatcher] Failed to init watch fds");
  running_ = true;
  try {
    watch_thread_ = std::thread([this]() { WatchLoop(); });
  } catch (const std::system_error &error) {
    running_ = false;
    CloseWatchFds();
    HIXL_CHK_BOOL_RET_STATUS(false, FAILED, "[TlsCertWatcher] Failed to start thread, error:%s", error.what());
  }
  HIXL_LOGI("[TlsCertWatcher] Started TLS file watcher, debounce:%d ms, poll_timeout:%d ms", kDebounceMs,
            kPollTimeoutMs);
  return SUCCESS;
}

void TlsCertWatcher::Stop() {
  const bool was_running = running_.exchange(false);
  if (wake_fd_[1] >= 0) {
    char byte = 'x';
    const auto ret = write(wake_fd_[1], &byte, 1);
    const int saved_errno = errno;
    if (ret < 0 && saved_errno != EAGAIN && saved_errno != EINTR) {
      HIXL_LOGW("Call api:write failed to wake TLS file watcher, ret:%ld, fd:%d, errno:%d, error_msg:%s", ret,
                wake_fd_[1], saved_errno, strerror(saved_errno));
    }
  }
  if (watch_thread_.joinable()) {
    watch_thread_.join();
  }
  if (was_running || inotify_fd_ >= 0 || wake_fd_[0] >= 0) {
    CloseWatchFds();
    HIXL_LOGI("[TlsCertWatcher] Stopped TLS file watcher");
  }
}

Status TlsCertWatcher::ReloadCertKey() {
  std::shared_ptr<TlsContext> ctx;
  const Status ret = TlsContext::Create(config_, ctx);
  if (ret != SUCCESS) {
    HIXL_LOGE(ret, "[TlsCertWatcher] Failed to reload cert/key, path:%s, keep previous certificates",
              config_.cert_file.c_str());
    return ret;
  }
  update_(std::move(ctx));
  HIXL_EVENT("[TlsCertWatcher] Reloaded cert/key successfully, cert_path:%s", config_.cert_file.c_str());
  return SUCCESS;
}

Status TlsCertWatcher::ReloadCa() {
  return ReloadCertKey();
}

void TlsCertWatcher::WatchLoop() {
  const WatchedFileNames files{BaseNameOf(config_.cert_file), BaseNameOf(config_.key_file),
                               config_.ca_file.empty() ? "" : BaseNameOf(config_.ca_file)};
  FileChangeState changes;
  while (running_) {
    struct pollfd fds[2];
    fds[0].fd = inotify_fd_;
    fds[0].events = POLLIN;
    fds[1].fd = wake_fd_[0];
    fds[1].events = POLLIN;
    const int pret = poll(fds, 2, kPollTimeoutMs);
    if (!running_) {
      break;
    }
    if (pret > 0 && (static_cast<uint32_t>(fds[0].revents) & static_cast<uint32_t>(POLLIN)) != 0U) {
      ConsumeInotify(inotify_fd_, files, changes);
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - changes.last_event)
            .count();
    if (changes.cert_or_key_changed && elapsed >= kDebounceMs) {
      HIXL_LOGI("[TlsCertWatcher] Reloading after cert/key change, ca_changed:%s",
                changes.ca_changed ? "true" : "false");
      (void)ReloadCertKey();
      changes.cert_or_key_changed = false;
      changes.ca_changed = false;
    } else if (changes.ca_changed && elapsed >= kDebounceMs) {
      HIXL_LOGI("[TlsCertWatcher] Reloading after CA change");
      (void)ReloadCa();
      changes.ca_changed = false;
    }
  }
}
}  // namespace hixl
