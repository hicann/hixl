# KV Benchmark（hixl_kv_bench）

简体中文 | [English](./README_en.md)

模拟KV池化场景，按模型形状和 KV block 数量测试 put/get 性能。

返回[Benchmark 总览](../README.md)查看一键入口与目录结构；环境准备与 Device 连通性检查参见[通信 Benchmark 环境要求](../comm_benchmark/README.md#环境要求)。

## 模型支持

| 模型 | 层数 | Attention 类型 | KV 策略  | 说明 |
|---|---|---|--------|---|
| `deepseek-r1` | 61 | MLA | shared | 每 key 等量 MLA cache |
| `glm5` | 78 | MLA + DSA | shared | 每 key 等量 MLA + DSA cache |
| `deepseek-v4` | 61 | Hybrid CSA/HCA + SWA | shared | SWA（`max_key_count=1`）仅 key0 传输，每层一份 |

**shared 策略**：MLA 模型所有推理 rank 共享同一份 KV Cache。测试中 rank 0 负责写入（put）全部 key，所有 rank 并行读取（get）。

日志与 CSV/JSON 的 `total_bytes` / `total_transfer_bytes` 表示**该 workload 下所有 key 的实际传输字节总和**（按 slice 汇总，尊重 `max_key_count`），不是「单 key 大小 × key_count」。

## 运行示例

```bash
# transport 默认随平台：A2=roce，A3=fabric_mem，A5=ub
python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py \
  --model=deepseek-r1

# 更详细参数
python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py \
  --num_processes=8 \
  --devices=0,1,2,3,4,5,6,7 \
  --model=deepseek-r1 \
  --key_counts=16,32,48,64 \
  --transport=fabric_mem
```

## KV 参数

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--model` | 模型配置名 | `deepseek-r1` |
| `--key_counts` | 测试的 KV block/key 数量，逗号分隔 | `16,32,48,64` |
| `--num_processes` | 并发进程数（模拟推理 rank 数） | 随平台，通常 `8` |
| `--base_port` | rank 0 的基础端口；连续端口范围须在 `1..65535` 内 | `19000` |
| `--devices` | 设备 ID 列表 | 随平台 `0..N-1` |
| `--base_port` | rank 0 使用的基础端口；后续 rank 使用递增端口 | `19000` |
| `--listen_host` | 本地 rank 监听的地址 | `127.0.0.1` |
| `--connect_host` | 本地 rank 连接对端时使用的地址 | `127.0.0.1` |
| `--transport` | 传输路径：`roce` / `fabric_mem` / `uboe` / `ub_rtp` / `ub`（后三者仅 A5） | A2=`roce`；A3=`fabric_mem`；A5=`ub` |
| `--platform` | 强制平台 `a2`/`a3`/`a5`（影响默认 rank 数与 transport） | 自动检测 |
| `--warmup` / `--repeat` | 预热次数 / 正式重复次数 | `1` / `10` |
| `--transfer_threads` | 并发传 key 的工作线程数 | `8` |
| `--local_buffer_min` | 本地 device buffer 下限 | `1G` |
| `--sync_timeout_sec` | 等待各 rank 同步文件就绪的超时时间（秒） | `300` |
| `--output_dir` | 输出目录（相对 cwd） | `kv_benchmark/output` |
| `--skip_plot` | 跳过画图 | 关闭 |
