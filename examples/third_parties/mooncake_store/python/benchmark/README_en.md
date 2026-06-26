# Bandwidth Benchmark Usage

## Function Description

`benchmark_bandwidth.py` is a benchmark tool that tests data transmission bandwidth across different block sizes. It supports multiple transfer modes and helps analyze the performance of HIXL integrated with Mooncake Store.

## Transfer Mode

The benchmark supports three transfer modes:

### 1. pairwise
- Each rank obtains data from the next rank (rank `i` obtains data from rank `i+1`).
- This mode is suitable for simple point‑to‑point performance testing.
- This mode is applicable to single-device or multi-device environments.

### 2. full_mesh
- All ranks transmit data to and receive data from every other rank.
- Each rank puts data to all other ranks and gets data from all other ranks.
- This mode is suitable for testing complex distributed communication scenarios.

### 3. one_to_many
- Rank 0 puts data to all other ranks.
- Other ranks get data from rank 0.
- This mode is suitable for testing performance in broadcast scenarios.

## Usage

### 1. Starting Mooncake Master

Start Mooncake master first.

```bash
mooncake_master \
  --enable_http_metadata_server=true \
  --http_metadata_server_host=0.0.0.0 \
  --http_metadata_server_port=8080
```

### 2. Run the Benchmark

Use `run.sh` to launch the benchmark.

```bash
bash run.sh benchmark_bandwidth.py [Parameters]
```

## Parameter Description

### Basic Parameters

- `--device_id`: (Required) Device ID of the NPU for the current process.
- `--schema`: (Optional) Transfer mode. The default value is `d2d`. The options are `h2h`, `h2d`, `d2h`, and `d2d`.
- `--transfer_mode`: (Optional) Transfer mode. The default value is `pairwise`. The options are as follows:
  - `pairwise`
  - `full_mesh`
  - `one_to_many`

### Benchmark Parameters

- `--block_sizes`: (Optional) List of block sizes (KB) to be tested. The default value is `1,4,16,64,144,256,512,1024`.
- `--num_blocks`: (Optional) Number of blocks per iteration. The default value is `100`.
- `--num_iters`: (Optional) Number of iterations for each block size. The default value is `10`.
- `--register_size_gb`: (Optional) Custom registered memory size (in GB), used for stress testing mode.

### Distributed Parameters

- `--config`: (Optional) Path of the YAML configuration file.
- `--rank`: (Optional) Rank of the current process. The default value is `device_id // 2`.
- `--world_size`: (Optional) Number of devices in the distributed cluster.
- `--distributed`: (Optional) Enables the distributed mode.

## Example

### Example 1: Single-device pairwise test

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="pairwise"
```

### Example 2: Testing a specific block size

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="pairwise" \
  --block_sizes="64,144,256" \
  --num_blocks=50 \
  --num_iters=20
```

### Example 3: Multi-device full_mesh test

In a multi-device environment, run the following command on each device:

```bash
# On device 0:
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=0 \
  --world_size=4 \
  --distributed

# On device 1:
bash run.sh benchmark_bandwidth.py \
  --device_id=1 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=1 \
  --world_size=4 \
  --distributed

# On device 2:
bash run.sh benchmark_bandwidth.py \
  --device_id=2 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=2 \
  --world_size=4 \
  --distributed

# On device 3:
bash run.sh benchmark_bandwidth.py \
  --device_id=3 \
  --schema="d2d" \
  --transfer_mode="full_mesh" \
  --rank=3 \
  --world_size=4 \
  --distributed
```

### Example 4: Using a configuration file

Create the configuration file `benchmark_config.yaml`.

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

Run the benchmark with the configuration file.

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --config=benchmark_config.yaml \
  --transfer_mode="full_mesh"
```

### Example 5: Stress test mode

Use the `--register_size_gb` parameter for stress testing.

```bash
bash run.sh benchmark_bandwidth.py \
  --device_id=0 \
  --schema="d2d" \
  --transfer_mode="pairwise" \
  --register_size_gb=10 \
  --block_sizes="1024" \
  --num_blocks=10
```

## Output Description

### Startup information

```
Starting bandwidth benchmark
Schema: d2d
Transfer mode: pairwise
World size: 1
```

### Output for a single block size

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

### Summary output

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
