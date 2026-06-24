# HIXL数据结构

## MemDesc

内存的描述信息。

```cpp
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
}
```

## MemHandle

内存的Handle。

```cpp
using MemHandle = void *;
```

## MemType

内存的类型。

```cpp
enum MemType {
  MEM_DEVICE,
  MEM_HOST
}
```

## AsyncConnectStatus

异步建链/拆链的状态。

```cpp
enum class AsyncConnectStatus {
  NOT_CONNECT,        // 未连接
  CONNECT_PENDING,    // 建链待执行
  CONNECTING,         // 建链执行中
  CONNECTED,          // 建链成功
  CONNECT_FAILED,     // 建链失败
  DISCONNECT_PENDING, // 断链待执行
  DISCONNECTING       // 断链执行中
};
```

## TransferOp

传输操作的类型。

```cpp
enum TransferOp {
  READ,
  WRITE
}
```

## TransferOpDesc

传输操作的描述信息。

```cpp
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
}
```

## TransferArgs

传输操作的可选参数。

```cpp
struct TransferArgs {
  void *user_data = nullptr;  // 用户自定义信息，需配合获取全部异步传输请求状态接口使用
  uint8_t reserved[120] = {};  // 预留参数
};
```

## TransferReq

传输请求的Handle。

```cpp
using TransferReq = void *;
```

## TransferStatus

异步传输的状态。

```cpp
enum class TransferStatus {
  WAITING,
  COMPLETED,
  TIMEOUT, //暂不支持
  FAILED
}
```

## GetTransferStatusArgs

获取全部异步传输请求状态时的参数。

```cpp
struct GetTransferStatusArgs {
  uint32_t max_query_count = UINT32_MAX;  // 最大查询出的传输请求状态的个数
  bool skip_waiting = false;  // 查询是否跳过状态为TransferStatus::WAITING的传输请求
  uint8_t reserved[123] = {};  // 预留参数
};
```

## TransferResult

获取全部异步传输请求状态时每一个请求的结果信息。

```cpp
struct TransferResult {
  TransferReq req = nullptr;  // 传输请求的Handle
  void *user_data = nullptr;  // 用户自定义信息
  TransferStatus status = TransferStatus::WAITING;  // 传输请求状态
  uint8_t reserved[108] = {};  // 预留参数
};
```

## NotifyDesc

Notify的描述信息。

```cpp
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
};
```

## FeatureType

库能力特性类型，用于GetCapability接口查询。枚举值必须显式赋值，新增能力仅允许在末尾扩展。

```cpp
enum FeatureType : int32_t {
  AUTO_CONNECT = 0,
  CLIENT_SERVER_COMM = 1,
}
```

| 枚举值 | 描述 |
| --- | --- |
| AUTO_CONNECT | Auto Connect模式，对应Initialize时OPTION_AUTO_CONNECT选项。 |
| CLIENT_SERVER_COMM | Client/Server通信模式，即Server端监听端口、Client端发起建链的能力。 |

## FEATURE_SUPPORTED / FEATURE_NOT_SUPPORTED

GetCapability接口输出参数value的取值常量。

```cpp
constexpr int32_t FEATURE_SUPPORTED = 1;
constexpr int32_t FEATURE_NOT_SUPPORTED = 0;
```
