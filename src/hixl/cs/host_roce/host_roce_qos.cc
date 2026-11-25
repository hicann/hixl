/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "cs/host_roce/host_roce_qos.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#ifdef HIXL_ENABLE_TEST_HOOKS
#include <mutex>
#endif
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "acl/acl.h"
#include "aclrt_proxy.h"
#include "common/hixl_checker.h"
#include "common/hixl_inner_types.h"
#include "common/hixl_log.h"
#include "common/scope_guard.h"

namespace hixl {
namespace {
constexpr uint32_t kDscpToTcShift = 2U;
constexpr uint32_t kDscpMax = 255U;
constexpr uint32_t kByteMask = 0xFFU;
constexpr uint32_t kDecimalRadix = 10U;
constexpr size_t kMinCfgLineLength = 3U;  // Minimal "k=v" pair length.
constexpr size_t kMaxQosPairs = 8U;
constexpr size_t kMaxCfgLine = 2113U;  // key(64) + '=' + value(2048) + NUL, matching HCCN cfg buffers
constexpr size_t kMaxLoggedCfg = 128U;

static_assert((static_cast<uint32_t>(kHostRoceDefaultDscp) << kDscpToTcShift) == kRdmaTrafficClass,
              "default DSCP must produce the default RoCE traffic class");

enum class QosParse : int32_t { kFound = 0, kMissing = 1, kInvalid = 2 };

#ifdef HIXL_ENABLE_TEST_HOOKS
std::mutex g_cfg_path_mu;
std::string g_cfg_path;
#endif

std::string CurrentCfgPath() {
#ifdef HIXL_ENABLE_TEST_HOOKS
  std::lock_guard<std::mutex> lock(g_cfg_path_mu);
  if (!g_cfg_path.empty()) {
    return g_cfg_path;
  }
#endif
  return kHostRoceQosCfgPath;
}

bool ParseDecimalDigit(char ch, uint32_t &digit) {
  switch (ch) {
    case '0':
      digit = 0U;
      return true;
    case '1':
      digit = 1U;
      return true;
    case '2':
      digit = 2U;
      return true;
    case '3':
      digit = 3U;
      return true;
    case '4':
      digit = 4U;
      return true;
    case '5':
      digit = 5U;
      return true;
    case '6':
      digit = 6U;
      return true;
    case '7':
      digit = 7U;
      return true;
    case '8':
      digit = 8U;
      return true;
    case '9':
      digit = 9U;
      return true;
    default:
      return false;
  }
}

bool ParseDecimal(const std::string &cfg, size_t &pos, uint32_t &out) {
  uint32_t digit = 0U;
  if (pos >= cfg.size() || !ParseDecimalDigit(cfg[pos], digit)) {
    return false;
  }
  uint32_t value = 0U;
  while (pos < cfg.size() && ParseDecimalDigit(cfg[pos], digit)) {
    if (value > (std::numeric_limits<uint32_t>::max() - digit) / kDecimalRadix) {
      return false;
    }
    value = value * kDecimalRadix + digit;
    ++pos;
  }
  out = value;
  return true;
}

QosParse ParsePair(const std::string &cfg, size_t &pos, uint32_t &qos, uint32_t &dscp) {
  if (!ParseDecimal(cfg, pos, qos) || pos >= cfg.size() || cfg[pos] != ':') {
    return QosParse::kInvalid;
  }
  ++pos;
  if (!ParseDecimal(cfg, pos, dscp) || qos > kQosMax || dscp > kDscpMax) {
    return QosParse::kInvalid;
  }
  return QosParse::kFound;
}

QosParse ParseQosDscp(const std::string &cfg, uint8_t qos, uint8_t &dscp) {
  size_t pos = 0U;
  for (size_t pair = 0U; pair < kMaxQosPairs; ++pair) {
    uint32_t cfg_qos = 0U;
    uint32_t cfg_dscp = 0U;
    const QosParse parsed = ParsePair(cfg, pos, cfg_qos, cfg_dscp);
    if (parsed != QosParse::kFound) {
      return parsed;
    }
    const bool at_end = (pos >= cfg.size());
    if (!at_end && cfg[pos] != ',') {
      return QosParse::kInvalid;
    }
    if (cfg_qos == qos) {
      dscp = static_cast<uint8_t>(cfg_dscp);
      return QosParse::kFound;
    }
    if (at_end) {
      return QosParse::kMissing;
    }
    ++pos;
  }
  return QosParse::kMissing;
}

void PublishMapping(uint32_t qos, uint8_t dscp, uint8_t &tc, uint8_t &sl) {
  sl = static_cast<uint8_t>(qos);
  tc = static_cast<uint8_t>((static_cast<uint32_t>(dscp) << kDscpToTcShift) & kByteMask);
  HIXL_LOGI("[HostRoce] Mapped business qos=%u to sl=%u and tc=%u, dscp=%u", qos, sl, tc, dscp);
}

bool TakeCfgValue(const char *line, const std::string &key, std::string &value) {
  if (line[0] == '#' || line[0] == '\0' || std::strlen(line) < kMinCfgLineLength) {
    return false;
  }
  const char *eq = std::strchr(line, '=');
  if (eq == nullptr || static_cast<size_t>(eq - line) != key.size() ||
      std::strncmp(line, key.c_str(), key.size()) != 0) {
    return false;
  }
  value.assign(eq + 1);
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
    value.pop_back();
  }
  return true;
}

void OpenCfg(const std::string &path, FILE *&fp) {
  fp = nullptr;
  char resolved[PATH_MAX] = {};
  if (realpath(path.c_str(), resolved) == nullptr) {
    const int saved_errno = errno;
    HIXL_LOGW("[HostRoce] qos_dscp file is unavailable, path=%s, errno=%d, error=%s", path.c_str(), saved_errno,
              strerror(saved_errno));
    return;
  }
  const int fd = open(resolved, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    const int saved_errno = errno;
    HIXL_LOGW("[HostRoce] Call api:open failed, path=%s, errno=%d, error=%s", resolved, saved_errno,
              strerror(saved_errno));
    return;
  }
  struct stat st {};
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
    const int saved_errno = errno;
    HIXL_LOGW("[HostRoce] qos_dscp path is not a regular file, path=%s, errno=%d", resolved, saved_errno);
    (void)close(fd);
    return;
  }
  fp = fdopen(fd, "r");
  if (fp == nullptr) {
    const int saved_errno = errno;
    HIXL_LOGW("[HostRoce] Call api:fdopen failed, path=%s, errno=%d, error=%s", resolved, saved_errno,
              strerror(saved_errno));
    (void)close(fd);
  }
}

Status ScanCfg(FILE *fp, const std::string &path, const std::string &key, std::string &value, bool &present) {
  char line[kMaxCfgLine] = {};
  while (fgets(line, static_cast<int>(sizeof(line)), fp) != nullptr) {
    const size_t length = std::strlen(line);
    if (length + 1U == sizeof(line) && line[length - 1U] != '\n') {
      HIXL_LOGE(PARAM_INVALID, "[HostRoce] qos_dscp line exceeds %zu bytes, path=%s", sizeof(line) - 1U, path.c_str());
      return PARAM_INVALID;
    }
    if (TakeCfgValue(line, key, value)) {
      present = true;
      return SUCCESS;
    }
  }
  if (ferror(fp) != 0) {
    const int saved_errno = errno;
    HIXL_LOGW("[HostRoce] Call api:fgets failed, path=%s, errno=%d, error=%s", path.c_str(), saved_errno,
              strerror(saved_errno));
  }
  return SUCCESS;
}

Status ReadQosValue(const std::string &path, uint32_t phy_id, std::string &value, bool &present) {
  present = false;
  value.clear();
  FILE *fp = nullptr;
  OpenCfg(path, fp);
  if (fp == nullptr) {
    return SUCCESS;
  }
  HIXL_MAKE_GUARD(close_cfg, ([&fp]() {
                    if (fp != nullptr) {
                      (void)fclose(fp);
                    }
                  }));
  return ScanCfg(fp, path, "qos_dscp_" + std::to_string(phy_id), value, present);
}

Status UseParsedDscp(QosParse parsed, uint32_t qos, uint32_t phy_id, const std::string &cfg, uint8_t &dscp) {
  if (parsed == QosParse::kFound) {
    return SUCCESS;
  }
  if (parsed == QosParse::kMissing) {
    dscp = kHostRoceDefaultDscp;
    HIXL_LOGW("[HostRoce] qos=%u is not in qos_dscp, phy_id=%u, use default dscp=%u", qos, phy_id, dscp);
    return SUCCESS;
  }
  const std::string shown = cfg.substr(0, kMaxLoggedCfg);
  HIXL_LOGE(PARAM_INVALID, "[HostRoce] invalid qos_dscp format, phy_id=%u, qos=%u, cfg=%s", phy_id, qos, shown.c_str());
  return PARAM_INVALID;
}

bool QueryPhyId(uint32_t &phy_id) {
  int32_t user_id = 0;
  const aclError dev_ret = aclrtGetDevice(&user_id);
  if (dev_ret != ACL_SUCCESS) {
    HIXL_LOGW("[HostRoce] Call api:aclrtGetDevice failed, ret=%d, use default dscp=%u", static_cast<int32_t>(dev_ret),
              kHostRoceDefaultDscp);
    return false;
  }
  int32_t phy = 0;
  const aclError phy_ret = AclrtProxy::GetPhyDevIdByUserDevId(user_id, &phy);
  if (phy_ret != ACL_SUCCESS || phy < 0) {
    HIXL_LOGW("[HostRoce] Call api:aclrtGetPhyDevIdByUserDevId failed, ret=%d, user_dev_id=%d, use default dscp=%u",
              static_cast<int32_t>(phy_ret), user_id, kHostRoceDefaultDscp);
    return false;
  }
  phy_id = static_cast<uint32_t>(phy);
  return true;
}
}  // namespace

Status ResolveHostRoceQos(uint32_t qos, uint32_t phy_id, const std::string &cfg_path, uint8_t &tc, uint8_t &sl) {
  HIXL_CHK_BOOL_RET_STATUS(qos <= kQosMax, PARAM_INVALID, "[HostRoce] qos=%u is outside [%u, %u]", qos,
                           static_cast<uint32_t>(kQosMin), static_cast<uint32_t>(kQosMax));
  uint8_t dscp = kHostRoceDefaultDscp;
  std::string value;
  bool present = false;
  const Status read = ReadQosValue(cfg_path, phy_id, value, present);
  if (read != SUCCESS) {
    return read;
  }
  if (!present || value.empty()) {
    HIXL_LOGW("[HostRoce] qos_dscp is empty, path=%s, phy_id=%u, qos=%u, use default dscp=%u", cfg_path.c_str(), phy_id,
              qos, dscp);
  } else {
    const Status parsed = UseParsedDscp(ParseQosDscp(value, static_cast<uint8_t>(qos), dscp), qos, phy_id, value, dscp);
    if (parsed != SUCCESS) {
      return parsed;
    }
  }
  PublishMapping(qos, dscp, tc, sl);
  return SUCCESS;
}

Status ApplyHostRoceQos(uint32_t qos, uint8_t &tc, uint8_t &sl) {
  if (qos == kHostRoceQosNotSet) {
    return SUCCESS;
  }
  HIXL_CHK_BOOL_RET_STATUS(qos <= kQosMax, PARAM_INVALID, "[HostRoce] qos=%u is outside [%u, %u]", qos,
                           static_cast<uint32_t>(kQosMin), static_cast<uint32_t>(kQosMax));
  uint32_t phy_id = 0U;
  if (!QueryPhyId(phy_id)) {
    PublishMapping(qos, kHostRoceDefaultDscp, tc, sl);
    return SUCCESS;
  }
  return ResolveHostRoceQos(qos, phy_id, CurrentCfgPath(), tc, sl);
}

#ifdef HIXL_ENABLE_TEST_HOOKS
void SetHostRoceQosCfgPathForTest(const std::string &path) {
  std::lock_guard<std::mutex> lock(g_cfg_path_mu);
  g_cfg_path = path;
}
#endif
}  // namespace hixl
