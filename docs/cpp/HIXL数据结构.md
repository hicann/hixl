# HIXL数据结构<a name="ZH-CN_TOPIC_0000002413024580"></a>
## MemDesc<a name="ZH-CN_TOPIC_0000002446623669"></a>

内存的描述信息。

```
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
}
```
## MemHandle<a name="ZH-CN_TOPIC_0000002464248565"></a>
内存的Handle。

```
using MemHandle = void *;
```
## MemType<a name="ZH-CN_TOPIC_0000002413184452"></a>

内存的类型。

```
enum MemType {
  MEM_DEVICE,
  MEM_HOST
}
```
## TransferOp<a name="ZH-CN_TOPIC_0000002446743593"></a>
传输操作的类型。

```
enum TransferOp {
  READ,
  WRITE
}
```
## TransferOpDesc<a name="ZH-CN_TOPIC_0000002413024584"></a>
传输操作的描述信息。

```
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
}
```