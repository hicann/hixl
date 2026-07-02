<!--
Copyright (c) 2026 Huawei Technologies Co., Ltd.
This program is free software, you can redistribute it and/or modify it under the terms and conditions of
CANN Open Software License Agreement Version 2.0 (the "License").
Please refer to the License for details. You may not use this file except in compliance with the License.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
See LICENSE in the root of the software repository for the full text of the License.
-->

# HIXL 传输路径总览

本文供 **hixl-troubleshoot** 分诊使用：先判 **引擎/路径**，再判 **阶段**。实现描述对齐当前 `hixl` 仓库源码，细节以代码为准。

性能慢 → Wiki [性能统计日志解读.md](https://gitcode.com/cann/hixl/wiki/性能统计日志解读.md)（**FabricMem** 与 **ADXL直传** 有 EVENT 级聚合统计；**hixl_cs 当前仓库暂无** 同类聚合统计，见 §0 表格「性能聚合统计」列）。

---

## 0. 先判路径

| 路径 | 触发条件 / 入口 | 源码 | 典型日志 | 性能聚合统计 |
|------|-----------------|------|----------|--------------|
| **FabricMem 引擎** | `OPTION_ENABLE_USE_FABRIC_MEM=1` | `src/hixl/engine/fabric_mem_engine.cc`、`src/hixl/fabric_mem/` | `[FabricMemEngine]`、`Fabric mem transfer statistic info` | 有（`FabricMemStatistic`） |
| **ADXL直传** | 默认 | `src/llm_datadist/adxl/` | `AdxlInnerEngine`、`HcclCommPrepare`、`Connect statistic info`、`Direct transfer statistic info` | 有（`StatisticManager`） |
| **HIXL_CS** | 配置了protocol_desc，或者version:1.3| `src/hixl/cs/` | `[HixlClient]`、`[HixlServer]`、`[HixlCSServer]` | **当前代码无** |

**快速 grep：**

```bash
grep -rniE "\[FabricMemEngine\]|Fabric mem transfer statistic" ~/ascend/log
grep -rniE "AdxlInnerEngine|Connect statistic info|Direct transfer statistic|HcclCommPrepare" ~/ascend/log
grep -rniE "\[HixlClient\]|\[HixlServer\]|HixlCSClient|HixlCSServer" ~/ascend/log
```

---

## 1. 路径判断（决策树）

```mermaid
flowchart TD
  log[plog 首错]
  cs{"[HixlClient] 或 HixlCSClient?"}
  fab{"[FabricMemEngine] 或 Fabric mem transfer statistic?"}
  adxl{"AdxlInnerEngine 或 Connect statistic?"}
  csPath[HIXL_CS]
  fabPath[FabricMemEngine]
  hcclPath[ADXL HCCL 直传]

  log --> cs
  cs -->|yes| csPath
  cs -->|no| fab
  fab -->|yes| fabPath
  fab -->|no| adxl
  adxl --> hcclPath
```
