### Test APIs Overview

This test validates the zero-copy functionalities of the following APIs when integrating HIXL with Mooncake Store: `batch_put_from`, `batch_get_into`, `batch_put_from_multi_buffers`, and `batch_get_into_multi_buffers`.

⚠️ **Note: Before calling zero-copy APIs, you must register the buffer.**

> Call the `register_buffer()` API of Mooncake Store to complete the registration. For details about related APIs, see [Mooncake Store Python API](https://kvcache-ai.github.io/Mooncake/python-api-reference/mooncake-store.html#register-buffer).

#### batch_put_from

```
def batch_put_from(self, keys: List[str], buffer_ptrs: List[int], sizes: List[int], config: ReplicateConfig = None) -> List[int]
```

**Parameters:**

- `keys` (List[str]): List of object identifiers
- `buffer_ptrs` (List[int]): List of memory addresses
- `sizes` (List[int]): List of buffer sizes
- `config` (ReplicateConfig, optional): Replication configuration

**Returns:**

- `List[int]`: List of status codes for each operation (0 = success, negative = error)

#### batch_get_into

```
def batch_get_into(self, keys: List[str], buffer_ptrs: List[int], sizes: List[int]) -> List[int]
```

**Parameters:**

- `keys` (List[str]): List of object identifiers
- `buffer_ptrs` (List[int]): List of memory addresses
- `sizes` (List[int]): List of buffer sizes

**Returns:**

- `List[int]`: List of bytes read for each operation (positive = success, negative = error)

#### batch_put_from_multi_buffers

```
def batch_put_from_multi_buffers(self, keys: List[str], all_buffer_ptrs: List[List[int]], all_sizes: List[List[int]],config: ReplicateConfig = None) -> List[int]
```

**Parameters:**

- `keys` (List[str]): List of object identifiers
- `all_buffer_ptrs` (List[List[int]]): all List of memory addresses
- `all_sizes` (List[List[int]]): all List of buffer sizes
- `config` (ReplicateConfig, optional): Replication configuration

**Returns:**

- `List[int]`: List of status codes for each operation (0 = success, negative = error)

#### batch_get_into_multi_buffers

```
def batch_get_into_multi_buffers(self, keys: List[str], all_buffer_ptrs: List[List[int]], all_sizes: List[List[int]]) -> List[int]
```

**Parameters:**

- `keys` (List[str]): List of object identifiers
- `all_buffer_ptrs` (List[List[int]]): all List of memory addresses
- `all_sizes` (List[List[int]]): all List of buffer sizes

**Returns:**

- `List[int]`: List of bytes read for each operation (positive = success, negative = error)

For details about the test APIs, see the Mooncake API document.

### Environment Preparations (Skip If Installed)

1. Install the CANN package. In the example, the `root` user is used for installation and usage.

2. Compile and install Mooncake. `v0.3.7.post2` is recommended. Use the `-DUSE_ASCEND_DIRECT=ON` parameter to enable the HIXL function. For details about the compilation and installation procedure, see the [Mooncake Build Guide](https://github.com/kvcache-ai/Mooncake/blob/v0.3.7.post2/doc/en/build.md).

### Test Case Execution

* Start mooncake_master.
```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

* Run the test.

Configure the distributed cluster and Mooncake Store parameters by referring to the `config_example.yaml` file.

In `run.sh`, set `export HCCL_INTRA_ROCE_ENABLE=1 ` to select `RDMA` as the transmission mode. (If this parameter is set to `0`, `HCCS` is used by default within the machine.)

Do not disable both `RoCE` and `PCIe` at the same time. Otherwise, the following error message is displayed:

>  [Parse] [IntraLinkType]only set HCCL_INTRA_ROCE_ENABLE, and the val is zero, pls set HCCL_INTRA_PCIE_ENABLE

Run the following command on the terminal:

``````bash 
bash run.sh **.py 
``````

> In the command, `**.py` is the sample corresponding to the API to be tested. For example, to test the `batch_put_get` API, use `batch_put_get_sample.py`.

Run commands to import execution parameters. The parameters are as follows:

* `device_id`: (mandatory) NPU device where the current process is located. The value is of the `int` type.
* `schema`: (optional) transmission type of the current test. The value is of the `str` type. The default value is `d2d`. The value must be `h2h`, `h2d`, `d2h`, or `d2d`, which is case-insensitive.
* `config`: (optional) path of the YAML configuration file. The hard-coded initial value has been deleted from the current code. You can modify the code or pass the `config` parameter to execute the test case. The value is of the `str` type.
* `rank`: (mandatory) rank of the current process. The value is of the `int` type. It is the unique identifier of each process. The value range is `[0, world_size - 1]`.
* `world_size`: (optional) number of devices configured in the distributed cluster. The value is of the `int` type.
* `distributed`: (optional) whether to enable the distributed cluster.

> Note that some parameters can also be configured in the configuration file, but the priority of the command line input is higher.

The following uses the `batch_put_get` API as an example to describe how to perform D2D data transmission in a single-node environment with a single device. After starting and configuring mooncake_master or hardcoding the corresponding parameters in the code, run the following command:

```bash
bash run.sh batch_put_get_sample.py --device_id=0 --schema="d2d"
```

> For tests in a single-node multi-device environment or a distributed cluster, you only need to create a configuration file by referring to `config_example.yaml` and pass the `config` parameter during runtime to specify the path of the configuration file.

### Dummy Client Mode (Optional)

In addition to the default embedded mode, the sample also supports the Dummy Client mode, which connects to an independently running Real Client process.

#### Dummy/Real Client Principles

- Real Client: runs as a standalone process. It implements the full suite of Mooncake Store functionalities, handling RPC communication, memory management, and data transmission.
- Dummy Client: A lightweight wrapper embedded within the application process. It forwards all operations to Real Client via RPC.
- Communication mechanism: Dummy Client and Real Client communicate via RPC and shared memory, enabling zero-copy data transfer.

> For details about Dummy/Real Client, see [Mooncake Store Dummy/Real Client Introduction](https://gitcode.com/cann/hixl/wiki/Mooncake%20Store%20Dummy-Real%20Client%20%E4%BB%8B%E7%BB%8D.md).

#### Using the Dummy Client Mode (Single-Node Instance)

1. Start mooncake_master (if it has not been started).
```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

2. Start Real Client as an independent process.
```bash
export ASCEND_ENABLE_USE_FABRIC_MEM=1
export ASCEND_RT_VISIBLE_DEVICES=0,1,2,3,4,5,6,7
mooncake_client \
    --master_server_address=127.0.0.1:50051 \
    --metadata_server=http://127.0.0.1:8080/metadata \
    --protocol=ascend \
    --port=54000 \
    --host=127.0.0.1 \
    --global_segment_size=5G
```

3. Run the sample and add the `--use_dummy` parameter.
```bash
bash run.sh batch_put_get_sample.py --device_id=0 --schema="d2d" --rank=0 --use_dummy
```

#### Additional Parameters in Dummy Client Mode

- `--use_dummy`: enables the Dummy Client mode.
- `--real_client_address`: The Real Client address (`127.0.0.1:54000` by default). Ensure that the `device id` is available to the Real Client process.
- `--mem_pool_size`: size of the Dummy Client memory pool (in bytes, optional)
- `--local_buffer_size`: size of the Dummy Client local buffer (in bytes, optional)
