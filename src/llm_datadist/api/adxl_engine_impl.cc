/**
 * This program is free software, you can redistribute it and/or modify it.
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This file is a part of the CANN Open Software.
 * Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "adxl/adxl_engine.h"
#include <mutex>
#include "adxl/adxl_inner_engine.h"
#include "base/err_msg.h"

namespace adxl {
namespace {
Status CheckTransferOpDescs(const std::vector<TransferOpDesc> &op_descs) {
  for (const auto &desc : op_descs) {
    auto local_addr = reinterpret_cast<void *>(desc.local_addr);
    auto remote_addr = reinterpret_cast<void *>(desc.remote_addr);
    ADXL_CHK_BOOL_RET_STATUS(local_addr != nullptr,
                             PARAM_INVALID, "local addr of desc can not be null.");
    ADXL_CHK_BOOL_RET_STATUS(remote_addr != nullptr,
                             PARAM_INVALID, "remote addr of desc can not be null.");
  }
  return SUCCESS;
}
}

class AdxlEngine::AdxlEngineImpl {
 public:
  explicit AdxlEngineImpl(const AscendString &local_engine) : adxl_engine_(local_engine) {}
  ~AdxlEngineImpl() = default;

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

  Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis);

  Status GetNotifies(std::vector<NotifyDesc> &notifies);

 private:
  std::mutex mutex_;
  AdxlInnerEngine adxl_engine_;
};

Status AdxlEngine::AdxlEngineImpl::Initialize(const std::map<AscendString, AscendString> &options) {
  std::lock_guard<std::mutex> lk(mutex_);
  ADXL_CHK_BOOL_RET_SPECIAL_STATUS(adxl_engine_.IsInitialized(), SUCCESS, "Already initialized");
  ADXL_CHK_STATUS_RET(adxl_engine_.Initialize(options), "Failed to initialize AdxlEngine.");
  return SUCCESS;
}

void AdxlEngine::AdxlEngineImpl::Finalize() {
  adxl_engine_.Finalize();
}

Status AdxlEngine::AdxlEngineImpl::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  ADXL_CHK_BOOL_RET_STATUS(adxl_engine_.IsInitialized(), FAILED, "AdxlEngine is not initialized");
  ADXL_CHK_BOOL_RET_STATUS(reinterpret_cast<void *>(mem.addr) != nullptr,
                           PARAM_INVALID, "mem.addr can not be null");
  ADXL_CHK_STATUS_RET(adxl_engine_.RegisterMem(mem, type, mem_handle), "Failed to register mem");
  return SUCCESS;
}

Status AdxlEngine::AdxlEngineImpl::DeregisterMem(MemHandle mem_handle) {
  ADXL_CHK_BOOL_RET_STATUS(adxl_engine_.IsInitialized(), FAILED, "AdxlEngine is not initialized");
  ADXL_CHK_BOOL_RET_STATUS(mem_handle != nullptr, PARAM_INVALID, "mem_handle can not be null");
  ADXL_CHK_STATUS_RET(adxl_engine_.DeregisterMem(mem_handle), "Failed to deregister mem");
  return SUCCESS;
}

Status AdxlEngine::AdxlEngineImpl::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  ADXL_CHK_BOOL_RET_STATUS(adxl_engine_.IsInitialized(), FAILED, "AdxlEngine is not initialized");
  ADXL_CHK_STATUS_RET(adxl_engine_.Connect(remote_engine, timeout_in_millis), "Failed to connect");
  return SUCCESS;
}

Status AdxlEngine::AdxlEngineImpl::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  ADXL_CHK_BOOL_RET_STATUS(adxl_engine_.IsInitialized(), FAILED, "AdxlEngine is not initialized");
  ADXL_CHK_STATUS_RET(adxl_engine_.Disconnect(remote_engine, timeout_in_millis), "Failed to disconnect");
  return SUCCESS;
}

Status AdxlEngine::AdxlEngineImpl::TransferSync(const AscendString &remote_engine,
                                                TransferOp operation,
                                                const std::vector<TransferOpDesc> &op_descs,
                                                int32_t timeout_in_millis) {
  ADXL_CHK_BOOL_RET_STATUS(adxl_engine_.IsInitialized(), FAILED, "AdxlEngine is not initialized");
  ADXL_CHK_STATUS_RET(CheckTransferOpDescs(op_descs), "Failed to check transfer op descs");
  ADXL_CHK_STATUS_RET(adxl_engine_.TransferSync(remote_engine, operation, op_descs, timeout_in_millis),
                      "Failed to transfer sync.");
  return SUCCESS;
}

Status AdxlEngine::AdxlEngineImpl::TransferAsync(const AscendString &remote_engine,
                                                 TransferOp operation,
                                                 const std::vector<TransferOpDesc> &op_descs,
                                                 const TransferArgs &optional_args,
                                                 TransferReq &req) {
  ADXL_CHK_BOOL_RET_STATUS(adxl_engine_.IsInitialized(), FAILED, "Hixl is not initialized.");
  ADXL_CHK_STATUS_RET(CheckTransferOpDescs(op_descs), "Failed to check transfer op descs.");
  std::vector<adxl::TransferOpDesc> descs;
  for (const auto &desc : op_descs) {
    adxl::TransferOpDesc op_desc{};
    op_desc.local_addr = desc.local_addr;
    op_desc.remote_addr = desc.remote_addr;
    op_desc.len = desc.len;
    descs.emplace_back(op_desc);
  }
  adxl::TransferArgs args;
  memcpy_s(&args, sizeof(args), &optional_args, sizeof(optional_args));
  ADXL_CHK_STATUS_RET(adxl_engine_.TransferAsync(remote_engine, static_cast<adxl::TransferOp>(operation), 
                                                 descs, args, req),
                      "Failed to transfer async.");
  return SUCCESS;
}

Status AdxlEngine::AdxlEngineImpl::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  adxl::TransferStatus transfer_status = adxl::TransferStatus::WAITING;
  auto ret = adxl_engine_.GetTransferStatus(req, transfer_status);
  if (ret != SUCCESS) {
    status = TransferStatus::FAILED;
    LLMLOGE(ret, "Failed to get transfer request status.");
    return ret;
  }          
  status = static_cast<TransferStatus>(static_cast<int>(transfer_status));
  return SUCCESS;
}

Status AdxlEngine::AdxlEngineImpl::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  ADXL_CHK_BOOL_RET_STATUS(adxl_engine_.IsInitialized(), FAILED, "AdxlEngine is not initialized");
  ADXL_CHK_STATUS_RET(adxl_engine_.SendNotify(remote_engine, notify, timeout_in_millis), 
                      "Failed to send notify to remote engine:%s", remote_engine.GetString());
  return SUCCESS;
}

Status AdxlEngine::AdxlEngineImpl::GetNotifies(std::vector<NotifyDesc> &notifies) {
  ADXL_CHK_BOOL_RET_STATUS(adxl_engine_.IsInitialized(), FAILED, "AdxlEngine is not initialized");
  
  ADXL_CHK_STATUS_RET(adxl_engine_.GetNotifies(notifies), 
                      "Failed to get notifies");
  
  return SUCCESS;
}

AdxlEngine::AdxlEngine() {}

AdxlEngine::~AdxlEngine() {
  Finalize();
}

Status AdxlEngine::Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options) {
  LLMLOGI("AdxlEngine initialize start");
  impl_ = llm::MakeUnique<AdxlEngineImpl>(local_engine);
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check AdxlEngine construct");
  const auto ret = impl_->Initialize(options);
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to initialize AdxlEngine");
  LLMLOGI("AdxlEngine initialized successfully");
  return SUCCESS;
}

void AdxlEngine::Finalize() {
  LLMLOGI("AdxlEngine finalize start");
  if (impl_ != nullptr) {
    impl_->Finalize();
    impl_.reset();
  }
  LLMLOGI("AdxlEngine finalized successfully");
}

Status AdxlEngine::RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle) {
  LLMLOGI("RegisterMem start, type:%d, addr:%p, size:%zu",
         static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len);
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check AdxlEngine init");
  const auto ret = impl_->RegisterMem(mem, type, mem_handle);
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to register mem, "
                           "type:%d, addr:%p, size:%lu",
                           static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len);
  LLMLOGI("RegisterMem success, type:%d, addr:%p, size:%zu, handle:%p",
         static_cast<int32_t>(type), reinterpret_cast<void *>(mem.addr), mem.len, mem_handle);
  return SUCCESS;
}

Status AdxlEngine::DeregisterMem(MemHandle mem_handle) {
  LLMLOGI("DeregisterMem start, mem_handle:%p", mem_handle);
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check AdxlEngine init");
  const auto ret = impl_->DeregisterMem(mem_handle);
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret, "Failed to deregister mem, mem_handle:%p",
                           mem_handle);
  LLMLOGI("DeregisterMem success, mem_handle:%p", mem_handle);
  return SUCCESS;
}

Status AdxlEngine::Connect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  LLMLOGI("Connect start, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check AdxlEngine init");
  ADXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->Connect(remote_engine, timeout_in_millis);
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to connect, remote engine:%s, timeout:%d ms",
                           remote_engine.GetString(), timeout_in_millis);
  LLMLOGI("Connect success, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status AdxlEngine::Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis) {
  LLMLOGI("Disconnect start, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check AdxlEngine init");
  ADXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->Disconnect(remote_engine, timeout_in_millis);
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to disconnect, remote engine:%s, timeout:%d ms",
                           remote_engine.GetString(), timeout_in_millis);
  LLMLOGI("Disconnect success, remote engine:%s, timeout:%d ms", remote_engine.GetString(), timeout_in_millis);
  return SUCCESS;
}

Status AdxlEngine::TransferSync(const AscendString &remote_engine,
                                TransferOp operation,
                                const std::vector<TransferOpDesc> &op_descs,
                                int32_t timeout_in_millis) {
  auto start = std::chrono::steady_clock::now();
  LLMLOGI("TransferSync start, remote_engine:%s, operation:%d, op_descs size:%zu, timeout:%d ms",
          remote_engine.GetString(), static_cast<int32_t>(operation), op_descs.size(), timeout_in_millis);
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check AdxlEngine init");
  ADXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->TransferSync(remote_engine, operation, op_descs, timeout_in_millis);
  ADXL_CHK_BOOL_RET_STATUS(
      ret == SUCCESS, ret, "Failed to TransferSync, remote_engine:%s, operation:%d, op_descs size:%zu, timeout:%d ms",
      remote_engine.GetString(), static_cast<int32_t>(operation), op_descs.size(), timeout_in_millis);
  LLMLOGI("TransferSync success, remote_engine:%s, operation:%d, op_descs size:%zu, cost time: %ld us.",
          remote_engine.GetString(), static_cast<int32_t>(operation), op_descs.size(),
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
  return SUCCESS;
}
Status AdxlEngine::TransferAsync(const AscendString &remote_engine,
                                 TransferOp operation,
                                 const std::vector<TransferOpDesc> &op_descs,
                                 const TransferArgs &optional_args,
                                 TransferReq &req) {
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "Impl is nullptr, check Hixl init.");
  const auto ret = impl_->TransferAsync(remote_engine, operation, op_descs, optional_args, req);
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to transfer async, remote_engine:%s, operation:%d, op_descs size:%zu.",
                           remote_engine.GetString(), static_cast<int32_t>(operation), op_descs.size());
  LLMLOGI("Transfer async success, remote_engine:%s, operation:%d, op_descs size:%zu.",
          remote_engine.GetString(), static_cast<int32_t>(operation), op_descs.size());
  return SUCCESS;
}

Status AdxlEngine::GetTransferStatus(const TransferReq &req, TransferStatus &status) {
  ADXL_CHK_BOOL_RET_STATUS(req != nullptr, FAILED, "Req is nullptr, check req.");
  const auto ret = impl_->GetTransferStatus(req, status);
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                          "Failed to get transfer status, req:%llu.", 
                          static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(req)));
  return SUCCESS;
}

Status AdxlEngine::SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis) {
  LLMLOGI("SendNotify start, remote engine:%s, notify name:%s", remote_engine.GetString(), notify.name.GetString());
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check AdxlEngine init");
  constexpr uint32_t kMaxNotifyLength = 1024U;
  ADXL_CHK_BOOL_RET_STATUS(notify.name.GetLength() <= kMaxNotifyLength, PARAM_INVALID,
                           "notify.name length exceed max limit: %u, current: %zu", kMaxNotifyLength, notify.name.GetLength());
  ADXL_CHK_BOOL_RET_STATUS(notify.notify_msg.GetLength() <= kMaxNotifyLength, PARAM_INVALID,
                           "notify.notify_msg length exceed max limit: %u, current: %zu", kMaxNotifyLength, notify.notify_msg.GetLength());
  ADXL_CHK_BOOL_RET_STATUS(timeout_in_millis > 0, PARAM_INVALID, "timeout_in_millis:%d must > 0", timeout_in_millis);
  const auto ret = impl_->SendNotify(remote_engine, notify, timeout_in_millis);
  
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to send notify, remote engine:%s, notify name:%s",
                           remote_engine.GetString(), notify.name.GetString());
  LLMLOGI("SendNotify success, remote engine:%s, notify name:%s", remote_engine.GetString(), notify.name.GetString());
  return SUCCESS;
}

Status AdxlEngine::GetNotifies(std::vector<NotifyDesc> &notifies) {
  LLMLOGI("GetNotifies start");
  ADXL_CHK_BOOL_RET_STATUS(impl_ != nullptr, FAILED, "impl is nullptr, check AdxlEngine init");
  
  const auto ret = impl_->GetNotifies(notifies);
  
  ADXL_CHK_BOOL_RET_STATUS(ret == SUCCESS, ret,
                           "Failed to get notifies");
  LLMLOGI("GetNotifies success, got %zu notifies", notifies.size());
  return SUCCESS;
}
}  // namespace adxl
