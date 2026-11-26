# KV Benchmark (hixl_kv_bench)

English | [简体中文](./README.md)

Simulates a KV pooling scenario and measures put/get performance for model shapes and KV block counts.

Return to the [benchmark overview](../README_en.md) for the one-command entry and directory structure; see [Communication Benchmark environment requirements](../comm_benchmark/README_en.md#environment-requirements) for environment setup and device connectivity checks.

## Model Support

| Model | Layers | Attention Type | KV Strategy | Description |
|---|---|---|---|---|
| `deepseek-r1` | 61 | MLA | shared | Equal MLA cache per key |
| `glm5` | 78 | MLA + DSA | shared | Equal MLA + DSA cache per key |
| `deepseek-v4` | 61 | Hybrid CSA/HCA + SWA | shared | SWA (`max_key_count=1`) only transfers key0, one copy per layer |

**shared strategy**: MLA models share one KV Cache across inference ranks. Rank 0 puts all keys; all ranks get in parallel.

`total_bytes` / `total_transfer_bytes` in logs and CSV/JSON is the **actual sum of transferred bytes for all keys in the workload** (by slice, honoring `max_key_count`), not “single-key size × key_count”.

## Running Examples

```bash
# Default transport by platform: A2=roce, A3=fabric_mem, A5=ub
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

## KV Parameters

| Parameter | Description | Default |
|---|---|---|
| `--model` | Model configuration name | `deepseek-r1` |
| `--key_counts` | KV block/key counts, comma-separated | `16,32,48,64` |
| `--num_processes` | Concurrent processes (inference ranks) | Platform-dependent, usually `8` |
| `--devices` | Device ID list | Platform `0..N-1` |
| `--base_port` | Base port for rank 0; each following rank uses the next port | `19000` |
| `--listen_host` | Address for local rank listeners | `127.0.0.1` |
| `--connect_host` | Address local ranks use to connect to peers | `127.0.0.1` |
| `--transport` | `roce` / `fabric_mem` / `uboe` / `ub_rtp` / `ub` (last three A5 only) | A2=`roce`; A3=`fabric_mem`; A5=`ub` |
| `--platform` | Force `a2`/`a3`/`a5` (affects default ranks and transport) | Auto-detect |
| `--warmup` / `--repeat` | Warmup / measured repeats | `1` / `10` |
| `--transfer_threads` | Worker threads for concurrent key transfers | `8` |
| `--local_buffer_min` | Minimum local device buffer | `1G` |
| `--sync_timeout_sec` | Timeout in seconds for rank synchronization files | `300` |
| `--output_dir` | Output directory (relative to cwd) | `kv_benchmark/output` |
| `--skip_plot` | Skip plot generation | off |
