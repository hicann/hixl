# HIXL数据结构

## MemDesc

内存的描述信息。

```
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
}
```

## MemHandle

内存的Handle。

```
using MemHandle = void *;
```

## MemType

内存的类型。

```
enum MemType {
  MEM_DEVICE,
  MEM_HOST
}
```

## TransferOp

传输操作的类型。

```
enum TransferOp {
  READ,
  WRITE
}
```

## TransferOpDesc

传输操作的描述信息。

```
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
}
```

## TransferArgs

传输操作的可选参数，暂未启用。

```
struct TransferArgs{
  uint8_t reserved[128] = {};
}
```

## TransferReq

请求传输的Handle。

```
using TransferReq = void *;
```

## TransferStatus

异步传输的状态。

```
enum class TransferStatus {
  WAITING,
  COMPLETED,
  TIMEOUT, //暂不支持
  FAILED
}
```

## NotifyDesc

Notify的描述信息。

```
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
  }
```
