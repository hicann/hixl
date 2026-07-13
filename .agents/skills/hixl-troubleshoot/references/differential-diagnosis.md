<!--
Copyright (c) 2026 Huawei Technologies Co., Ltd.
This program is free software, you can redistribute it and/or modify it under the terms and conditions of
CANN Open Software License Agreement Version 2.0 (the "License").
Please refer to the License for details. You may not use this file except in compliance with the License.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
See LICENSE in the root of the software repository for the full text of the License.
-->

# HIXL 鉴别诊断与证据门槛

Wiki 的关键词用于**生成候选假设**，不是根因结论。先运行
`log-triage.sh` 归并所有端的时间线，再用本表寻找能证实或证伪假设的证据。

## 结论门槛

| 等级 | 最低证据 | 可输出内容 |
|---|---|---|
| 已确认 | 关键日志的来源/时间/行号，至少一条判别证据；建链问题还需双端日志；代码判断有精确 ref 或明确标为日期近似 | 根因与修复建议；BUG 可指向文件/函数 |
| 高概率假设 | 单端完整 plog 或双端日志不完整，能排除部分候选 | 按优先级列出 1–2 个假设、已有/缺失证据与下一步 |
| 证据不足 | 只有截图、应用层报错，或没有关键 plog | 只请求日志和环境信息；不得下 BUG、网络或配置根因结论 |

建链类问题缺任一端日志时，`wait socket establish timeout`、`503900` 等对端可见症状只能作为
**高概率假设**。不要将“没有本端异常”解释为环境问题；先检查另一端在同一时间窗口的
DRV、HCCP、RUNTIME、HCCL 首错。

## 高歧义现象的候选假设

### `wait socket establish timeout` / `HcclCommPrepare`

| 候选假设 | 证实证据 | 反证或下一步 |
|---|---|---|
| 双端下发时间差超过 timeout | 同一 `comm[...]` 的两端 `Entry-HcclCommPrepare` 时间差大于日志内 timeout | 时间差小于 timeout 时排除；继续查同窗口对端首错 |
| 链路路径不一致 | `LINK_ERROR_INFO` 中一端使用 HCCS 虚拟 IP、另一端使用 RoCE IP，或 `HCCL_INTRA_ROCE_ENABLE` 不一致 | 双端路径和环境变量一致时排除 |
| TLS 配置不一致 | `TlsStatus` 或两端 `hccn_tool -tls -g` 结果不一致 | TLS 状态一致时排除 |
| 监听端口/容器端口资源冲突 | `listen on port ... fail`、`Address already in use`、同卡多进程 `device_port`/`listen_port` 冲突，或随机端口与 ranktable 不符 | 双端监听成功且 ranktable 端口一致时排除 |
| 对端资源或注册失败被包装为超时 | 超时前同一时间窗口的对端有 `DevMemAllocHugePageManaged`、`ibv_cmd_create_cq`、MR 注册或 DRV/HCCP 首错 | 对端无此类首错后才进入网络与配置排查 |

### `Failed to connect ... 503900` / `HcclCommPrepare ... ret[13]`

| 候选假设 | 证实证据 | 反证或下一步 |
|---|---|---|
| 用户 HBM 不足 | 对端先出现 `DevMemAllocHugePageManaged` 或 `halMemAlloc ... out of memory` | 只有 `ibv_cmd_create_cq ret 12` 时不是本项 |
| device 系统内存/CQ/QP 资源不足 | `ibv_cmd_create_cq failed, ret 12`、`create cq failed ret -259`、`qp create failed` 早于 HCCL 失败 | 未出现时检查注册和链路证据 |
| 内存注册参数或类型非法 | 对端有 `halShmem`、MR 注册、对齐、size 或 `HcclMemRegIpc` 首错 | 仅有 503900 不能推断为注册问题 |
| 对端未进入同一建链流程 | 双端无相同 `comm[...]`，或进入时间差超过 timeout | 两端已进入且资源正常时检查链路配置 |

### `stream sync timeout` / `RtStreamSynchronizeWithTimeout`

| 候选假设 | 证实证据 | 反证或下一步 |
|---|---|---|
| RDMA 重传超次或网络闪断 | `error cqe status`、HCCP/RoCE 重传错误、网卡历史 `DOWN` | 无重传证据时不应直接归因网络 |
| 业务超时小于 RDMA 重传窗口 | 日志/环境中传输 timeout 小于 `HCCL_RDMA_TIMEOUT` × retry 范围 | 调整窗口后仍复现时继续查传输任务链 |
| PFC 或拥塞导致性能退化 | 性能慢伴随重传统计，且 NPU/交换机 PFC/DSCP 配置不一致 | 只有一次超时、无重传或统计时不应套用 PFC 结论 |
| 上层传输任务未完成 | HIXL/ADXL 任务时间线早于 stream timeout 已有失败或未下发证据 | 先定位最早任务错误而不是只看最终同步超时 |

### `CheckMemCpyAttr`

| 候选假设 | 证实证据 | 反证或下一步 |
|---|---|---|
| 传输入参与真实内存类型/device ID 不匹配 | RUNTIME 报出的实际类型与接口传入类型不同 | 类型与 ID 一致时检查注册归属 |
| HOST 内存未被 RUNTIME 识别 | 未使用 `aclrtMallocHost`/`rtMallocHost` 或 torch 未 `pin_memory=True` | 已确认 pinned 时排除 |
| 在错误的 Hixl/AdxlEngine 实例注册 | 注册日志的实例/地址与传输调用实例不一致 | 同一实例且地址已注册时排除 |
| 使用未注册内存 | 注册范围不覆盖传输地址，或注册发生在传输/建链之后 | 地址与时序均覆盖时继续查接口属性 |

### `Can't find remoteBuffer by key`

| 候选假设 | 证实证据 | 反证或下一步 |
|---|---|---|
| 对端未注册或注册晚于建链 | 对端没有对应 `Add local mem range`，或其时间晚于建链/传输 | 地址、长度和注册时间匹配时排除 |
| HCCS 传输对端使用 HOST 内存 | HCCS 路径且对端注册 HOST 内存、未配置所需 RoCE 路径 | 路径和内存类型兼容时检查 key/生命周期 |
| 远端实例重建导致 key 失效 | 重建/Finalize 后仍复用旧 handle、context 或 key | 无生命周期事件时先查注册与地址 |

## 代码验证门槛

1. 先记录 `source-align.sh` 输出的仓库、SHA、对齐方式；`date-approximation` 不能用于断言“该版本已修复”。
2. 用 `git -C <repo> show <sha>:<file>`、`git -C <repo> grep <pattern> <sha> -- <path>` 阅读历史代码，避免修改共享依赖仓工作树。
3. BUG 结论必须同时给出：触发日志、历史/精确源码位置、关键变量来源，以及同类路径的横向比对。
4. 只有 master 当前代码、没有可对齐版本时，可报告“怀疑已有修复/需确认版本”，不能给出明确升级版本。

## 回复前检查

- 路径、阶段和最早相关错误是否都带 `source/file:line/timestamp`？
- 是否列出至少一个已排除候选，或说明为何证据不足无法排除？
- 建链问题是否已检查双端和同一 `comm[...]`？
- 环境结论是否有直接证据，而非由应用层超时反推？
- 代码 BUG 是否有对应源码 ref、文件和函数？
