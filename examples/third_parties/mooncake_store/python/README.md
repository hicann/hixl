### 测试接口介绍

测试Hixl对接Mooncake Store中batch_put_from、batch_get_into、batch_put_from_multi_buffers、batch_get_into_multi_buffers零拷贝相关接口功能。

⚠️ **注意：在零拷贝接口调用前，必须完成buffer的注册**

> 需要先调用Mooncake store的register_buffer()完成注册，相关API可以参考[Mooncake Store Python API](https://kvcache-ai.github.io/Mooncake/python-api-reference/mooncake-store.html#register-buffer)

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
def batch_put_from_multi_buffers(self, keys: List[str], all_buffer_ptrs: List[List[int]], all_sizes: List[List[int]],config: ReplicateConfig = None) -> List[int]
```

**参数：**

- `keys` (List[str]): List of object identifiers
- `all_buffer_ptrs` (List[List[int]]): all List of memory addresses
- `all_sizes` (List[List[int]]): all List of buffer sizes
- `config` (ReplicateConfig, optional): Replication configuration

**返回值：**

- `List[int]`: List of status codes for each operation (0 = success, negative = error)

#### batch_get_into_multi_buffers

```
def batch_get_into_multi_buffers(self, keys: List[str], all_buffer_ptrs: List[List[int]], all_sizes: List[List[int]]) -> List[int]
```

**参数:**

- `keys` (List[str]): List of object identifiers
- `all_buffer_ptrs` (List[List[int]]): all List of memory addresses
- `all_sizes` (List[List[int]]): all List of buffer sizes

**返回值:**

- `List[int]`: List of bytes read for each operation (positive = success, negative = error)

测试接口的详细信息，可以参考Mooncake接口文档

### 环境准备（已安装可跳过）

1. 安装CANN包，样例中场景为root用户安装与使用

2. Mooncake编译安装，注意使用`-DUSE_ASCEND_DIRECT=ON` 参数启用Hixl功能；具体的编译安装步骤，参考[Mooncake 安装文档](https://github.com/kvcache-ai/Mooncake/blob/main/doc/zh/build.md)


### 执行测试用例

* 启动Mooncake master
```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

* 运行测试：

参考`config_example.yaml`文件，配置运行时分布式集群配置以及Mooncake Store相关参数

在`run.sh`中，通过`export HCCL_INTRA_ROCE_ENABLE=1 `选择传输方式为RDMA（如果设置为0，则机器内默认走hccs）

执行时通过在终端执行：

``````bash 
bash run.sh **.py 
``````

> 其中`**.py`  为需要测试的接口对应的样例，例如测试`batch_put_get`接口时，就使用`batch_put_get_sample.py`

并通过命令传入执行参数，具体的参数列表如下：

* device_id ，必填，类型为int，当前进程所在npu设备
* schema，选填，类型为str，默认为“d2d”，当前测试的传输类型，（必须为h2h，h2d，d2h，d2d ，不区分大小写）
* config，选填， 类型为str，为yaml配置文件的路径，由于当前代码中删除了硬编码的初始值，可以选择修改代码或者传入config参数的方式，执行用例
* rank，选填，类型为int，当前进程的rank，如果不传入，默认为 **device_id // 2**
* world_size，选填，类型为int，分布式集群配置的设备数
* distributed，选填，是否启用分布式集群

> 注意，某些参数也可以通过配置文件的方式配置，但是命令行传入优先级更高

以单机环境单卡执行batch_put_get接口对应用例，进行d2d数据传输时，在启动完Mooncake master并完成配置或在代码中硬编码对应的参数之后；执行以下命令：

```bash
bash run.sh batch_put_get_sample.py --device_id=0 --schema="d2d"
```

> 单机多卡环境以及分布式集群下进行测试，只需要参考config_example.yaml创建配置文件，并在运行时传入 config参数指定配置文件路径即可。
