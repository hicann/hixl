/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hixl_py.h"

#include "Python.h"

#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include "hixl/hixl_types.h"
#include "common/hixl_checker.h"

#undef PYBIND11_CHECK_PYTHON_VERSION
#define PYBIND11_CHECK_PYTHON_VERSION

namespace hixl_py {
namespace py = pybind11;

HixlPy::HixlPy() : hixl_engine_(nullptr), initialized_(false) {}

HixlPy::~HixlPy() {
  if (initialized_ && hixl_engine_ != nullptr) {
    py::gil_scoped_release release;
    hixl_engine_->Finalize();
    hixl_engine_.reset();
    initialized_ = false;
  }
}

hixl::Status HixlPy::Initialize(const std::string &local_engine, const std::map<std::string, std::string> &options) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    HIXL_LOGI("Initialize: already initialized, ignoring repeated call");
    return hixl::SUCCESS;
  }
  auto instance = std::make_unique<hixl::Hixl>();
  hixl::AscendString ascend_local_engine(local_engine.c_str());
  std::map<hixl::AscendString, hixl::AscendString> ascend_options;
  for (const auto &opt : options) {
    (void)ascend_options.emplace(opt.first.c_str(), opt.second.c_str());
  }
  hixl::Status ret = instance->Initialize(ascend_local_engine, ascend_options);
  if (ret != hixl::SUCCESS) {
    HIXL_LOGE(ret, "Initialize: failed for engine '%s'", local_engine.c_str());
    return ret;
  }
  hixl_engine_ = std::move(instance);
  initialized_ = true;
  return hixl::SUCCESS;
}

void HixlPy::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return;
  }
  hixl_engine_->Finalize();
  hixl_engine_.reset();
  initialized_ = false;
}

std::pair<hixl::Status, uintptr_t> HixlPy::RegisterMem(const hixl::MemDesc &mem_desc, hixl::MemType mem_type) {
  hixl::MemHandle handle = nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return {hixl::FAILED, reinterpret_cast<uintptr_t>(handle)};
  }
  hixl::Status ret = hixl_engine_->RegisterMem(mem_desc, mem_type, handle);
  return {ret, reinterpret_cast<uintptr_t>(handle)};
}

hixl::Status HixlPy::DeregisterMem(uintptr_t mem_handle) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return hixl::FAILED;
  }
  hixl::MemHandle handle = reinterpret_cast<hixl::MemHandle>(mem_handle);
  return hixl_engine_->DeregisterMem(handle);
}

hixl::Status HixlPy::Connect(const std::string &remote_engine, int32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return hixl::FAILED;
  }
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->Connect(ascend_remote, timeout_ms);
}

hixl::Status HixlPy::Disconnect(const std::string &remote_engine, int32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return hixl::FAILED;
  }
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->Disconnect(ascend_remote, timeout_ms);
}

hixl::Status HixlPy::ConnectAsync(const std::string &remote_engine, int32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return hixl::FAILED;
  }
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->ConnectAsync(ascend_remote, timeout_ms);
}

hixl::Status HixlPy::DisconnectAsync(const std::string &remote_engine, int32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return hixl::FAILED;
  }
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->DisconnectAsync(ascend_remote, timeout_ms);
}

std::pair<hixl::Status, hixl::AsyncConnectStatus> HixlPy::GetAsyncConnectStatus(const std::string &remote_engine) {
  hixl::AsyncConnectStatus status = hixl::AsyncConnectStatus::NOT_CONNECT;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return {hixl::FAILED, status};
  }
  hixl::AscendString ascend_remote(remote_engine.c_str());
  hixl::Status ret = hixl_engine_->GetAsyncConnectStatus(ascend_remote, status);
  return {ret, status};
}

std::pair<hixl::Status, std::map<std::string, hixl::AsyncConnectStatus>> HixlPy::GetAllAsyncConnectStatus() {
  std::map<hixl::AscendString, hixl::AsyncConnectStatus> statuses;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return {hixl::FAILED, {}};
  }
  hixl::Status ret = hixl_engine_->GetAsyncConnectStatus(statuses);
  std::map<std::string, hixl::AsyncConnectStatus> result;
  for (const auto &s : statuses) {
    (void)result.emplace(std::string(s.first.GetString()), s.second);
  }
  return {ret, result};
}

hixl::Status HixlPy::TransferSync(const std::string &remote_engine, hixl::TransferOp operation,
                                  const std::vector<hixl::TransferOpDesc> &op_descs, int32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return hixl::FAILED;
  }
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->TransferSync(ascend_remote, operation, op_descs, timeout_ms);
}

std::pair<hixl::Status, uintptr_t> HixlPy::TransferAsync(const std::string &remote_engine, hixl::TransferOp operation,
                                                         const std::vector<hixl::TransferOpDesc> &op_descs,
                                                         hixl::TransferArgs args) {
  hixl::TransferReq req = nullptr;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return {hixl::FAILED, reinterpret_cast<uintptr_t>(req)};
  }
  hixl::AscendString ascend_remote(remote_engine.c_str());
  hixl::Status ret = hixl_engine_->TransferAsync(ascend_remote, operation, op_descs, args, req);
  return {ret, reinterpret_cast<uintptr_t>(req)};
}

std::pair<hixl::Status, hixl::TransferStatus> HixlPy::GetTransferStatus(uintptr_t req_id) {
  hixl::TransferStatus status = hixl::TransferStatus::FAILED;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return {hixl::FAILED, status};
  }
  hixl::TransferReq req = reinterpret_cast<hixl::TransferReq>(req_id);
  hixl::Status ret = hixl_engine_->GetTransferStatus(req, status);
  return {ret, status};
}

std::pair<hixl::Status, std::vector<hixl::TransferResult>> HixlPy::GetAllTransferStatus(
    hixl::GetTransferStatusArgs args) {
  std::vector<hixl::TransferResult> results;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return {hixl::FAILED, {}};
  }
  hixl::Status ret = hixl_engine_->GetTransferStatus(args, results);
  return {ret, results};
}

hixl::Status HixlPy::SendNotify(const std::string &remote_engine, const hixl::NotifyDesc &notify, int32_t timeout_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return hixl::FAILED;
  }
  hixl::AscendString ascend_remote(remote_engine.c_str());
  return hixl_engine_->SendNotify(ascend_remote, notify, timeout_ms);
}

std::pair<hixl::Status, std::vector<hixl::NotifyDesc>> HixlPy::GetNotifies() {
  std::vector<hixl::NotifyDesc> notifies;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_ || hixl_engine_ == nullptr) {
    return {hixl::FAILED, {}};
  }
  hixl::Status ret = hixl_engine_->GetNotifies(notifies);
  return {ret, notifies};
}

std::pair<hixl::Status, int32_t> HixlPy::GetCapability(hixl::FeatureType feature_type) {
  int32_t value = hixl::FEATURE_NOT_SUPPORTED;
  hixl::Status ret = hixl::Hixl::GetCapability(feature_type, value);
  return {ret, value};
}

static void RegisterConstants(py::module_ &m) {
  m.attr("SUCCESS") = py::int_(hixl::SUCCESS);
  m.attr("PARAM_INVALID") = py::int_(hixl::PARAM_INVALID);
  m.attr("TIMEOUT") = py::int_(hixl::TIMEOUT);
  m.attr("NOT_CONNECTED") = py::int_(hixl::NOT_CONNECTED);
  m.attr("ALREADY_CONNECTED") = py::int_(hixl::ALREADY_CONNECTED);
  m.attr("NOTIFY_FAILED") = py::int_(hixl::NOTIFY_FAILED);
  m.attr("UNSUPPORTED") = py::int_(hixl::UNSUPPORTED);
  m.attr("FAILED") = py::int_(hixl::FAILED);
  m.attr("RESOURCE_EXHAUSTED") = py::int_(hixl::RESOURCE_EXHAUSTED);

  m.attr("OPTION_ENABLE_USE_FABRIC_MEM") = py::str(hixl::OPTION_ENABLE_USE_FABRIC_MEM);
  m.attr("OPTION_RDMA_TRAFFIC_CLASS") = py::str(hixl::OPTION_RDMA_TRAFFIC_CLASS);
  m.attr("OPTION_RDMA_SERVICE_LEVEL") = py::str(hixl::OPTION_RDMA_SERVICE_LEVEL);
  m.attr("OPTION_BUFFER_POOL") = py::str(hixl::OPTION_BUFFER_POOL);
  m.attr("OPTION_GLOBAL_RESOURCE_CONFIG") = py::str(hixl::OPTION_GLOBAL_RESOURCE_CONFIG);
  m.attr("OPTION_AUTO_CONNECT") = py::str(hixl::OPTION_AUTO_CONNECT);
  m.attr("OPTION_LOCAL_COMM_RES") = py::str(hixl::OPTION_LOCAL_COMM_RES);

  m.attr("FEATURE_SUPPORTED") = py::int_(hixl::FEATURE_SUPPORTED);
  m.attr("FEATURE_NOT_SUPPORTED") = py::int_(hixl::FEATURE_NOT_SUPPORTED);
}

static void RegisterEnums(py::module_ &m) {
  py::enum_<hixl::MemType>(m, "MemType")
      .value("MEM_DEVICE", hixl::MEM_DEVICE)
      .value("MEM_HOST", hixl::MEM_HOST)
      .export_values();

  py::enum_<hixl::TransferOp>(m, "TransferOp").value("READ", hixl::READ).value("WRITE", hixl::WRITE).export_values();

  py::enum_<hixl::TransferStatus>(m, "TransferStatus")
      .value("WAITING", hixl::TransferStatus::WAITING)
      .value("COMPLETED", hixl::TransferStatus::COMPLETED)
      .value("TIMEOUT", hixl::TransferStatus::TIMEOUT)
      .value("FAILED", hixl::TransferStatus::FAILED);

  py::enum_<hixl::AsyncConnectStatus>(m, "AsyncConnectStatus")
      .value("NOT_CONNECT", hixl::AsyncConnectStatus::NOT_CONNECT)
      .value("CONNECT_PENDING", hixl::AsyncConnectStatus::CONNECT_PENDING)
      .value("CONNECTING", hixl::AsyncConnectStatus::CONNECTING)
      .value("CONNECTED", hixl::AsyncConnectStatus::CONNECTED)
      .value("CONNECT_FAILED", hixl::AsyncConnectStatus::CONNECT_FAILED)
      .value("DISCONNECT_PENDING", hixl::AsyncConnectStatus::DISCONNECT_PENDING)
      .value("DISCONNECTING", hixl::AsyncConnectStatus::DISCONNECTING)
      .export_values();

  py::enum_<hixl::FeatureType>(m, "FeatureType")
      .value("AUTO_CONNECT", hixl::AUTO_CONNECT)
      .value("CLIENT_SERVER_COMM", hixl::CLIENT_SERVER_COMM)
      .export_values();
}

static void RegisterDataClasses(py::module_ &m) {
  py::class_<hixl::MemDesc>(m, "MemDesc")
      .def(py::init<uintptr_t, size_t>(), py::arg("addr"), py::arg("len"))
      .def_readwrite("addr", &hixl::MemDesc::addr)
      .def_readwrite("len", &hixl::MemDesc::len);

  py::class_<hixl::TransferOpDesc>(m, "TransferOpDesc")
      .def(py::init<uintptr_t, uintptr_t, size_t>(), py::arg("local_addr"), py::arg("remote_addr"), py::arg("len"))
      .def_readwrite("local_addr", &hixl::TransferOpDesc::local_addr)
      .def_readwrite("remote_addr", &hixl::TransferOpDesc::remote_addr)
      .def_readwrite("len", &hixl::TransferOpDesc::len);

  py::class_<hixl::TransferArgs>(m, "TransferArgs")
      .def(py::init<>())
      .def_property(
          "user_data", [](const hixl::TransferArgs &a) { return reinterpret_cast<uintptr_t>(a.user_data); },
          [](hixl::TransferArgs &a, uintptr_t v) { a.user_data = reinterpret_cast<const void *>(v); });

  py::class_<hixl::GetTransferStatusArgs>(m, "GetTransferStatusArgs")
      .def(py::init<uint32_t, bool>(), py::arg("max_query_count") = UINT32_MAX, py::arg("skip_waiting") = false)
      .def_readwrite("max_query_count", &hixl::GetTransferStatusArgs::max_query_count)
      .def_readwrite("skip_waiting", &hixl::GetTransferStatusArgs::skip_waiting);

  py::class_<hixl::TransferResult>(m, "TransferResult")
      .def(py::init<>())
      .def_property(
          "req", [](const hixl::TransferResult &r) { return reinterpret_cast<uintptr_t>(r.req); },
          [](hixl::TransferResult &r, uintptr_t v) { r.req = reinterpret_cast<hixl::TransferReq>(v); })
      .def_property(
          "user_data", [](const hixl::TransferResult &r) { return reinterpret_cast<uintptr_t>(r.user_data); },
          [](hixl::TransferResult &r, uintptr_t v) { r.user_data = reinterpret_cast<const void *>(v); })
      .def_readonly("status", &hixl::TransferResult::status);

  py::class_<hixl::NotifyDesc>(m, "NotifyDesc")
      .def(py::init([](const std::string &name, const std::string &msg) {
             hixl::NotifyDesc n;
             n.name = hixl::AscendString(name.c_str());
             n.notify_msg = hixl::AscendString(msg.c_str());
             return n;
           }),
           py::arg("name"), py::arg("notify_msg"))
      .def_property(
          "name", [](const hixl::NotifyDesc &n) { return std::string(n.name.GetString()); },
          [](hixl::NotifyDesc &n, const std::string &s) { n.name = hixl::AscendString(s.c_str()); })
      .def_property(
          "notify_msg", [](const hixl::NotifyDesc &n) { return std::string(n.notify_msg.GetString()); },
          [](hixl::NotifyDesc &n, const std::string &s) { n.notify_msg = hixl::AscendString(s.c_str()); });
}

static void RegisterHixlEngine(py::module_ &m) {
  py::class_<HixlPy>(m, "Hixl")
      .def(py::init<>())
      .def("initialize", &HixlPy::Initialize, py::arg("local_engine"),
           py::arg("options") = std::map<std::string, std::string>{}, py::call_guard<py::gil_scoped_release>())
      .def("finalize", &HixlPy::Finalize, py::call_guard<py::gil_scoped_release>())
      .def("register_mem", &HixlPy::RegisterMem, py::call_guard<py::gil_scoped_release>())
      .def("deregister_mem", &HixlPy::DeregisterMem, py::call_guard<py::gil_scoped_release>())
      .def("connect", &HixlPy::Connect, py::arg("remote_engine"), py::arg("timeout_in_millis") = 1000,
           py::call_guard<py::gil_scoped_release>())
      .def("disconnect", &HixlPy::Disconnect, py::arg("remote_engine"), py::arg("timeout_in_millis") = 1000,
           py::call_guard<py::gil_scoped_release>())
      .def("connect_async", &HixlPy::ConnectAsync, py::arg("remote_engine"), py::arg("timeout_in_millis") = 1000,
           py::call_guard<py::gil_scoped_release>())
      .def("disconnect_async", &HixlPy::DisconnectAsync, py::arg("remote_engine"), py::arg("timeout_in_millis") = 1000,
           py::call_guard<py::gil_scoped_release>())
      .def("get_async_connect_status", &HixlPy::GetAsyncConnectStatus, py::call_guard<py::gil_scoped_release>())
      .def("get_all_async_connect_status", &HixlPy::GetAllAsyncConnectStatus, py::call_guard<py::gil_scoped_release>())
      .def("transfer_sync", &HixlPy::TransferSync, py::arg("remote_engine"), py::arg("op"), py::arg("op_descs"),
           py::arg("timeout_in_millis") = 1000, py::call_guard<py::gil_scoped_release>())
      .def("transfer_async", &HixlPy::TransferAsync, py::arg("remote_engine"), py::arg("op"), py::arg("op_descs"),
           py::arg("args") = hixl::TransferArgs{}, py::call_guard<py::gil_scoped_release>())
      .def("get_transfer_status", &HixlPy::GetTransferStatus, py::call_guard<py::gil_scoped_release>())
      .def("get_all_transfer_status", &HixlPy::GetAllTransferStatus, py::arg("args") = hixl::GetTransferStatusArgs{},
           py::call_guard<py::gil_scoped_release>())
      .def("send_notify", &HixlPy::SendNotify, py::arg("remote_engine"), py::arg("notify"),
           py::arg("timeout_in_millis") = 1000, py::call_guard<py::gil_scoped_release>())
      .def("get_notifies", &HixlPy::GetNotifies, py::call_guard<py::gil_scoped_release>());

  m.def("get_capability", &HixlPy::GetCapability, py::call_guard<py::gil_scoped_release>());
}

PYBIND11_MODULE(hixl, m) {
  RegisterConstants(m);
  RegisterEnums(m);
  RegisterDataClasses(m);
  RegisterHixlEngine(m);
}

}  // namespace hixl_py
