# 待废弃_ADXL数据结构

**MemType**

内存的类型。

```
enum MemType {
  MEM_DEVICE,
  MEM_HOST
}
```

**TransferOp**

传输操作的类型。

```
enum TransferOp {
  READ,
  WRITE
}
```

**MemDesc**

内存的描述信息。

```
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
}
```

**TransferOpDesc**

传输操作的描述信息。

```
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
}
```

**MemHandle**

注册内存的Handle。

```
using MemHandle = void *;
```

**NotifyDesc**

Notify的描述信息。

```
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
}
```

