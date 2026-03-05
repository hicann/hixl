# Bandwidth Benchmark 使用说明

## 功能概述

`benchmark_bandwidth.py` 是一个用于测试不同 block 大小下的数据传输带宽的 benchmark 工具。它支持多种传输模式，可以帮助分析 Hixl 与 Mooncake Store 集成的性能表现。

## 传输模式

该 benchmark 支持三种传输模式：

### 1. pairwise（成对传输）
- 每个 rank 从下一个 rank 获取数据（rank i 从 rank i+1 获取）
- 适用于简单的点对点性能测试
- 适合单卡或多卡环境

### 2. full_mesh（全互联传输）
- 所有 rank 相互之间都进行数据传输
- 每个 rank 向其他所有 rank put 数据，并从其他所有 rank get 数据
- 适用于测试复杂的分布式通信场景

### 3. one_to_many（一对多传输）
- rank 0 向其他所有 rank put 数据
- 其他 rank 从 rank 0 get 数据
- 适用于测试广播场景的性能

## 使用方法

### 1. 启动 Mooncake Master

首先需要启动 Mooncake master：

```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

### 2. 运行 Benchmark

使用 `run.sh` 脚本运行 benchmark：

```bash
bash run.sh benchmark_bandwidth.py [参数]
```

## 参数说明

### 基本参数

- `--device_id`: （必填）当前进程所在的 NPU 设备 ID
- `--schema`: （可选）传输模式，默认为 "d2d"，可选值：h2h, h2d, d2h, d2d
- `--transfer_mode`: （可选）传输模式，默认为 "pairwise"，可选值：
  - `pairwise`: 成对传输
  - `full_mesh`: 全互联传输
  - `one_to_many`: 一对多传输

### Benchmark 参数

- `--block_sizes`: （可选）要测试的 block 大小列表（KB），默认为 "1,4,16,64,144,256,512,1024"
- `--num_blocks`: （可选）每次迭代的 block 数量，默认为 100
- `--num_iters`: （可选）每个 block 大小的迭代次数，默认为 10
- `--register_size_gb`: （可选）自定义要注册的内存大小（GB），用于压力测试模式

### 分布式参数

- `--config`: （可选）YAML 配置文件路径
- `--rank`: （可选）当前进程的 rank，默认为 device_id // 2
- `--world_size`: （可选）分布式集群的设备数
- `--distributed`: （可选）启用分布式模式

## 使用示例

### 示例 1：单卡测试 pairwise 模式

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="pairwise"
```

### 示例 2：测试特定 block 大小

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="pairwise" \
  --block_sizes="64,144,256" \
  --num_blocks=50 \
  --num_iters=20
```

### 示例 3：多卡 full_mesh 测试

在多卡环境中，需要在每个设备上运行：

```bash
# 在 device 0 上
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=0 \
  --world_size=4 \
  --distributed

# 在 device 1 上
bash run.sh benchmark_bandwidth.py \
  --device_id=1 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=1 \
  --world_size=4 \
  --distributed

# 在 device 2 上
bash run.sh benchmark_bandwidth.py \
  --device_id=2 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=2 \
  --world_size=4 \
  --distributed

# 在 device 3 上
bash run.sh benchmark_bandwidth.py \
  --device_id=3 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=3 \
  --world_size=4 \
  --distributed
```

### 示例 4：使用配置文件

创建配置文件 `benchmark_config.yaml`：

```yaml
distributed:
  enabled: true
  world_size: 4
  master_addr: "127.0.0.1"
  master_port: "29500"

mooncake:
  store_ip: "127.0.0.1"
  port_start: 12345
  metadata_url: "http://127.0.0.1:8080/metadata"
  grpc_url: "127.0.0.1:50051"
```

然后使用配置文件运行：

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --config=benchmark_config.yaml \
  --transfer_mode="full_mesh"
```

### 示例 5：压力测试模式

使用 `--register_size_gb` 参数进行压力测试：

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="pairwise" \
  --register_size_gb=10 \
  --block_sizes="1024" \
  --num_blocks=10
```

## 输出说明

### 启动信息

```
Starting bandwidth benchmark
Schema: d2d
Transfer mode: pairwise
World size: 1
```

### 单个 Block Size 的输出

```
================================================================================
Benchmark Configuration:
  Block Size: 144 KB
  Number of Blocks: 100
  Iterations: 10
  Transfer Mode: pairwise
================================================================================

Performing warmup iteration...
Warmup completed

================================================================================
Results for 144 KB:
  Total Data per operation: 0.137 GB
  Put: 0.012s => 11.417 GB/s (0.137 GB)
  Get (avg over 10 iters): 0.011s => 12.454 GB/s (0.137 GB)
================================================================================
```

### 汇总输出

```
================================================================================
SUMMARY - Bandwidth Results
Mode: pairwise
World Size: 1
================================================================================
Block (KB)        Put (GB/s)        Get (GB/s)        
--------------------------------------------------------------------------------
1                 0.010             0.011             
4                 0.035             0.036             
16                0.120             0.125             
64                0.450             0.460             
144               11.417            12.454            
256               0.980             1.000             
512               1.850             1.900             
1024              3.200             3.300             
================================================================================
```