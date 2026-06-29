# Bandwidth Benchmark Usage Guide

## Overview

`benchmark/benchmark_bandwidth.py` is a benchmark tool for testing data transmission bandwidth with different block sizes. It supports multiple transfer modes and helps analyze the performance of HIXL integrated with Mooncake Store.

## Transfer Modes

The benchmark supports two transfer modes:

### 1. full_mesh (Full Mesh Transfer)

- All ranks transmit data to and receive data from every other rank.
- Each rank puts data to all other ranks and gets data from all other ranks.
- Suitable for testing complex distributed communication scenarios.
- **Requires `world_size >= 2`. For single-device testing, use the `one_to_many` mode.**

### 2. one_to_many (One-to-Many Transfer)

- Rank 0 puts data to all other ranks.
- Other ranks get data from rank 0.
- Suitable for testing performance in broadcast scenarios.

## Usage

### 1. Start Mooncake Master

First, start the Mooncake master:

```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

### 2. Run the Benchmark

Use the `run.sh` script to run the benchmark:

```bash
bash run.sh benchmark/benchmark_bandwidth.py [parameters]
```

## Parameter Description

### Basic Parameters

- `--device_id`: (Required) NPU device ID for the current process.
- `--schema`: (Optional) Transfer schema, default is "d2d". Options: h2h, h2d, d2h, d2d.
- `--transfer_mode`: (Optional) Transfer mode, default is "full_mesh". Options:
  - `full_mesh`: Full mesh transfer
  - `one_to_many`: One-to-many transfer

### Benchmark Parameters

- `--block_sizes`: (Optional) List of block sizes to test (KB), default is "1,4,16,64,144,256,512,1024".
- `--num_blocks`: (Optional) Number of blocks per iteration, default is 100.
- `--num_iters`: (Optional) Number of iterations for each block size, default is 10.
- `--register_size_gb`: (Optional) Custom memory size to register (GB), used for stress testing mode.

### Distributed Parameters

- `--config`: (Optional) YAML configuration file path.
- `--rank`: Rank of the current process, default is device_id // 2.
- `--world_size`: Number of devices in the distributed cluster.
- `--distributed`: Enable distributed mode.

## Usage Examples

### Example 1: Single-device one_to_many Test

```bash
bash run.sh benchmark/benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="one_to_many"
```

### Example 2: Test Specific Block Sizes

```bash
bash run.sh benchmark/benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="one_to_many" \
  --block_sizes="64,144,256" \
  --num_blocks=50 \
  --num_iters=20
```

### Example 3: Multi-device full_mesh Test

In a multi-device environment, run on each device:

```bash
# On device 0:
bash run.sh benchmark/benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=0 \
  --world_size=4 \
  --distributed

# On device 1:
bash run.sh benchmark/benchmark_bandwidth.py \
  --device_id=1 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=1 \
  --world_size=4 \
  --distributed

# On device 2:
bash run.sh benchmark/benchmark_bandwidth.py \
  --device_id=2 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=2 \
  --world_size=4 \
  --distributed

# On device 3:
bash run.sh benchmark/benchmark_bandwidth.py \
  --device_id=3 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=3 \
  --world_size=4 \
  --distributed
```

### Example 4: Using a Configuration File

Create a configuration file `benchmark_config.yaml`:

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

Then run with the configuration file:

```bash
bash run.sh benchmark/benchmark_bandwidth.py \
  --device_id=0 \
  --config=benchmark_config.yaml \
  --transfer_mode="full_mesh"
```

### Example 5: Stress Testing Mode

Use the `--register_size_gb` parameter for stress testing:

```bash
bash run.sh benchmark/benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --register_size_gb=10 \
  --block_sizes="1024" \
  --num_blocks=10
```

## Output Description

### Startup Information

```sh
Starting bandwidth benchmark
Schema: d2d
Transfer mode: full_mesh
World Size: 4
```

### Output for a Single Block Size

```sh
================================================================================
Benchmark Configuration:
  Block Size: 144 KB
  Number of Blocks: 100
  Iterations: 10
  Transfer Mode: full_mesh
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

### Summary Output

```sh
================================================================================
SUMMARY - Bandwidth Results
Mode: full_mesh
World Size: 4
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

