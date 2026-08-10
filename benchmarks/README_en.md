# HIXL Benchmarks

This directory contains benchmarks for communication and KV Cache scenarios, measuring HIXL transmission performance under different configurations.

- [Environment Requirements](#environment-requirements)
- [Quick Start](#quick-start)
- [Communication Benchmark](#communication-benchmark-hixl_comm_bench)
- [KV Benchmark](#kv-benchmark-hixl_kv_bench)
- [Directory Structure](#directory-structure)
- [Performance Data](performance_en.md)

## Environment Requirements

### 1. Hardware and Software Preparation

- Chips: Atlas A3 training/inference products, Atlas 800I A2 inference products/A200I A2 Box heterogeneous components, Ascend 950PR/Ascend 950DT
- Refer to [Environment Setup](../docs/en/build.md#environment-setup) to complete Ascend AI software stack deployment in the runtime environment

### 2. Device Connectivity Check

Before running samples, use the [hccn_tool](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference) provided in the driver package to check **connectivity between two devices**. Using A2 scenario as an example:

> If the `hccn_tool` command cannot be found, search for the executable file in the CANN driver package installation directory (default `/usr/local/Ascend/driver/tools/hccn_tool`), and use `ln -s` to add it to `PATH`.

- Step 1: Query the IP information of required devices, using an 8-card setup as an example:

```shell
for i in {0..7}; do hccn_tool -i $i -ip -g; done
```

- Step 2: Check connectivity between two devices, using devices a and b as an example:

```shell
hccn_tool -i ${device_id_a} -ping -g address ${ip_address_b}
hccn_tool -i ${device_id_b} -ping -g address ${ip_address_a}
```

If `recv time out seq` is returned, the two devices are not connected.

- RDMA between A3 single-card dual-die may not work; even after environment configuration, `ping` may fail. The most accurate way to determine is using `roce_test ib_send_bw` for traffic testing:

```bash
# Receiver
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test reset
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test ib_send_bw -s 65536 -n 1000 -tcp

# Sender
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test reset
PEER_IP=$(/usr/local/Ascend/driver/tools/hccn_tool -i 0 -ip -g 2>/dev/null | sed -n 's/^ipaddr:\(.*\)/\1/p' | head -1)
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test ib_send_bw -s 65536 -n 1000 address "$PEER_IP" -tcp
```

- Step 3: Check TLS certificate configuration consistency between devices:

```shell
for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch
```

Devices with TLS enabled cannot establish connections with devices where TLS is disabled. Example (disable TLS):

```shell
for i in {0..7}; do hccn_tool -i $i -tls -s enable 0; done
```

If `hccn_tool is busy, please try again` appears, avoid concurrent use of the command and retry later.

### Constraint Notes

- **HCCS**:
  - **A2 (Ascend910B-class)**: D2D only (`D2rD` / `rD2D`)
  - **A3 (Ascend910-class)**: D2D + `H2rD` / `rD2H`
  - **A5 (Ascend950)**: HCCS not supported
- **FabricMem**: not supported on A2; all 8 directions on A3 / A5
- **RoCE (A5)**: data plane uses the host NIC; usually requires `--host_roce_ip` (not the same as control-plane `--target-host` / `--local_engine`)
- **UBOE / UB_RTP / UB**: A5 only

## Quick Start

Run the following commands from the **repository root** (relative paths and default `output_dir` depend on cwd).

### Entry Points

| Entry | Purpose |
| --- | --- |
| `bash benchmarks/run_all_bench.sh` | Single-machine full comm + KV run; writes `benchmarks/perf.md` |
| `python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py` | Comm launcher (single- or dual-machine orchestration) |
| `build/benchmarks/comm_benchmark/hixl_comm_bench` | Run the C++ binary directly (you start target / initiator) |
| `python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py` | KV Cache benchmark |

### 1. Build

```bash
bash build.sh --examples
```

Compiled executables are in `build/benchmarks/`:

- `comm_benchmark/hixl_comm_bench` — Communication performance test
- `kv_benchmark/hixl_kv_bench` — KV Cache scenario test

### 2. Run All Tests with One Command

```bash
bash benchmarks/run_all_bench.sh
```

This script automatically:

- Checks and sources the CANN environment
- Detects chip model via `npu-smi info` (A2 / A3 / A5; device name containing Ascend910B→A2, Ascend910→A3, Ascend950→A5)
- Runs all supported communication direction × transport combinations on the current platform
- Runs KV Cache benchmarks (default transport by platform: A2=`roce`, A3/A5=`fabric_mem`)
- Generates `benchmarks/perf.md` and line charts
- Prints performance results in the terminal

After completion, open `benchmarks/perf.md` to view results.

> `run_all_bench.sh` supports **single-machine only**. For dual-machine, use `run_comm_benchmark.py --role=target|initiator` (see below). `--target-host` is deprecated; a non-`127.0.0.1` value exits with an error.

### 3. Custom Parameters

```bash
# Custom communication benchmark repetition loops and devices
bash benchmarks/run_all_bench.sh --loops 10 --device-ids 0,1,2,3,4,5,6,7

# Skip npu-smi and force platform
bash benchmarks/run_all_bench.sh --platform a3

# Custom perf.md output path
bash benchmarks/run_all_bench.sh --output /tmp/perf.md

# Run only KV tests
bash benchmarks/run_all_bench.sh --skip-comm

# Run only communication tests
bash benchmarks/run_all_bench.sh --skip-kv

# Pass HIXL Initialize() options to the communication benchmark (same as hixl_comm_bench -H=KEY=VALUE; repeatable)
bash benchmarks/run_all_bench.sh --hixl-option 'LocalCommRes={"version":"1.3"}'
```

### 4. Performance Data Summary

- **`perf.md`**: Automatically generated by `run_all_bench.sh` on a single platform (A2 / A3 / A5), containing current platform performance tables and line charts.
- **`performance.md`** / **`performance_en.md`**: Multi-platform summary documents, manually maintained by developers, sourced from `perf.md` generated on each platform.

---

## Communication Benchmark (`hixl_comm_bench`)

Measures HIXL block transmission bandwidth under different directions and transports.

- Bandwidth "GB/s" uses **decimal** units: **1 GB = 10⁹ bytes**
- Size units `K` / `M` / `G` / `T` in parameters (e.g. `16K`, `128M`) are parsed as **binary 1024**

**Concepts**:

- **Initiator**: The party that initiates transmission (read / write)
- **Target**: The party that responds (registers memory, waits for initiator connection)
- **Direction**: Determined by initiator local memory type + target remote memory type + operation

### Direction Naming

Direction name format is `source → remote target`, where **D**=Device, **H**=Host, **r**=remote.

From the **Initiator** perspective:

| Direction | Initiator local | Target remote | Op | Meaning |
| :--- | :---: | :---: | :---: | :--- |
| **D2rD** | device | device | write | Device writes to remote Device |
| **rD2D** | device | device | read | Read from remote Device to Device |
| **D2rH** | device | host | write | Device writes to remote Host |
| **rH2D** | device | host | read | Read from remote Host to Device |
| **H2rH** | host | host | write | Host writes to remote Host |
| **rH2H** | host | host | read | Read from remote Host to Host |
| **H2rD** | host | device | write | Host writes to remote Device |
| **rD2H** | host | device | read | Read from remote Device to Host |

### Single-Machine Run

Use `run_comm_benchmark.py` (recommended). It starts both target and initiator on the same host.

```bash
# Quick test for one direction (single-machine defaults: transport=hccs, direction=D2rD)
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --direction=D2rD --transport=hccs

# Specify devices and block-size range
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --direction=D2rH --transport=roce --device_ids=0,1 --block_sizes=16K:2M

# One-to-many: first N-1 device_ids are targets, last is initiator
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=one_to_many --device_ids=0,1,2,3,4 \
  --direction=D2rD --transport=hccs

# Many-to-one: first device_id is target, rest are initiators
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=many_to_one --device_ids=0,1,2,3,4 \
  --direction=D2rD --transport=hccs

# Pass HIXL Initialize options (same as hixl_comm_bench -H=KEY=VALUE; repeatable)
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --direction=D2rD --transport=hccs \
  -H 'LocalCommRes={"version":"1.3"}'
```

> Single-machine does **not** support `--transport=all` (exits with an error). If `--transport` is set explicitly and `--direction` is omitted, direction defaults to all directions supported for that transport on the current platform.

### Dual-Machine Run

Start the **target** host first, then the **initiator** host. The target prints a copy-paste initiator command; you can also use the templates below.

**Notes**:

- You must set `--role=target` or `--role=initiator`
- Initiator **requires** `--target-host=<target host IP>`
- If dual-machine omits `--transport` / `--direction`, defaults are **`all`** (every supported transport × direction on the platform); multi-run target default `peer_wait_s=300`
- `--transport=all` is **dual-machine only**; with dual `all`, A2 expands to `roce` only (no hccs)
- Default HIXL base port: `16000`; peer TCP coordination ports are derived from each engine port (+10000 or -10000)
- Across machines, prefer `--host=<this host public IP>` on the target so the advertised address is correct
- Dual-machine default `--device_ids=0` (single-machine default is `0,1`)

**1:1 (default pairwise)**:

```bash
# === Target host ===
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=roce --direction=D2rD --host=<TARGET_IP>

# === Initiator host (or copy the command printed by target) ===
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=initiator --transport=roce --direction=D2rD \
  --target-host=<TARGET_IP>
```

**one_to_many** (multiple target NPUs, one initiator NPU):

```bash
# Target host
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=roce --direction=D2rD \
  --pattern=one_to_many --device_ids=0,1,2 --host=<TARGET_IP>

# Initiator host (--num_targets must match target lane count)
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=initiator --transport=roce --direction=D2rD \
  --pattern=one_to_many --num_targets=3 --target-host=<TARGET_IP>
```

**many_to_one** (one target NPU, multiple initiator NPUs):

```bash
# Target host
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=roce --direction=D2rD \
  --pattern=many_to_one --num_initiators=3 --host=<TARGET_IP>

# Initiator host
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=initiator --transport=roce --direction=D2rD \
  --pattern=many_to_one --num_initiators=3 --device_ids=0,1,2 \
  --target-host=<TARGET_IP>
```

### Direct `hixl_comm_bench` Run

Start the target first. The peer TCP coordination port is derived from the engine port (+10000 or -10000).

Binary defaults differ from the Python launcher: `loops=1`; if `--block_sizes` is omitted it equals `transfer_size` (default `128M`); `buffer_size` defaults to `1G`. With `loops=1` the first transfer is often warm-up; use `loops>1` for steady throughput.

```bash
# Target (D2rD / rD2D: remote memory is device)
build/benchmarks/comm_benchmark/hixl_comm_bench \
  --role=target --device_id=1 \
  --local_engine=127.0.0.1:16001 \
  --memory=device --peer_count=1 --peer_wait_s=30 \
  --transport=hccs

# Initiator: D2rD (write)
build/benchmarks/comm_benchmark/hixl_comm_bench \
  --role=initiator --device_id=0 \
  --local_engine=127.0.0.1:16000 \
  --remote_engine=127.0.0.1:16001 \
  --memory=device --remote_memory=device --op=write \
  --transport=hccs --transfer_size=128M --block_sizes=16K:2M --loops=5

# Initiator: rD2D (read) — only change --op=read
#   --memory=device --remote_memory=device --op=read
```

### Python Launcher Parameters (`run_comm_benchmark.py`)

| Parameter | Description | Options | Default |
|---|---|---|---|
| `--direction` | Transfer direction | `D2rD`…`rD2H`, `all` | Single: `D2rD` if transport omitted; `all` if transport set or dual-machine |
| `--transport` | Transport path | `hccs` / `roce` / `fabric_mem` / `uboe` / `ub_rtp` / `ub` / `all` | Single `hccs`; dual `all` (`all` is **dual-only**) |
| `--pattern` | Topology | `pairwise` / `one_to_many` / `many_to_one` | `pairwise` |
| `--role` | Dual-machine role; omit for single-machine | `target` / `initiator` | (none, single) |
| `--target-host` | Target host IP; required when `--role=initiator` | IP | (none) |
| `--host` | Local IP; dual target uses this as advertised address | IP | `127.0.0.1` |
| `--base_hixl_port` | HIXL engine base port | Positive int | `16000` |
| `--num_targets` | Target lane count for one_to_many | Positive int | (none) |
| `--num_initiators` | Initiator lane count for many_to_one | Positive int | (none) |
| `--block_sizes` | Block size list or power-of-two range (units ×1024) | `16K:2M`, `4K,64K,1M` | `16K:2M` |
| `--transfer_size` | Bytes transferred per block-size step | `128M`, `1G` | Binary default `128M` |
| `--buffer_size` | Alloc/register size; must be ≥ `transfer_size` | `1G`, `512M` | Binary default `1G` |
| `--loops` | Full block-ladder repeats | Positive int | `5` (binary default `1`) |
| `--device_ids` | Device ID list | Comma-separated | Single `0,1`; dual `0` |
| `--host_roce_ip` | A5 RoCE host NIC IP (data-plane `LocalCommRes`) | IP, comma-separated | (none) |
| `--peer_wait_s` | Max seconds for target to wait for initiator peers | Positive int | 30 single-run, 300 multi-run |
| `--connect_timeout_ms` | Initiator connect timeout (ms) | Positive int | `60000` |
| `--inter_run_delay_s` | Delay between multi-direction runs (seconds) | Non-negative int | `3` |
| `--output_dir` | CSV output directory (relative to cwd) | Path | `comm_benchmark/output` |
| `--plot` / `--skip_plot` | Generate PNGs for this run's CSVs | Flag | Plot on by default |
| `--report` / `--skip_report` | Dual initiator: write perf.md | Flag | Report on by default |
| `--report_path` | perf.md path | Path | `{output_dir}/perf.md` |
| `--bench_bin` | Path to `hixl_comm_bench` | Path | Auto-detect under `build/benchmarks/...` |
| `-H` / `--hixl_option` | Initialize options forwarded to binary | `KEY=VALUE`, repeatable | (none) |

### Common Binary Parameters (`hixl_comm_bench`)

| Parameter | Description | Default |
|---|---|---|
| `--role` | `target` / `initiator` | Required |
| `--memory` | Local buffer: `host` / `device` | Required (by role) |
| `--remote_memory` | Initiator: remote buffer type | Required on initiator |
| `--op` | Initiator: `read` / `write` / `mix` | Required on initiator |
| `--transport` | `hccs` / `roce` / `fabric_mem` / `uboe` / `ub_rtp` / `ub` | `hccs` |
| `--device_id` | Device id (comma list allowed) | Initiator `0`, target `1` |
| `--local_engine` / `--remote_engine` | HIXL endpoint `host:port` | See binary usage |
| `--peer_count` | Initiators the target waits for | `1` |
| `--peer_wait_s` | Target connect-phase timeout (seconds) | `30` |
| `--transfer_size` | Bytes per block-size step | `128M` |
| `--buffer_size` | Alloc/register size | `1G` |
| `--block_sizes` | Block list/range; if omitted equals `transfer_size` | `= transfer_size` |
| `--loops` / `-n` | Repeat full ladder | `1` |
| `--use_async` / `--async_batch_num` | Async transfer | off / `1` |
| `--connect_timeout_ms` | Connect timeout | `60000` |
| `--host_roce_ip` | A5 RoCE data-plane IP | (none) |
| `--group` | Result group name | `default` |
| `--output_dir` | CSV/JSONL directory | `output` |
| `-H` / `--hixl_option` | `Initialize()` option `KEY=VALUE` | (none) |

Full list: run `build/benchmarks/comm_benchmark/hixl_comm_bench` with no args to print usage.

### Support Matrix

| Platform | HCCS | RoCE | FabricMem | UBOE / UB_RTP / UB |
|---|---|---|---|---|
| **A2** | D2rD, rD2D | All 8 directions | Not supported | Not supported |
| **A3** | D2rD, rD2D, H2rD, rD2H | All 8 directions | All 8 directions | Not supported |
| **A5** | Not supported | All 8 directions (needs `--host_roce_ip`) | All 8 directions | All 8 directions |

Transports expanded by dual-machine `--transport=all`:

- A2: `roce`
- A3: `hccs`, `roce`, `fabric_mem`
- A5: `roce`, `fabric_mem`, `uboe`, `ub_rtp`, `ub`

---

## KV Benchmark (`hixl_kv_bench`)

Simulates a KV pooling scenario and measures put/get performance for model shapes and KV block counts.

### Model Support

| Model | Layers | Attention Type | KV Strategy | Description |
|---|---|---|---|---|
| `deepseek-r1` | 61 | MLA | shared | Equal MLA cache per key |
| `glm5` | 78 | MLA + DSA | shared | Equal MLA + DSA cache per key |
| `deepseek-v4` | 61 | Hybrid CSA/HCA + SWA | shared | SWA (`max_key_count=1`) only transfers key0, one copy per layer |

**shared strategy**: MLA models share one KV Cache across inference ranks. Rank 0 puts all keys; all ranks get in parallel.

`total_bytes` / `total_transfer_bytes` in logs and CSV/JSON is the **actual sum of transferred bytes for all keys in the workload** (by slice, honoring `max_key_count`), not “single-key size × key_count”.

### Running Examples

```bash
# Default transport by platform: A2=roce, A3/A5=fabric_mem
python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py \
  --model=deepseek-r1

# More detailed parameters
python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py \
  --num_processes=8 \
  --devices=0,1,2,3,4,5,6,7 \
  --model=deepseek-r1 \
  --key_counts=16,32,48,64 \
  --transport=fabric_mem
```

### KV Parameters

| Parameter | Description | Default |
|---|---|---|
| `--model` | Model configuration name | `deepseek-r1` |
| `--key_counts` | KV block/key counts, comma-separated | `16,32,48,64` |
| `--num_processes` | Concurrent processes (inference ranks) | Platform-dependent, usually `8` |
| `--devices` | Device ID list | Platform `0..N-1` |
| `--transport` | `roce` / `fabric_mem` / `uboe` / `ub_rtp` / `ub` (last three A5 only) | A2=`roce`; A3/A5=`fabric_mem` |
| `--platform` | Force `a2`/`a3`/`a5` (affects default ranks and transport) | Auto-detect |
| `--warmup` / `--repeat` | Warmup / measured repeats | `1` / `10` |
| `--transfer_threads` | Worker threads for concurrent key transfers | `8` |
| `--local_buffer_min` | Minimum local device buffer | `1G` |
| `--output_dir` | Output directory (relative to cwd) | `kv_benchmark/output` |
| `--skip_plot` | Skip plot generation | off |

---

## Directory Structure

```sh
benchmarks/
├── README.md / README_en.md
├── run_all_bench.sh                        # One-command full run (entry)
├── run_all_benchmarks.py                   # Python orchestration
├── platform_detect.py                      # npu-smi platform detection
├── kv_defaults.py                          # KV defaults by platform
├── benchmark_log.py                        # Logging setup
├── performance.md / performance_en.md      # Multi-platform summary (manual)
├── performance/
│   └── render_perf_md.py                   # CSV → perf.md + charts
├── comm_benchmark/
│   ├── hixl_comm_bench.cpp                 # Comm benchmark main
│   ├── common/
│   │   ├── benchmark_config.h/cpp          # CLI / config
│   │   ├── tcp_client_server.h/cpp         # Peer TCP coordination
│   │   ├── client_runner.cc                # Initiator logic
│   │   └── server_runner.cc                # Target logic
│   ├── scripts/
│   │   ├── run_comm_benchmark.py           # Launcher
│   │   └── plot_comm_benchmark.py          # Plotting
│   └── output/                             # CSV output (created at runtime)
└── kv_benchmark/
    ├── hixl_kv_bench.cpp                   # KV benchmark main
    ├── kv_transfer_executor.h/cpp          # Transfer execution
    ├── kvstore/
    │   ├── kvstore.h/cpp                   # KV store simulation
    │   ├── model_config.h/cpp              # Model config load
    │   ├── segment_manager.h/cpp           # Segment management
    │   └── kv_slice_layout.h/cpp           # Slice layout
    ├── config/
    │   └── models.json                     # Model parameters
    ├── scripts/
    │   ├── run_kv_benchmark.py             # Launcher
    │   └── plot_kv_benchmark.py            # Plotting
    └── output/                             # Output (created at runtime)
```
