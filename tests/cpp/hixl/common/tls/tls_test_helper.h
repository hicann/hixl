/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CANN_HIXL_TESTS_CPP_HIXL_COMMON_TLS_TLS_TEST_HELPER_H_
#define CANN_HIXL_TESTS_CPP_HIXL_COMMON_TLS_TLS_TEST_HELPER_H_

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>

#include "common/tls/tls_config.h"

namespace hixl {
namespace test {

inline std::string MakeTempDir() {
  char tmpl[] = "/tmp/hixl_tls_XXXXXX";
  char *dir = mkdtemp(tmpl);
  if (dir == nullptr) {
    return "";
  }
  return dir;
}

inline bool RunOpenssl(const std::string &cmd) {
  return std::system(cmd.c_str()) == 0;
}

inline bool GenerateSelfSignedCert(const std::string &dir, const std::string &cert_name = "cert.pem",
                                   const std::string &key_name = "key.pem") {
  const std::string cert = dir + "/" + cert_name;
  const std::string key = dir + "/" + key_name;
  const std::string cmd = "openssl req -x509 -newkey rsa:2048 -keyout " + key + " -out " + cert +
                          " -days 1 -nodes -subj /CN=localhost >/dev/null 2>&1";
  return RunOpenssl(cmd);
}

inline std::string ReadFileToString(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return "";
  }
  return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

inline TlsConfig MakeTlsConfigFromFiles(const std::string &dir, const std::string &cert_name,
                                        const std::string &key_name) {
  TlsConfig cfg;
  cfg.configured = true;
  cfg.cert_file = dir + "/" + cert_name;
  cfg.ca_file = cfg.cert_file;
  cfg.key_pem = ReadFileToString(dir + "/" + key_name);
  return cfg;
}

inline TlsConfig MakeTlsConfig(const std::string &dir) {
  return MakeTlsConfigFromFiles(dir, "cert.pem", "key.pem");
}

inline bool GeneratePkcs1Cert(const std::string &dir) {
  const std::string cert = dir + "/cert_pkcs1.pem";
  const std::string key = dir + "/key_pkcs1.pem";
  const std::string cmd = "openssl genrsa -traditional -out " + key +
                          " 2048 >/dev/null 2>&1 && openssl req -new -x509 -key " + key + " -out " + cert +
                          " -days 1 -subj /CN=localhost >/dev/null 2>&1";
  return RunOpenssl(cmd);
}

inline bool GenerateEcCert(const std::string &dir) {
  const std::string cert = dir + "/cert_ec.pem";
  const std::string key = dir + "/key_ec.pem";
  const std::string cmd = "openssl ecparam -name prime256v1 -genkey -noout -out " + key +
                          " >/dev/null 2>&1 && openssl req -new -x509 -key " + key + " -out " + cert +
                          " -days 1 -subj /CN=localhost >/dev/null 2>&1";
  return RunOpenssl(cmd);
}

inline void RemovePath(const std::string &path) {
  (void)unlink(path.c_str());
  (void)rmdir(path.c_str());
}

}  // namespace test
}  // namespace hixl

#endif  // CANN_HIXL_TESTS_CPP_HIXL_COMMON_TLS_TLS_TEST_HELPER_H_
