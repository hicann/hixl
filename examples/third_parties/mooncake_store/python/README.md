### 测试接口介绍

测试Hixl对接Mooncake Store中batch_put_from、batch_get_into、batch_put_from_multi_buffers、batch_get_into_multi_buffers零拷贝相关接口功能。

⚠️ **注意：在 零拷贝接口调用前，必须完成buffer的注册**

#### batch_put_from

```
def batch_put_from(self, keys: List[str], buffer_ptrs: List[int], sizes: List[int], config: ReplicateConfig = None) -> List[int]
```

**参数:**

- `keys` (List[str]): List of object identifiers
- `buffer_ptrs` (List[int]): List of memory addresses
- `sizes` (List[int]): List of buffer sizes
- `config` (ReplicateConfig, optional): Replication configuration

**返回值:**

- `List[int]`: List of status codes for each operation (0 = success, negative = error)

#### batch_get_into

```
def batch_get_into(self, keys: List[str], buffer_ptrs: List[int], sizes: List[int]) -> List[int]
```

**参数:**

- `keys` (List[str]): List of object identifiers
- `buffer_ptrs` (List[int]): List of memory addresses
- `sizes` (List[int]): List of buffer sizes

**返回值:**

- `List[int]`: List of bytes read for each operation (positive = success, negative = error)

#### batch_put_from_multi_buffers

```
def batch_put_from_multi_buffers(self, keys: List[str], all_buffer_ptrs: List[List[int]], all_sizes: List[List[int]],
                                 config: ReplicateConfig = None) -> List[int]
```

**参数：**

- `keys` (List[str]): List of object identifiers
- `all_buffer_ptrs` (List[int]): all List of memory addresses
- `sizes` (List[int]): all List of buffer sizes
- `config` (ReplicateConfig, optional): Replication configuration

**返回值：**

- `List[int]`: List of status codes for each operation (0 = success, negative = error)

#### batch_get_into_multi_buffers

```
def batch_get_into_multi_buffers(self, keys: List[str], all_buffer_ptrs: List[List[int], all_sizes: List[List[int]) ->
List[int]
```

**参数:**

- `keys` (List[str]): List of object identifiers
- `all_buffer_ptrs` (List[int]): List of memory addresses
- `all_sizes` (List[int]): List of buffer sizes

**返回值:**

- `List[int]`: List of bytes read for each operation (positive = success, negative = error)

测试接口的详细信息，可以参考Mooncake接口文档

### 环境准备（已安装可跳过）

1. 安装CANN包，样例中场景为root用户安装与使用
2. Mooncake编译安装 使用`-DUSE_ASCEND_DIRECT=ON` 参数启用Hixl功能
> mkdir build && cd build \
> cmake -DUSE_ASCEND_DIRECT=ON .. \
> make -j \
> make install

### 执行测试用例

* 启动Mooncake master
```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host= <运行时填入http server host> \
  --http_metadata_server_port=8080
```

* 运行测试：

在`run.sh`中，通过`export HCCL_INTRA_ROCE_ENABLE=1 `选择传输方式为RDMA（如果设置为0，则走hccs）

以单机两卡环境下进行d2d测试为例，在两个终端分别执行，其中`device_id` 绑定device，`schema` 为传输场景（h2h，h2d，d2h，d2d 不区分大小写）

```bash
bash run.sh batch_put_get_sample.py --device_id=0 --shcema="d2d"
```

```bash
bash run.sh batch_put_get_sample.py --device_id=3 --schema="d2d"
```

