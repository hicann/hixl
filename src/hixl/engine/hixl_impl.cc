/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <mutex>
#include "hixl/hixl.h"
#include "common/hixl_checker.h"
#include "common/hixl_utils.h"
#include "adxl_engine.h"
#include "base/err_msg.h"
#include "engine.h"
#include "engine_factory.h"

namespace hixl {
namespace {
Status CheckTransferOpDescs(const std::vector<TransferOpDesc> &op_descs) {
  for (const auto &desc : op_descs) {
    auto local_addr = reinterpret_cast<void *>(desc.local_addr);
    auto remote_addr = reinterpret_cast<void *>(desc.remote_addr);
    HIXL_CHK_BOOL_RET_STATUS(local_addr != nullptr,
                             PARAM_INVALID, "local addr of desc can not be null.");
    HIXL_CHK_BOOL_RET_STATUS(remote_addr != nullptr,
                             PARAM_INVALID, "remote addr of desc can not be null.");
  }
  return SUCCESS;
}
}

class Hixl::HixlImpl {
 public:
  explicit HixlImpl(const AscendString &local_engine) : local_engine_(local_engine.GetString()) {}
  ~HixlImpl() = default;

  Status Initialize(const std::map<AscendString, AscendString> &options);

  void Finalize();

  Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle);

  Status DeregisterMem(MemHandle mem_handle);

  Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000);

  Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000);

  Status TransferSync(const AscendString &remote_engine,
                      TransferOp operation,
                      const std::vector<TransferOpDesc> &op_descs,
                      int32_t timeout_in_millis = 1000);

  Status TransferAsync(const AscendString &remote_engine,
                       TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req);

  Status GetTransferStatus(const TransferReq &req, TransferStatus &status);

  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, uint32_t timeout_in_millis);

  Status GetNotifies(std::vector<NotifyDesc> &notifies);

 private:
  std::mutex mutex_;
  std::string local_engine_;
  std::unique_ptr<Engine> engine_ = nullptr;
};

Status Hixl::HixlImpl::Initialize(const std::map<AscendString, AscendString> &options) {
  std::lock_guard<std::mutex> lk(mutex_);
  if (engine_ != nullptr) {
    HIXL_CHK_BOOL_RET_SPECIAL_STATUS(engine_->IsInitialized(), SUCCESS, "Already initialized");
  }
  engine_ = hixl::EngineFactory::CreateEngine(local_engine_, options);
  HIXL_CHECK_NOTNULL(engine_, "[HixlEngine] Created engine is null, please check your parameters! local_engine:%s", 
                     local_engine_.c_str());
  HIXL_CHK_STATUS_RET(engine_->Initialize(options), "Failed to initialize Hixl.");
  return SUCCESS;
}

void Hixl::HixlImpl::Finalize() {
  engine_->Finalize();
}

Status Hixl::HixlImpl::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_BOOL_RET_STATUS(reinterpret_cast<void *>(mem.addr) != nullptr,
                           PARAM_INVALID, "mem.addr can not be null");
  HIXL_CHK_STATUS_RET(engine_->RegisterMem(mem, type, mem_handle),
                      "Failed to register mem");
  return SUCCESS;
}

Status Hixl::HixlImpl::DeregisterMem(MemHandle mem_handle) {
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_BOOL_RET_STATUS(mem_handle != nullptr, PARAM_INVALID, "mem_handle can not be null");
  HIXL_CHK_STATUS_RET(engine_->DeregisterMem(mem_handle), "Failed to deregister mem");
  return SUCCESS;
}

Status Hixl::HixlImpl::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(engine_->Connect(remote_engine, timeout_in_millis), "Failed to connect");
  return SUCCESS;
}

Status Hixl::HixlImpl::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(engine_->Disconnect(remote_engine, timeout_in_millis), "Failed to disconnect");
  return SUCCESS;
}

Status Hixl::HixlImpl::TransferSync(const AscendString &remote_engine,
                                    TransferOp operation,
                                    const std::vector<TransferOpDesc> &op_descs,
                                    int32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(CheckTransferOpDescs(op_descs), "Failed to check transfer op descs");
  HIXL_CHK_STATUS_RET(engine_->TransferSync(remote_engine, operation,
                                            op_descs, timeout_in_millis),
                                            "Failed to transfer sync.");
  return SUCCESS;
}

Status Hixl::HixlImpl::TransferAsync(const AscendString &remote_engine,
                                     TransferOp operation,
                                     const std::vector<TransferOpDesc> &op_descs,
                                     const TransferArgs &optional_args,
                                     TransferReq &req) {
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized.");
  HIXL_CHK_STATUS_RET(CheckTransferOpDescs(op_descs), "Failed to check transfer op descs.");
  HIXL_CHK_STATUS_RET(engine_->TransferAsync(remote_engine, operation, 
                                             op_descs, optional_args, req),
                                             "Failed to transfer request async.");
  return SUCCESS;
}

Status Hixl::HixlImpl::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  TransferStatus transfer_status = TransferStatus::WAITING;
  auto ret = engine_->GetTransferStatus(req, transfer_status);
  if (ret != SUCCESS) {
    status = TransferStatus::FAILED;
    HIXL_LOGE(ret, "Failed to get transfer status.");
    return ret;
  }          
  status = transfer_status;
  return SUCCESS;
}

Status Hixl::HixlImpl::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, uint32_t timeout_in_millis) {
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(engine_->SendNotify(remote_engine, notify, timeout_in_millis), 
                      "Failed to send notify to remote engine:%s", remote_engine.GetString());
  return SUCCESS;
}

Status Hixl::HixlImpl::GetNotifies(std::vector<NotifyDesc> &notifies) {
  HIXL_CHK_BOOL_RET_STATUS(engine_->IsInitialized(), FAILED, "Hixl is not initialized");
  HIXL_CHK_STATUS_RET(engine_->GetNotifies(notifies), 
                      "Failed to get notifies");
  return SUCCESS;
}

Hixl::Hixl() {}

Hixl::~Hixl() {
  Finalize();
}

Status Hixl::Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options) {
  HIXL_LOGI("Hixl initialize start");
  impl_ = llm::MakeUnique<HixlImpl>(local_engine);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl construct");
  const auto ret = impl_->Initialize(options);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to initialize Hixl");
  HIXL_LOGI("Hixl initialized successfully");
  return SUCCESS;
}

void Hixl::Finalize() {
  HIXL_LOGI("Hixl finalize start");
  if (impl_ != nullptr) {
    impl_->Finalize();
    impl_.reset();
  }
  HIXL_LOGI("Hixl finalized successfully");
}

Status Hixl::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  HIXL_LOGI("RegisterMem start, type:%d, addr:%p, size:%zu",
         static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  const auto ret = impl_->RegisterMem(mem, type, mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to register mem, "
                           "type:%d, addr:%p, size:%lu",
                           static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len);
  HIXL_LOGI("RegisterMem success, type:%d, addr:%p, size:%zu, handle:%p",
            static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len, mem_handle);
  return SUCCESS;
}

Status Hixl::DeregisterMem(MemHandle mem_handle) {
  HIXL_LOGI("DeregisterMem start, mem_handle:%p", mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  const auto ret = impl_->DeregisterMem(mem_handle);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to deregister mem, mem_handle:%p",
                           mem_handle);
  HIXL_LOGI("DeregisterMem success, mem_handle:%p", mem_handle);
  return SUCCESS;
}

Status Hixl::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("Connect start, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->Connect(remote_engine, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to connect, remote engine:%s, timeout:%d ms",
                           remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("Connect success, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status Hixl::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  HIXL_LOGI("Disconnect start, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->Disconnect(remote_engine, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to disconnet, remote engine:%s, timeout:%d ms",
                           remote_engine.GetString(), timeout_in_millis);
  HIXL_LOGI("Disconnect success, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status Hixl::TransferSync(const AscendString &remote_engine,
                                TransferOp operation,
                                const std::vector<TransferOpDesc> &op_descs,
                                int32_t timeout_in_millis) {
  HIXL_LOGI("TransferSync start, remote_engine:%s, operation:%s, op_descs size:%zu, timeout:%d ms",
         remote_engine.GetString(), TransferOpToString(operation).c_str(), op_descs.size(), timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->TransferSync(remote_engine, operation, op_descs, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to TransferSync, remote_engine:%s, operation:%s, op_descs size:%zu, timeout:%d ms",
                           remote_engine.GetString(), TransferOpToString(operation).c_str(),
                           op_descs.size(), timeout_in_millis);
  HIXL_LOGI("TransferSync success, remote_engine:%s, operation:%s, op_descs size:%zu, timeout:%d ms",
          remote_engine.GetString(), TransferOpToString(operation).c_str(), op_descs.size(), timeout_in_millis);
  return SUCCESS;
}

Status Hixl::TransferAsync(const AscendString &remote_engine,
                           TransferOp operation,
                           const std::vector<TransferOpDesc> &op_descs,
                           const TransferArgs &optional_args,
                           TransferReq &req) {
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "HixlImpl is nullptr, check Hixl init.");
  const auto ret = impl_->TransferAsync(remote_engine, operation, op_descs, optional_args, req);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to transfer async, remote_engine:%s, operation:%s, op_descs size:%zu.",
                           remote_engine.GetString(), TransferOpToString(operation).c_str(), op_descs.size());
  HIXL_LOGI("Transfer async success, remote_engine:%s, operation:%s, op_descs size:%zu.",
            remote_engine.GetString(), TransferOpToString(operation).c_str(), op_descs.size());
  return SUCCESS;
}

Status Hixl::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "Impl is nullptr, check Hixl init.");
  HIXL_CHK_BOOL_RET_STATUS(req != nullptr, FAILED, "Req is nullptr, check req.");
  const auto ret = impl_->GetTransferStatus(req, status);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to get transfer status, req:%llu.", 
                           static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(req)));
  return SUCCESS;
}

Status Hixl::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  HIXL_LOGI("SendNotify start, remote engine:%s, notify name:%s", remote_engine.GetString(), notify.name.GetString());
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  constexpr uint32_t kMaxNotifyLength = 1024U;
  HIXL_CHK_BOOL_RET_STATUS(notify.name.GetLength() <= kMaxNotifyLength, PARAM_INVALID,
                           "notify.name length exceed max limit: %u, current: %zu", kMaxNotifyLength, notify.name.GetLength());
  HIXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(notify.notify_msg.GetLength() <= kMaxNotifyLength, PARAM_INVALID,
                           "notify.notify_msg length exceed max limit: %u, current: %zu", kMaxNotifyLength, notify.notify_msg.GetLength());
  const auto ret = impl_->SendNotify(remote_engine, notify, timeout_in_millis);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to send notify, remote engine:%s, notify name:%s",
                           remote_engine.GetString(), notify.name.GetString());
  HIXL_LOGI("SendNotify success, remote engine:%s, notify name:%s", remote_engine.GetString(), notify.name.GetString());
  return SUCCESS;
}

Status Hixl::GetNotifies(std::vector<NotifyDesc> &notifies) {
  HIXL_LOGI("GetNotifies start");
  HIXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check Hixl init");
  const auto ret = impl_->GetNotifies(notifies);
  HIXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to get notifies");
  HIXL_LOGI("GetNotifies success, got %zu notifies", notifies.size());
  return SUCCESS;
}
}  // namespace hixl