<!-- Copyright (c) 2026 Huawei Technologies Co., Ltd.
This program is free software, you can redistribute it and/or modify it under the terms and conditions of
CANN Open Software License Agreement Version 2.0 (the "License").
Please refer to the License for details. You may not use this file except in compliance with the License.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
See LICENSE in the root of the software repository for the full text of the License. -->

# 对外头文件 Doxygen 注释质量规范

> **适用场景**：`include/hixl/`、`include/adxl/`、`include/cs/`、
> `include/llm_datadist/` 下的 `.h` 文件中新增或修改的公共 API 函数声明。
> 仅检查 PR diff 中新增或修改的注释行，不追溯存量问题。

## 规范列表

| 编号 | 规范名称 | 严重级别 | 检查内容 |
|------|---------|---------|---------|
| HC-1 | 公共 API 函数必须有 Doxygen 块注释 | 中 | 所有公开函数声明必须有 `/** ... */` 块注释 |
| HC-2 | `@brief` 必须存在且格式一致 | 低 | 单空格 `* @brief`（非 `*  @brief`），描述简洁准确 |
| HC-3 | `@param` 必须标注方向 `[in]`/`[out]` | 中 | 所有参数行必须包含 `[in]` 或 `[out]` 标签 |
| HC-4 | 数值/时间/大小参数必须标注单位 | 中 | 如 `timeout`→`单位ms`、`size`→`单位byte` 等 |
| HC-5 | `@return` 必须存在且标点一致 | 低 | 列出成功和主要失败码，句尾标点在同一头文件内统一 |
| HC-6 | 结构体/枚举字段应有行内注释 | 低 | 公开结构体每个字段至少有 `//` 行内说明或 Doxygen 块 |
| HC-7 | 单位标注风格在同一头文件内一致 | 低 | 不混用 `单位ms` 和 `（ms）` |
| HC-8 | `@param` 参数名与函数原型严格一致 | 中 | `@param` 后的参数名必须与函数签名中的参数名完全匹配 |
| HC-9 | `@param` 描述不与参数名同义反复 | 低 | 不写"ptr 释放的ptr"这类冗余描述 |

## 检查范围

仅 `include/` 下四个模块目录中的 `.h` 对外头文件：

- `include/hixl/`
- `include/adxl/`
- `include/cs/`
- `include/llm_datadist/`

目录内新增的 `.h` 文件自动纳入检查范围，无需维护文件清单。

## 检查方法

1. 从 PR diff 中提取 `include/` 下 `.h` 文件的新增/修改行
2. 识别新增或修改的公共 API 函数声明（非 private/internal 的成员函数、
   非 `class` 内部实现）
3. 逐函数检查其 Doxygen 块注释是否符合 HC-1 至 HC-9
4. 仅检查 diff 范围内的内容，不追溯存量问题

## 报告反馈格式

对每条违规项，报告必须包含以下信息：

| 字段 | 说明 |
|------|------|
| 规范编号 | HC-1 至 HC-9 |
| 发现位置 | `<文件路径>:<行号>`，`<函数名>` |
| 当前内容 | 违规注释的原始代码片段 |
| 建议改为 | 修改后的正确代码片段 |

**示例输出**：

```
HC-3 | include/llm_datadist/llm_datadist.h:255 | CopyKvCache
当前内容:
  * @param src_cache 源Cache
  * @param dst_cache 目标Cache
建议改为:
  * @param [in] src_cache 源Cache
  * @param [out] dst_cache 目标Cache
```

给出修改建议时需遵循以下规则：

- 参照本文件"最佳实践示例"中标杆的格式
- 仅基于 diff 中的实际函数签名生成建议，不臆造参数名或类型
- 若一条违规涉及多个参数，在同一个建议块中合并给出
- 若无法确定正确值（如单位应取 byte 还是 MB），标注"待确认"并说明原因

## 最佳实践示例（标杆）

`hixl/hixl.h` 的 `Initialize`：

```cpp
/**
 * @brief 初始化Hixl, 在调用其他接口前需要先调用该接口
 * @param [in] local_engine Hixl的唯一标识，如果是ipv4格式为host_ip:host_port或host_ip,
 * 如果是ipv6格式为[host_ip]:host_port或[host_ip],
 * 当设置host_port且host_port > 0时代表当前Hixl作为server端，需要对配置端口进行监听
 * @param [in] options 初始化所需的选项
 * @return 成功:SUCCESS, 失败:其它.
 */
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options);
```

`adxl/adxl_engine.h` 的 `ExportToShareableHandle`（`@return` 列出具体错误码）：

```cpp
/**
 * @brief ...
 * @return 成功:SUCCESS, 地址非MallocMem申请或已释放:PARAM_INVALID, 失败:其它.
 */
```
