

# HIXL CS 接口说明<a name="ZH-CN_TOPIC_0000002446743579"></a>

## 产品支持情况

<a name="table38301303189"></a>

| 产品 | 是否支持 |
|---|---:|
| Ascend 950PR/Ascend 950DT | √ |

## HixlStatus 与返回码<a name="ZH-CN_TOPIC_CS_HIXLSTATUS"></a>

接口返回码类型与常量。

```
typedef uint32_t HixlStatus;
static const uint32_t HIXL_SUCCESS = 0U;
static const uint32_t HIXL_PARAM_INVALID = 103900U;
static const uint32_t HIXL_TIMEOUT = 103901U;
static const uint32_t HIXL_FAILED = 503900U;
```

具体错误码含义如下。

<a name="table124618224416"></a>
<table><thead align="left"><tr id="row833920317342"><th class="cellrowborder" valign="top" width="34.26342634263426%" id="mcps1.1.5.1.1"><p id="p324682215414"><a name="p324682215414"></a><a name="p324682215414"></a>枚举值</p>
</th>
<th class="cellrowborder" valign="top" width="18.421842184218423%" id="mcps1.1.5.1.2"><p id="p132471122448"><a name="p132471122448"></a><a name="p132471122448"></a>含义</p>
</th>
<th class="cellrowborder" valign="top" width="14.151415141514152%" id="mcps1.1.5.1.3"><p id="p26947289405"><a name="p26947289405"></a><a name="p26947289405"></a>是否可恢复</p>
</th>
<th class="cellrowborder" valign="top" width="33.16331633163316%" id="mcps1.1.5.1.4"><p id="p1385143414013"><a name="p1385143414013"></a><a name="p1385143414013"></a>解决办法</p>
</th>
</tr>
</thead>
<tbody><tr id="row579015813543"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p67911835418"><a name="p67911835418"></a><a name="p67911835418"></a>HIXL_SUCCESS</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p117912815412"><a name="p117912815412"></a><a name="p117912815412"></a>成功</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p7694192815408"><a name="p7694192815408"></a><a name="p7694192815408"></a>无</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1785153464014"><a name="p1785153464014"></a><a name="p1785153464014"></a>不涉及。</p>
</td>
</tr>
<tr id="row024710221414"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p6114134464315"><a name="p6114134464315"></a><a name="p6114134464315"></a>HIXL_PARAM_INVALID</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p201143445431"><a name="p201143445431"></a><a name="p201143445431"></a>参数错误</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p2011444434319"><a name="p2011444434319"></a><a name="p2011444434319"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p2114154420439"><a name="p2114154420439"></a><a name="p2114154420439"></a>基于日志排查错误原因。</p>
</td>
</tr>
<tr id="row1612782310553"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1044310442234"><a name="p1044310442234"></a><a name="p1044310442234"></a>HIXL_TIMEOUT</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p17443344192314"><a name="p17443344192314"></a><a name="p17443344192314"></a>处理超时</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p844384413234"><a name="p844384413234"></a><a name="p844384413234"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1443444102314"><a name="p1443444102314"></a><a name="p1443444102314"></a>保留现场，获取Host/Device日志，并备份。</p>
</td>
</tr>
<tr id="row1791926194512"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p19683111558"><a name="p19683111558"></a><a name="p19683111558"></a>HIXL_FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p102471228410"><a name="p102471228410"></a><a name="p102471228410"></a>通用失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p56940282403"><a name="p56940282403"></a><a name="p56940282403"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p585113454013"><a name="p585113454013"></a><a name="p585113454013"></a>保留现场，获取Host/Device日志，并备份。</p>
</tr>
</tbody>
</table>

## 句柄类型<a name="ZH-CN_TOPIC_CS_HANDLES"></a>

句柄类型定义。

```
typedef void *HixlServerHandle;
typedef void *HixlClientHandle;
typedef void *CompleteHandle;
typedef void *MemHandle;
```

## 结构体说明<a name="ZH-CN_TOPIC_CS_STRUCTS"></a>

以下是 CS API 中使用到的结构体定义。

### HixlServerConfig<a name="ZH-CN_TOPIC_CS_HIXLServerConfig"></a>

Server 配置保留字段，用于未来扩展。

```
struct HixlServerConfig {
  uint8_t reserved[128] = {};
};
```

### HixlClientConfig<a name="ZH-CN_TOPIC_CS_HIXLClientConfig"></a>

Client 配置保留字段，用于未来扩展。

```
struct HixlClientConfig {
  uint8_t reserved[128] = {};
};
```

### HixlClientDesc<a name="ZH-CN_TOPIC_CS_HIXLClientDesc"></a>

Client 描述信息。

```
struct HixlClientDesc {
  const char *server_ip;
  uint32_t server_port;
  const EndpointDesc *local_endpoint;
  const EndpointDesc *remote_endpoint;
};
```

字段说明：

| 字段 | 类型 | 描述 |
|---|---|---|
| server_ip | const char* | 目标 server Host IP。|
| server_port | uint32_t | 目标 server Host 端口。|
| local_endpoint | const EndpointDesc* | 本端Endpoint描述（定义见 https://gitcode.com/cann/hcomm/blob/master/include/hccl/hccl_res.h ）。|
| remote_endpoint | const EndpointDesc* | 远端Endpoint描述（定义见 https://gitcode.com/cann/hcomm/blob/master/include/hccl/hccl_res.h ）。|

### HixlServerDesc<a name="ZH-CN_TOPIC_CS_HIXLServerDesc"></a>

Server 描述信息。

```
struct HixlServerDesc {
  const char *server_ip;
  uint32_t server_port;
  const EndpointDesc *endpoint_list;
  uint32_t endpoint_list_num;
};
```

字段说明：

| 字段 | 类型 | 描述 |
|---|---|---|
| server_ip | const char* | 监听 IP。|
| server_port | uint32_t | 监听端口。|
| endpoint_list | const EndpointDesc* | Endpoint描述数组指针。Endpoint描述定义见 https://gitcode.com/cann/hcomm/blob/master/include/hccl/hccl_res.h|
| endpoint_list_num | uint32_t | Endpoint数量。|

### HixlOneSideOpDesc<a name="ZH-CN_TOPIC_CS_HIXLOneSideOpDesc"></a>

单向操作描述（用于批量 put/get）。

```
struct HixlOneSideOpDesc {
  void *remote_buf;
  void *local_buf;
  uint64_t len;
};
```

字段说明：

| 字段 | 类型 | 描述 |
|---|---|---|
| remote_buf | void* | 远端（server）数据地址。|
| local_buf | void* | 本端（client）数据地址。|
| len | uint64_t | 传输长度（字节）。|
## 枚举

异步任务完成状态定义：

```
enum HixlCompleteStatus {
  HIXL_COMPLETE_STATUS_WAITING,
  HIXL_COMPLETE_STATUS_COMPLETED,
  HIXL_COMPLETE_STATUS_TIMEOUT,
  HIXL_COMPLETE_STATUS_FAILED
};

说明：
其中HIXL_COMPLETE_STATUS_TIMEOUT和HIXL_COMPLETE_STATUS_FAILED当前为预留字段。
```

## 接口说明

每个接口包含：函数功能、函数原型、参数说明、返回值、调用示例与约束说明。

### HixlCSServerCreate

**函数功能**

创建并初始化 Server 实例。

**函数原型**

```
HixlStatus HixlCSServerCreate(const HixlServerDesc *server_desc,
                              const HixlServerConfig *config,
                              HixlServerHandle *server_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_servercreate_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">server_desc</td><td class="cellrowborder">输入</td><td class="cellrowborder">Server 描述信息，包含监听 IP/端口与端点列表。</td></tr>
<tr><td class="cellrowborder">config</td><td class="cellrowborder">输入</td><td class="cellrowborder">server配置，预留配置，暂未使用。</td></tr>
<tr><td class="cellrowborder">server_handle</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回创建的 `HixlServerHandle`。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败或错误码（见常量）。

**约束说明**

- 如果使用本地device的Endpoint，调用该接口前需要先调用“aclrtSetDevice”。
- 调用失败时 `server_handle` 不保证有效。

### HixlCSServerRegMem

**函数功能**

Server 注册共享内存供 Client 访问。

**函数原型**

```
HixlStatus HixlCSServerRegMem(HixlServerHandle server_handle,
                              const char *mem_tag,
                              const HcommMem *mem,
                              MemHandle *mem_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_serverregmem_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">server_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">Server 实例句柄。</td></tr>
<tr><td class="cellrowborder">mem_tag</td><td class="cellrowborder">输入</td><td class="cellrowborder">内存标签字符串，用于标识此内存，可选，可传入为NULL。</td></tr>
<tr><td class="cellrowborder">mem</td><td class="cellrowborder">输入</td><td class="cellrowborder">指向 `HcommMem`，描述内存位置与大小（定义见 https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h ）。</td></tr>
<tr><td class="cellrowborder">mem_handle</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回的内存句柄，用于后续注销。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：注册成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 在client调用HixlCSClientConnect与本端建链之前需要完成所有本地内存的注册。
- 使用host RoCE网卡当前不支持注册“aclrtMallocHost”申请出来的内存，可使用malloc等方式。
- 注册Device内存使用“aclrtMalloc”进行申请。

### HixlCSServerListen

**函数功能**

启动 Server 监听，接受来自 Client 的连接。

**函数原型**

```
HixlStatus HixlCSServerListen(HixlServerHandle server_handle, uint32_t backlog);
```

**参数说明**

<a name="zh-cn_topic_cs_serverlisten_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">server_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">Server 实例句柄。</td></tr>
<tr><td class="cellrowborder">backlog</td><td class="cellrowborder">输入</td><td class="cellrowborder">连接队列最大长度。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 在client调用HixlCSClientConnect与本端建链之前需要完成监听。

### HixlCSServerUnregMem

**函数功能**

注销之前注册的内存。

**函数原型**

```
HixlStatus HixlCSServerUnregMem(HixlServerHandle server_handle, MemHandle mem_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_serverunregmem_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">server_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">Server 实例句柄。</td></tr>
<tr><td class="cellrowborder">mem_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">需注销的内存句柄（HixlCSServerRegMem 返回）。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 确保远端不再访问该内存后，且与当前server建链的client均断链以后再注销。

### HixlCSServerDestroy

**函数功能**

销毁 Server 实例并释放相关资源。

**函数原型**

```
HixlStatus HixlCSServerDestroy(HixlServerHandle server_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_serverdestroy_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">server_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">Server 实例句柄。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 需要和HixlCSServerCreate配对使用。
- 建议在调用HixlCSServerDestroy前，链路进行断链以及对注册的内存进行解注册。
- Server需要等所有Client完成断链后调用该接口，如果Server提前退出，Client断链以及数据传输过程会发生报错。
- 当Client需要操作Server端地址进行远端读写，Server端需要等Client完成远端读写之后才调用该接口；否则会出现失败。
- 该接口不能和其他接口并发调用。

### HixlCSClientCreate

**函数功能**

创建 Client 实例并初始化本端资源。

**函数原型**

```
HixlStatus HixlCSClientCreate(const HixlClientDesc *client_desc,
                              const HixlClientConfig *config,
                              HixlClientHandle *client_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_clientcreate_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">client_desc</td><td class="cellrowborder">输入</td><td class="cellrowborder">Client 描述，包含 server IP/port 与端点。</td></tr>
<tr><td class="cellrowborder">config</td><td class="cellrowborder">输入</td><td class="cellrowborder">client配置，预留配置，暂未使用。</td></tr>
<tr><td class="cellrowborder">client_handle</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回创建的客户端句柄。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 如果使用本地device的Endpoint，调用该接口前需要先调用“aclrtSetDevice”。
- 调用失败时 `client_handle` 不保证有效。

### HixlCSClientConnect

**函数功能**

发起与 Server 的同步建链（阻塞直到成功或超时）。

**函数原型**

```
HixlStatus HixlCSClientConnect(HixlClientHandle client_handle, uint32_t timeout_ms);
```

**参数说明**

<a name="zh-cn_topic_cs_clientconnect_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">client_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">客户端句柄。</td></tr>
<tr><td class="cellrowborder">timeout_ms</td><td class="cellrowborder">输入</td><td class="cellrowborder">连接超时时间（毫秒）。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：连接成功
- `HIXL_PARAM_INVALID`：参数错误
- `HIXL_TIMEOUT`：连接超时
- 其他：失败
  
**约束说明**

- 调用当前接口与server建链前，需要完成所有本地内存的注册。
- 调用当前接口与server建链前，需要确保server已经处于监听状态。
- 调用当前接口建链后，需要调用HixlCSClientGetRemoteMem接口，确保远端内存描述信息交换至本地。

### HixlCSClientGetRemoteMem

**函数功能**

获取 Server 已注册的内存信息。

**函数原型**

```
HixlStatus HixlCSClientGetRemoteMem(HixlClientHandle client_handle,
                                    HcommMem **remote_mem_list,
                                    char ***mem_tag_list,
                                    uint32_t *list_num,
                                    uint32_t timeout_ms);
```

**参数说明**

<a name="zh-cn_topic_cs_clientgetremotemem_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">client_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">客户端句柄。</td></tr>
<tr><td class="cellrowborder">remote_mem_list</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回的 `HcommMem` 数组指针，内存生命周期由接口内部管理。重复调用该接口，将释放上一次申请的内存，如有需要按需拷贝至本地内存（定义见 https://gitcode.com/cann/hcomm/blob/master/include/hcomm_res_defs.h）。</td></tr>
<tr><td class="cellrowborder">mem_tag_list</td><td class="cellrowborder">输出</td><td class="cellrowborder">字符串数组，对应每个 `HcommMem` 的标签，内存生命周期由接口内部管理。重复调用该接口，将释放上一次申请的内存，如有需要按需拷贝至本地内存。</td></tr>
<tr><td class="cellrowborder">list_num</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回的列表长度，内存生命周期由接口内部管理。重复调用该接口，将释放上一次申请的内存，如有需要按需拷贝至本地内存。</td></tr>
<tr><td class="cellrowborder">timeout_ms</td><td class="cellrowborder">输入</td><td class="cellrowborder">请求超时时间（毫秒）。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败或超时

**约束说明**

- 输出参数内存生命周期由接口内部管理。重复调用该接口，将释放上一次申请的内存，如有需要按需拷贝至本地内存。
- 调用HixlCSClientConnect接口后，需要调用当前接口，确保远端内存描述信息交换至本地。

### HixlCSClientRegMem

**函数功能**

Client 注册本地内存。

**函数原型**

```
HixlStatus HixlCSClientRegMem(HixlClientHandle client_handle,
                              const char *mem_tag,
                              const HcommMem *mem,
                              MemHandle *mem_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_clientregmem_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">client_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">客户端句柄。</td></tr>
<tr><td class="cellrowborder">mem_tag</td><td class="cellrowborder">输入</td><td class="cellrowborder">内存标签字符串。</td></tr>
<tr><td class="cellrowborder">mem</td><td class="cellrowborder">输入</td><td class="cellrowborder">`HcommMem` 描述。</td></tr>
<tr><td class="cellrowborder">mem_handle</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回的内存句柄。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 在调用HixlCSClientConnect与server端建链之前需要完成所有本地内存的注册。
- 使用host RoCE网卡当前不支持注册“aclrtMallocHost”申请出来的内存，可使用malloc等方式。
- 注册Device内存使用“aclrtMalloc”进行申请。

### HixlCSClientUnregMem

**函数功能**

注销 Client 注册的内存。

**函数原型**

```
HixlStatus HixlCSClientUnregMem(HixlClientHandle client_handle, MemHandle mem_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_clientunregmem_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">client_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">客户端句柄。</td></tr>
<tr><td class="cellrowborder">mem_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">要注销的内存句柄。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 确保本地不再访问该内存后，且与server均断链以后再注销。

### HixlCSClientBatchPutAsync

**函数功能**

异步批量向Server写多组数据。

**函数原型**

```
HixlStatus HixlCSClientBatchPutAsync(HixlClientHandle client_handle,
                                     uint32_t list_num,
                                     const HixlOneSideOpDesc *desc_list,
                                     CompleteHandle *complete_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_clientbatchput_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">client_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">客户端句柄。</td></tr>
<tr><td class="cellrowborder">list_num</td><td class="cellrowborder">输入</td><td class="cellrowborder">任务组数。</td></tr>
<tr><td class="cellrowborder">desc_list</td><td class="cellrowborder">输入</td><td class="cellrowborder">指向 `HixlOneSideOpDesc` 数组，长度为 `list_num`。</td></tr>
<tr><td class="cellrowborder">complete_handle</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回的完成句柄，用于用户异步查询任务状态，当查询成功后，将自动释放相关资源。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：任务提交成功（不代表完成）
- `HIXL_PARAM_INVALID`：参数错误
- 其他：提交失败

**约束说明**

- 最大支持4K个数据并发传输，下发任务后需及时调用HixlCSClientQueryCompleteStatus接口查询任务状态。

### HixlCSClientBatchGetAsync

**函数功能**

异步批量从Server读多组数据到本地。

**函数原型**

```
HixlStatus HixlCSClientBatchGetAsync(HixlClientHandle client_handle,
                                     uint32_t list_num,
                                     const HixlOneSideOpDesc *desc_list,
                                     CompleteHandle *complete_handle);
```

**参数说明**

<a name="zh-cn_topic_cs_clientbatchput_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">client_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">客户端句柄。</td></tr>
<tr><td class="cellrowborder">list_num</td><td class="cellrowborder">输入</td><td class="cellrowborder">任务组数。</td></tr>
<tr><td class="cellrowborder">desc_list</td><td class="cellrowborder">输入</td><td class="cellrowborder">指向 `HixlOneSideOpDesc` 数组，长度为 `list_num`。</td></tr>
<tr><td class="cellrowborder">complete_handle</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回的完成句柄，用于用户异步查询任务状态，当查询成功后，将自动释放相关资源。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：任务提交成功（不代表完成）
- `HIXL_PARAM_INVALID`：参数错误
- 其他：提交失败

**约束说明**

- 最大支持4K个数据并发传输，下发任务后需及时调用HixlCSClientQueryCompleteStatus接口查询任务状态。

### HixlCSClientQueryCompleteStatus

**函数功能**

查询异步批量任务的完成状态。

**函数原型**

```
HixlStatus HixlCSClientQueryCompleteStatus(HixlClientHandle client_handle,
                                           CompleteHandle complete_handle,
                                           HixlCompleteStatus *complete_status);
```

**参数说明**

<a name="zh-cn_topic_cs_clientquery_table"></a>
<table><thead align="left"><tr><th class="cellrowborder">参数名</th><th class="cellrowborder">输入/输出</th><th class="cellrowborder">描述</th></tr></thead><tbody>
<tr><td class="cellrowborder">client_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">客户端句柄。</td></tr>
<tr><td class="cellrowborder">complete_handle</td><td class="cellrowborder">输入</td><td class="cellrowborder">要查询的任务句柄。</td></tr>
<tr><td class="cellrowborder">complete_status</td><td class="cellrowborder">输出</td><td class="cellrowborder">返回任务状态枚举（见 `HixlCompleteStatus`）。</td></tr>
</tbody></table>

**返回值**

- `HIXL_SUCCESS`：查询成功，传输状态需要根据complete_status确定。
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 最大支持4K个数据并发传输，下发任务后需及时调用HixlCSClientQueryCompleteStatus接口查询任务状态。
- 查询传输任务状态为HIXL_COMPLETE_STATUS_COMPLETED后，相关资源将自动释放，不支持使用相同的complete_handle再次查询。
- 查询传输任务状态为HIXL_COMPLETE_STATUS_WAITING，需用户自行判断当前传输任务是否已经发生超时，如果超时可重建传输传输链路进行重试，并销毁当前异常链路。

### HixlCSClientDestroy

**函数功能**

销毁 Client 实例并释放资源。

**函数原型**

```
HixlStatus HixlCSClientDestroy(HixlClientHandle client_handle);
```

**参数说明**

| 参数名 | 输入/输出 | 描述 |
|---|---:|---|
| client_handle | 输入 | 客户端句柄。|

**返回值**

- `HIXL_SUCCESS`：成功
- `HIXL_PARAM_INVALID`：参数错误
- 其他：失败

**约束说明**

- 需要和HixlCSClientCreate配对使用。
- 建议在调用HixlCSClientDestroy前，链路进行断链以及对注册的内存进行解注册。
- 该接口不能和其他接口并发调用。

## 使用建议

1. 环境准备：调用 `aclrtSetDevice(device_id)`。
2. 准备 Endpoint 描述（`EndpointDesc`）。
3. Server 流程：
  - 构造 `HixlServerDesc` 与 `HixlServerConfig`。
  - 调用 `HixlCSServerCreate` 创建 `HixlServerHandle`。
  - 为要被远端访问的内存分配并准备 `HcommMem`（Host/Device 内存），调用 `HixlCSServerRegMem` 注册并保存返回的 `MemHandle`。
  - 调用 `HixlCSServerListen` 开始监听连接。
  - 等待 Client 建链并发起数据传输。
  - 传输结束后调用 `HixlCSServerUnregMem` 注销内存并 `HixlCSServerDestroy` 销毁服务。
4. Client 流程：
  - 构造 `HixlClientDesc` 与 `HixlClientConfig`。
  - 调用 `HixlCSClientCreate` 创建 `HixlClientHandle`。
  - 准备本端 `HcommMem` 并通过 `HixlCSClientRegMem` 注册（保存 `MemHandle`）。
  - 调用 `HixlCSClientConnect` 建链（阻塞或等待超时），确保Server处于监听状态。
  - 调用 `HixlCSClientGetRemoteMem` 获取 Server 已注册的内存，从而获取远端地址用于后续操作。
  - 构造一组 `HixlOneSideOpDesc`（local/remote 地址、长度），调用 `HixlCSClientBatchPutAsync` 或 `HixlCSClientBatchGetAsync` 提交异步批量操作。
  - 轮询 `HixlCSClientQueryCompleteStatus` 直到状态为 `HIXL_COMPLETE_STATUS_COMPLETED`。
  - 完成后按需读取/校验内存内容，最后 `HixlCSClientUnregMem` 注销本端内存并 `HixlCSClientDestroy`。

## 示例

下面给出简化版的流程代码片段，体现基准程序中的关键步骤与顺序（省略细节错误处理与平台初始化代码）：

```c
// --- Server (伪代码) ---
HixlServerHandle server = NULL;
HixlServerDesc sdesc = {"0.0.0.0", 12345, &endpoint, 1};
HixlCSServerCreate(&sdesc, NULL, &server);

// 分配并初始化 server 内存（Host 或 Device）
HcommMem server_mem = { .addr = server_buf, .size = size, .type = HCCL_MEM_TYPE_DEVICE };
MemHandle server_mem_h = NULL;
HixlCSServerRegMem(server, "server_mem", &server_mem, &server_mem_h);

HixlCSServerListen(server, 1024);

// 等待 client 发起并完成传输（可用 TCP 信令同步）

// 注销并销毁
HixlCSServerUnregMem(server, server_mem_h);
HixlCSServerDestroy(server);

// --- Client (伪代码) ---
HixlClientHandle client = NULL;
HixlClientDesc cdesc = {"server.ip", 12345, &local_ep, &remote_ep};
HixlCSClientCreate(&cdesc, NULL, &client);

// 分配并注册本地内存
HcommMem client_mem = { .addr = client_buf, .size = size, .type = HCCL_MEM_TYPE_DEVICE };
MemHandle client_mem_h = NULL;
HixlCSClientRegMem(client, "client_mem", &client_mem, &client_mem_h);

// 建链
HixlCSClientConnect(client, 5000);

// 获取远端内存地址（通过 mem_tag）
HcommMem *remote_list = NULL; char **tags = NULL; uint32_t num = 0;
HixlCSClientGetRemoteMem(client, &remote_list, &tags, &num, 2000);

// 构造批量操作描述，示例：分块循环 varying block size
std::vector<HixlOneSideOpDesc> ops = ...; // 填写 local/remote addr 与 len
CompleteHandle ch;
HixlCSClientBatchPutAsync(client, ops.size(), ops.data(), &ch);

// 查询直到完成
HixlCompleteStatus st;
do {
  HixlCSClientQueryCompleteStatus(client, ch, &st);
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
} while (st == HIXL_COMPLETE_STATUS_WAITING);

// 清理
HixlCSClientUnregMem(client, client_mem_h);
HixlCSClientDestroy(client);
```
---
