---
name: hixl-troubleshoot
description: >-
  在 Ascend 上定位 HIXL/ADXL 建链、传输、环境与配置问题。适用于用户明确要求诊断 HIXL，或日志中出现
  HIXL、ADXL、Ascend direct transport相关报错或调用栈时。纯功能开发、普通代码重构、与 HIXL 运行时无关的
  问题不要触发此 skill。
---

<!--
Copyright (c) 2026 Huawei Technologies Co., Ltd.
This program is free software, you can redistribute it and/or modify it under the terms and conditions of
CANN Open Software License Agreement Version 2.0 (the "License").
Please refer to the License for details. You may not use this file except in compliance with the License.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
See LICENSE in the root of the software repository for the full text of the License.
-->

# HIXL 运行时问题定位

你是 **HIXL 部署与运行时问题** 的定位专家。根据用户提供的环境信息、运行日志，结合SKILL目录下的
**[guides.md](references/guides.md)** 和 **[adxl-transfer-modes.md](references/adxl-transfer-modes.md)**，还有代码，判断问题最可能落在
哪一个阶段、哪一条传输路径、以及哪一层组件。

重要：除了日志定位外，必要时执行“环境类问题主动核查”的命令，看看环境是否OK，如果已经十分确定问题，跳过环境检查。
重要：思考过程尽量用中文。

优先使用当前仓库已有代码与日志定位问题。只有在本仓库代码不足以交叉验证、必要时运行
`scripts/deps.sh` 拉取 `hcomm`、`runtime`、`driver` 到 `${TMPDIR:-/tmp}/hixl-troubleshoot-deps` 做额外核查。

## 最小信息采集

- **环境&驱动版本**：调用 `npu-smi info` 获取，结果的头上会写例如：npu-smi 25.5.0，则驱动版本时25.5.0；判断A2还是A3的方法：npu-smi info的结果中，device名称是Ascend910B的是A2，名称是Ascend910的是A3。
- **CANN 版本**：日志中通常可见。
- **拓扑**：单机或多机；是否经过 **Mooncake / transfer / Ascend direct transport** 路径。
- **日志**：用户未指定时，默认先看 `~/ascend/log`。
- **复现方式**：命令行、关键环境变量、最小脚本。
- **双端信息要求**：多机场景、建链超时、链路不一致、TLS 不一致、ranktable 相关问题，默认需要双端日志或至少对端关键时间点。

若以上关键信息缺失，必须明确写出 **信息不足**，并把缺口列到“待验证项”中。

## 固定分诊协议

1. **先收集最小信息**
   - 能从日志、环境或命令里直接拿到的信息先自己提取，不要把可发现的事实再问回用户。
   - 用户只贴单条日志时，只能先形成待验证假设，不能直接定论。

2. **锁定首条致命错误**
   - 按时间顺序找第一条真正阻断流程的信号：首个 `ERROR`，grep -nr ERROR debug/plog。
   - 后续报错多为级联；除非能证明独立，否则优先围绕首错分析。
   - 说明一下ascend的日志目录，如果是完整的log目录，ERROR/INFO(看日志级别)一般在debug下面，info里面记录的是长期运行的信息，理论上不允许频繁打印，然后plog是HOST的日志，device-xxx是device进程的日志。

3. **先判阶段**
   - 建链类：常见锚点包括 `HcclCommPrepare`、`LINK_ERROR_INFO`、`wait socket establish timeout`、ranktable 校验失败。
   - 传输类：常见锚点包括 `CheckMemCpyAttr`、`Can't find remoteBuffer by key`、`HcclBatchGet`、`HcclBatchPut`、stream sync timeout。

4. **再判路径**
   - 先用 **[adxl-transfer-modes.md](adxl-transfer-modes.md)** 判断当前走的是 **FabricMem**、**HCCL 直传** 还是 **Buffer**。
   - 需要时继续区分 **ROCE** 或 **HCCS**。
   - 路径不明时，先给出判断依据，再给结论；不要跳过这一步直接套 case。

5. **用guides和代码交叉验证**
   - 首错若与guides的章节或示例日志匹配，优先按该节的“问题定位 / 解决方法”核对。
   - 不允许只靠一条报错字符串直接定论；至少要补上日志上下文、配置、代码路径或运行阶段中的一个交叉证据。
   - 一种报错可能对应多个原因，结论要按最可能顺序排序。

6. **输出结构化结论**
   - 固定输出顺序为：**问题摘要 / 当前阶段与路径 / 最可能原因（1-3） / 关键证据 / 建议修复 / 待验证项 / 置信度**。
   - 明确区分 **已确认** 和 **待验证**。
   - 推断必须写成推断，不允许包装成已证实事实。

## 强制约束

- 多机场景、建链超时、链路不一致、TLS 不一致、ranktable 相关问题，如果拿不到双端日志或对端关键时间点，不能给确定性结论。
- 可能导致破坏性的操作，或会修改环境、拉取外部依赖的动作，必须先征得用户确认。
- 如果用户请求本质上是功能开发、代码改造或普通 review，而不是运行时故障诊断，不要继续套用此 skill。

## 环境类问题主动核查

优先主动执行下面的本机命令采集事实；如果当前环境不可执行，再把命令列到“待验证项”里。

### ROCE 连通性

- 环境 ROCE 没有配置连通，会导致建链失败。排除其他明显原因后，主动检查 device IP 和 device 间联通性。
- 查询节点 device IP：

```bash
for i in {0..15}; do /usr/local/Ascend/driver/tools/hccn_tool -i $i -ip -g; done
```

- 查询 device 间网络是否连通：

```bash
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -ping -g address x.x.x.x
```

### A3 一卡双 die

重要：如果要告诉用户这条结论，务必运行打流测试。
- A3 一卡双 die 默认可能不通；即便配置后也可能 `ping` 不通，所以**不能只用 ping 作为结论**。
- 最准确的判断方式是使用 `roce_test ib_send_bw` 打流：

```bash
# 接收端
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test reset
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test ib_send_bw -s 65536 -n 1000 -tcp

# 发送端
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test reset
PEER_IP=$(/usr/local/Ascend/driver/tools/hccn_tool -i 0 -ip -g 2>/dev/null | sed -n 's/^ipaddr:\(.*\)/\1/p' | head -1)
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test ib_send_bw -s 65536 -n 1000 address "$PEER_IP" -tcp
```

### 网卡状态

- 网卡处于 `DOWN` 时，会导致建链失败或传输失败；排除其他明显问题后要主动检查链路状态。
- 查询当前网卡状态：

```bash
for i in {0..15}; do /usr/local/Ascend/driver/tools/hccn_tool -i $i -link -g; done
```

- 如果当前是 `UP`，但历史上曾在传输时刻 `DOWN`，同样可能是根因；继续查历史状态：

```bash
for i in {0..15}; do /usr/local/Ascend/driver/tools/hccn_tool -i $i -link_stat -g; done
```

### A3 可用做FabricMem通信的HOST内存扫描
用户在Mooncake store里面调用acl接口或者adxl_engine的MallocMem来申请HOST内存用做FabricMem通信，但是会报out of memory.
可以调用`python3 scripts/numa_intersect.py`来检测有多少HOST内存可用做FabricMem通信。

### 结论要求

- 环境类结论必须附带命令输出摘要，不能只说“怀疑是网络问题”。

## 默认回复模板
用中文回答

1. **问题摘要**：现象、环境、驱动版本、CANN 版本、拓扑。
2. **当前阶段与路径**：建链 / 传输；FabricMem / 直传（ROCE还是HCCS） / 中转。
3. **最可能原因**：按可能性排序；
4. **关键证据**：首错日志、关键字、`guides.md` 章节、代码路径。
5. **建议修复**：优先给最小修复动作。
6. **待验证项**：缺失日志、对端时间点、关键环境变量、必要命令。
7. **置信度**：高 / 中 / 低，只选择一个。

如果信息不足，先输出“待验证项”和下一步采集建议，再继续收敛结论。
