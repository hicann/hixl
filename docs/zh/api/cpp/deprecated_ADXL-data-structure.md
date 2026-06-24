# 待废弃_ADXL数据结构

**MemType**

内存的类型。

```cpp
enum MemType {
  MEM_DEVICE,
  MEM_HOST
}
```

**TransferOp**

传输操作的类型。

```cpp
enum TransferOp {
  READ,
  WRITE
}
```

**MemDesc**

内存的描述信息。

```cpp
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
}
```

**TransferOpDesc**

传输操作的描述信息。

```cpp
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
}
```

**MemHandle**

注册内存的Handle。

```cpp
using MemHandle = void *;
```

**NotifyDesc**

Notify的描述信息。

```cpp
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
}
```

**FeatureType**

库能力特性类型，用于GetCapability接口查询。枚举值必须显式赋值，新增能力仅允许在末尾扩展。

```cpp
enum FeatureType : int32_t {
  AUTO_CONNECT = 0,
  CLIENT_SERVER_COMM = 1,
}
```

|枚举值|描述|
|--|--|
|AUTO_CONNECT|Auto Connect模式，对应Initialize时OPTION_AUTO_CONNECT选项。|
|CLIENT_SERVER_COMM|Client/Server通信模式，即Server端监听端口、Client端发起建链的能力。|

**FEATURE_SUPPORTED / FEATURE_NOT_SUPPORTED**

GetCapability接口输出参数value的取值常量。

```cpp
constexpr int32_t FEATURE_SUPPORTED = 1;
constexpr int32_t FEATURE_NOT_SUPPORTED = 0;
```
