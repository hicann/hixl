# HIXL数据结构

## MemDesc

内存的描述信息。

```python
class MemDesc:
    def __init__(self, addr: int, len: int)

    addr: int   # 内存地址
    len: int    # 内存长度（字节）
```

**调用示例**

```python
mem_desc = hixl.MemDesc(addr=0x7f0000000000, len=1024*1024)
```

## MemHandle

内存的Handle。

Python中为int类型，由register\_mem接口返回。

## MemType

内存的类型。

```python
class MemType:
    MEM_DEVICE = 0  # Device内存
    MEM_HOST = 1    # Host内存
```

## AsyncConnectStatus

异步建链/拆链的状态。

```python
class AsyncConnectStatus:
    NOT_CONNECT = 0         # 未连接
    CONNECT_PENDING = 1     # 建链待执行
    CONNECTING = 2          # 建链执行中
    CONNECTED = 3           # 建链成功
    CONNECT_FAILED = 4      # 建链失败
    DISCONNECT_PENDING = 5  # 断链待执行
    DISCONNECTING = 6       # 断链执行中
```

## TransferOp

传输操作的类型。

```python
class TransferOp:
    READ = 0   # 从远端读取到本地
    WRITE = 1  # 从本地写入远端
```

## TransferOpDesc

传输操作的描述信息。

```python
class TransferOpDesc:
    def __init__(self, local_addr: int, remote_addr: int, len: int)

    local_addr: int   # 本地内存地址
    remote_addr: int  # 远端内存地址
    len: int          # 传输长度（字节）
```

**调用示例**

```python
op_desc = hixl.TransferOpDesc(local_addr=local, remote_addr=remote, len=4096)
```

## TransferArgs

传输操作的可选参数。

```python
class TransferArgs:
    def __init__(self)

    user_data: int  # 用户自定义信息（int类型），需配合获取全部异步传输请求状态接口使用
```

**调用示例**

```python
args = hixl.TransferArgs()
args.user_data = 12345
```

## TransferReq

传输请求的Handle。

Python中为int类型，由transfer\_async接口返回。

## TransferStatus

异步传输的状态。

```python
class TransferStatus:
    WAITING = 0    # 等待中
    COMPLETED = 1  # 已完成
    TIMEOUT = 2    # 超时（暂不支持）
    FAILED = 3     # 失败
```

## GetTransferStatusArgs

获取全部异步传输请求状态时的参数。

```python
class GetTransferStatusArgs:
    def __init__(self, max_query_count: int = 0xFFFFFFFF, skip_waiting: bool = False)

    max_query_count: int  # 最大查询出的传输请求状态的个数
    skip_waiting: bool    # 查询时是否跳过状态为TransferStatus.WAITING的传输请求，True表示跳过，False表示不跳过
```

**调用示例**

```python
args = hixl.GetTransferStatusArgs(max_query_count=10, skip_waiting=True)
```

## TransferResult

获取全部异步传输请求状态时每一个请求的结果信息。

```python
class TransferResult:
    req: int                # 传输请求的Handle（int类型）
    user_data: int          # 用户自定义信息（int类型）
    status: TransferStatus  # 传输请求状态（只读）
```

## NotifyDesc

Notify的描述信息。

```python
class NotifyDesc:
    def __init__(self, name: str, notify_msg: str)

    name: str         # Notify名称，长度上限1024字符
    notify_msg: str   # Notify消息内容，长度上限1024字符
```

**调用示例**

```python
notify = hixl.NotifyDesc(name="cache_ready", notify_msg="block_0")
```

## FeatureType

库能力特性类型，用于get\_capability接口查询。枚举值必须显式赋值，新增能力仅允许在末尾扩展。

```python
class FeatureType:
    AUTO_CONNECT = 0       # Auto Connect模式
    CLIENT_SERVER_COMM = 1 # Client/Server通信模式
```

| 枚举值 | 描述 |
| --- | --- |
| AUTO_CONNECT | Auto Connect模式，对应Initialize时OPTION\_AUTO\_CONNECT选项。 |
| CLIENT_SERVER_COMM | Client/Server通信模式，即Server端监听端口、Client端发起建链的能力。 |

## FEATURE\_SUPPORTED / FEATURE\_NOT\_SUPPORTED

get\_capability接口输出参数value的取值常量。

```python
FEATURE_SUPPORTED = 1      # 特性支持
FEATURE_NOT_SUPPORTED = 0  # 特性不支持
```
