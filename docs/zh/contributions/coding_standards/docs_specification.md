# HIXL日志与文档规范

>  **适用场景**：本规范适用于HIXL仓所有代码中的日志打印，以及仓库内全部Markdown文档的检视。当PR修改涉及`.md`文件或日志输出时，必须执行本规范。

## 规范列表

| 规范编号 | 规范名称 | 类别 | 严重级别 |
|---------|---------|------|---------|
| 1.1 | 失败必须打印错误描述与关键信息 | 日志 | 高 |
| 1.2 | 调用非本组件接口失败须打印api名/错误码/参数 | 日志 | 中 |
| 1.3 | 外部参数校验和非本组件接口失败必须使用HIXL CHECK宏 | 日志 | 高 |
| 1.4 | 日志内容全部使用英文 | 日志 | 高 |
| 1.5 | 内容无语法拼写错误、完整准确、避免自创缩写 | 日志 | 中 |
| 1.6 | 度量信息须添加度量单位 | 日志 | 中 |
| 1.7 | 不得以明文记录敏感信息 | 日志 | 高 |
| 1.8 | 日志不得携带个人信息 | 日志 | 高 |
| 1.9 | 性能敏感流程不得记录run日志、生产环境不持续输出DEBUG | 日志 | 中 |
| 1.10 | 避免高频循环体内打印重复错误日志 | 日志 | 中 |
| 1.11 | 单行日志长度不超过1024字符 | 日志 | 中 |
| 2.1 | 文档表格与列表项须逐项完整 | 文档写作 | 中 |

## 说明

本规范日志部分（第1章）依据GitCode HIXL仓库Wiki「日志内容打印规范」，并结合HIXL PR检视流程中既有的日志检查项整理。文档写作规范（第2章）直接遵循GitCode CANN社区《文档写作规范》，不在本文件重复。

参与HIXL开发的开发者提交日志或文档相关变更时，首先需要遵循本规范内容，其余遵循CANN通用编码规范。如果对规则有异议，建议提交issue并说明理由，经CANN运营团队评审后可接纳并修改生效。

## 适用范围

HIXL相关开源仓的日志打印、Markdown文档、README、示例与脚本说明的检视。

---

### 1. 日志规范

#### 规则 1.1 失败必须打印错误描述与关键信息

执行出错或失败时，日志必须进行错误描述，并打印出可能造成该失败的关键信息，不得静默返回失败。

[反例]：未打印日志及关键信息

```cpp
auto ret = IpToInt(ip_info.ip.GetString(), llm_ip_info.ip);
if (ret != SUCCESS) {
  return FAILED;
}
```

[正例]：

```cpp
auto ret = IpToInt(ip_info.ip.GetString(), llm_ip_info.ip);
if (ret != SUCCESS) {
  LOGE(FAILED, "Failed to transfer ip to int, please check ip:%s is valid.", ip_info.ip.GetString());
  return FAILED;
}
```

#### 规则 1.2 调用非本组件接口失败须打印api名/错误码/参数

调用非本组件接口失败时，需要将调用的api名字、失败错误码以及参数打印出来，以便问题定位。非本组件接口包括HCCL、ACL、DSMI、DCMI、HCOMM、securec、JSON库、动态库加载接口，以及socket、poll、read、write、open等系统接口。参考格式`Call api:xxx failed, ret:xxx, param:xxx.`。

[反例]：未打印函数名及入参

```cpp
auto ret = aclrtSetDevice(device_id);
if (ret != ACL_ERROR_NONE) {
  LOGE(FAILED, "Failed to set device.");
  return FAILED;
}
```

[正例]：

```cpp
auto ret = aclrtSetDevice(device_id);
if (ret != ACL_ERROR_NONE) {
  LOGE(FAILED, "Call api:aclrtSetDevice failed, ret:%u, device_id:xxx.");
  return FAILED;
}
```

#### 规则 1.3 外部参数校验和非本组件接口失败必须使用HIXL CHECK宏

外部参数校验失败、调用非本组件接口失败（包含系统接口失败）时，必须优先使用HIXL CHECK宏统一处理返回、上报error msg并打印错误日志。错误信息必须包含可能导致当前错误的关键信息，例如参数名、参数值、合法范围、API名称、返回码、`errno`、`strerror(errno)`、设备ID、端口、fd、超时时间等。

常用宏包括`HIXL_CHECK_NOTNULL`、`HIXL_CHK_BOOL_RET_STATUS`、`HIXL_CHK_STATUS_RET`、`HIXL_CHK_ACL_RET`、`HIXL_CHK_HCCL_RET`等。不得只返回错误码，也不得只使用`HIXL_LOGE`而遗漏error msg上报。

[反例]：外部参数校验失败时只打印日志，未上报error msg

```cpp
if (client_desc == nullptr) {
  HIXL_LOGE(PARAM_INVALID, "client_desc is nullptr");
  return PARAM_INVALID;
}
```

[正例]：

```cpp
HIXL_CHECK_NOTNULL(client_desc);
HIXL_CHK_BOOL_RET_STATUS(port >= kMinPort && port <= kMaxPort, PARAM_INVALID,
                         "Invalid listen_port:%u, valid range is [%u, %u]", port, kMinPort, kMaxPort);
```

[反例]：系统接口失败时未使用HIXL CHECK宏，且缺少error msg上报

```cpp
int32_t ret = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
if (ret != 0) {
  HIXL_LOGE(FAILED, "setsockopt failed");
  return FAILED;
}
```

[正例]：

```cpp
int32_t ret = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
HIXL_CHK_BOOL_RET_STATUS(ret == 0, FAILED,
                         "Call api:setsockopt failed, ret:%d, fd:%d, option:SO_RCVTIMEO, error msg:%s, errno:%d",
                         ret, fd, strerror(errno), errno);
```

[正例]：ACL/HCCL等已有专用宏时直接使用专用宏

```cpp
HIXL_CHK_ACL_RET(aclrtSetDevice(device_id), "device_id:%u", device_id);
```

#### 规则 1.4 日志内容全部使用英文

日志内容全部使用英文描述，不得使用拼音、中文和中文符号。

#### 规则 1.5 内容无语法拼写错误、完整准确、避免自创缩写

日志内容不能存在语法、拼写错误，表达应完整准确、言简意赅，避免自创缩写。

#### 规则 1.6 度量信息须添加度量单位

日志中涉及度量信息（如耗时、数据量、带宽）需要添加度量单位。

#### 规则 1.7 不得以明文记录敏感信息

不能以明文的形式记录敏感信息，比如密码、秘钥、token等。

#### 规则 1.8 日志不得携带个人信息

日志内容中不得携带个人信息。

#### 规则 1.9 性能敏感流程不得记录run日志、生产环境不持续输出DEBUG

性能敏感流程中不能记录run日志（INFO级），例如KV传输流程中HIXL的数据面接口；生产环境不应出现DEBUG级别的持续输出。

#### 规则 1.10 避免高频循环体内打印重复错误日志

避免高频循环体内打印重复错误日志，否则将导致有用的错误信息被掩盖。

#### 规则 1.11 单行日志长度不超过1024字符

单行日志输出长度不得超过1024个字符。

---

### 2. 文档写作规范

文档（`.md`）写作规范直接遵循GitCode CANN社区[《文档写作规范》](https://gitcode.com/cann/community/blob/master/contributor/docs/document_writing_specs.md)。该规范涵盖文件命名、标题、字体样式、图片、代码块、列表、链接、锚点、表格、标点符号等全部文档写作要求（含“数字/单位/中英文之间不加空格、产品名称例外”“中文文档使用全角标点、数字半角”等规则）。检视文档变更时先以此为准（community规范不在此重复）。HIXL补充以下检查项：

#### 规则 2.1 文档表格与列表项须逐项完整

文档中的表格、列表，每一项必须有对应的说明内容，不得出现空单元格或缺失项。
