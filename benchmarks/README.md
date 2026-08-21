# HIXL Benchmarks

本目录包含通信与 KV Cache 场景的基准测试，用于测量 HIXL 在不同配置下的传输性能。

- [环境要求](#环境要求)
- [快速开始](#快速开始)
- [通信 Benchmark](#通信-benchmark-hixl_comm_bench)
- [KV Benchmark](#kv-benchmark-hixl_kv_bench)
- [目录结构](#目录结构)
- [性能数据](performance.md)

## 环境要求

### 1. 硬件和软件准备

- 芯片：Atlas A3 训练/推理系列产品、Atlas 800I A2 推理产品/A200I A2 Box 异构组件、Ascend 950PR/Ascend 950DT
- 参考 [环境准备](../docs/zh/build.md#环境准备) 完成昇腾AI软件栈在运行环境上的部署

### 2. Device 连通性检查

在执行样例前，请先使用驱动包提供的 [hccn_tool 工具](https://support.huawei.com/enterprise/zh/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference) 检查**两个 device 之间的连通性**。以 A2 场景为例：

> 若 `hccn_tool` 命令找不到，可在 CANN 驱动包安装目录下搜索可执行文件（默认 `/usr/local/Ascend/driver/tools/hccn_tool`），并可 `ln -s` 到 `PATH`。

- Step1：查询所需 device 的 IP 信息，以 8 卡为例：

```shell
for i in {0..7}; do hccn_tool -i $i -ip -g; done
```

- Step2：检查两个 device 之间的连通性，以设备 a 和 b 为例：

```shell
hccn_tool -i ${device_id_a} -ping -g address ${ip_address_b}
hccn_tool -i ${device_id_b} -ping -g address ${ip_address_a}
```

若返回 recv time out seq 字样，说明两个设备之间不连通。

- A3一卡双die之间RDMA可能不通；即便环境配置后也可能 `ping` 不通，最准确的判断方式是使用 `roce_test ib_send_bw` 打流：

```bash
# 接收端
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test reset
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test ib_send_bw -s 65536 -n 1000 -tcp

# 发送端
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test reset
PEER_IP=$(/usr/local/Ascend/driver/tools/hccn_tool -i 0 -ip -g 2>/dev/null | sed -n 's/^ipaddr:\(.*\)/\1/p' | head -1)
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test ib_send_bw -s 65536 -n 1000 address "$PEER_IP" -tcp
```

- Step3：检查设备之间 TLS 证书配置一致性：

```shell
for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch
```

TLS 使能的设备与 TLS 不使能的设备无法建链。示例（关闭 TLS）：

```shell
for i in {0..7}; do hccn_tool -i $i -tls -s enable 0; done
```

若出现 `hccn_tool is busy, please try again`，避免并发使用该命令并稍后重试。

### 约束说明

- **HCCS**：
  - **A2（Ascend910B-class）**：仅 D2D（`D2rD` / `rD2D`）
  - **A3（Ascend910-class）**：D2D + `H2rD` / `rD2H`
  - **A5（Ascend950）**：不支持 HCCS
- **FabricMem**：A2 不支持；A3 支持全部 8 个方向，A5 暂不支持
- **RoCE（A5）**：数据面走 Host NIC，通常需要 `--host_roce_ip`（与控制面 `--target-host` / `--local_engine` 不是同一地址）
- **UBOE / UB_RTP / UB**：仅 A5

## 快速开始

请在**仓库根目录**执行下列命令（相对路径与默认 `output_dir` 均依赖 cwd）。

### 入口说明

| 入口 | 用途 |
| --- | --- |
| `bash benchmarks/run_all_bench.sh` | 单机一键跑全量通信 + KV，生成 `benchmarks/perf.md` |
| `python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py` | 通信基准启动器（单机 / 双机编排） |
| `build/benchmarks/comm_benchmark/hixl_comm_bench` | 直接跑 C++ binary（需自行起 target / initiator） |
| `python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py` | KV Cache 基准 |

### 1. 编译

```bash
bash build.sh --examples
```

编译后的可执行文件在 `build/benchmarks/` 下：

- `comm_benchmark/hixl_comm_bench` — 通信性能测试
- `kv_benchmark/hixl_kv_bench` — KV Cache 场景测试

### 2. 一键运行全部测试

```bash
bash benchmarks/run_all_bench.sh
```

这个脚本会自动：

- 检查并 source CANN 环境
- 通过 `npu-smi info` 检测芯片型号（A2 / A3 / A5；设备名含 Ascend910B→A2、Ascend910→A3、Ascend950→A5）
- 依次跑完当前平台支持的通信方向 × 传输类型组合
- 运行 KV Cache 基准测试（transport 随平台：A2=`roce`，A3/A5=`fabric_mem`）
- 生成 `benchmarks/perf.md` 和折线统计图
- 在终端打印性能结果

跑完后打开 `benchmarks/perf.md` 即可看到结果。

> `run_all_bench.sh` **仅支持单机**。双机请用 `run_comm_benchmark.py --role=target|initiator`（见下文）。`--target-host` 已废弃，传入非 `127.0.0.1` 会直接报错退出。

### 3. 自定义参数

```bash
# 自定义通信基准重复轮数和设备
bash benchmarks/run_all_bench.sh --loops 10 --device-ids 0,1,2,3,4,5,6,7

# 跳过 npu-smi，强制指定平台
bash benchmarks/run_all_bench.sh --platform a3

# 指定 perf.md 输出路径
bash benchmarks/run_all_bench.sh --output /tmp/perf.md

# 只跑KV测试
bash benchmarks/run_all_bench.sh --skip-comm

# 只跑通信测试
bash benchmarks/run_all_bench.sh --skip-kv

# 向通信基准传入 HIXL Initialize() 选项（与 hixl_comm_bench 的 -H=KEY=VALUE 一致，可重复）
bash benchmarks/run_all_bench.sh --hixl-option 'LocalCommRes={"version":"1.3"}'
```

### 4. 性能数据汇总

- **`perf.md`**：由 `run_all_bench.sh` 在单平台（A2 / A3 / A5）上自动生成，包含当前平台的性能表格和折线图。
- **`performance.md`**：多平台汇总文档，由开发者手动维护，内容来源于各平台跑出来的 `perf.md`。

---

## 通信 Benchmark (`hixl_comm_bench`)

测量 HIXL 在不同方向、不同传输类型下的 block 传输带宽。

- 带宽「GB/s」按 **十进制**：**1 GB = 10⁹ 字节**
- 参数中的 `K` / `M` / `G` / `T`（如 `16K`、`128M`）按 **二进制 1024** 解析

**概念**：

- **Initiator**：发起传输的一方（read / write）
- **Target**：响应传输的一方（注册内存，等待 initiator 连接）
- **方向**：由 Initiator 本地内存类型 + Target 远端内存类型 + 操作类型决定

### 方向命名

方向名格式为 `源 → 远程目标`，其中 **D**=Device、**H**=Host、**r**=remote。

站在 **Initiator** 视角：

| 方向 | Initiator 本地 | Target 远端 | 操作 | 含义 |
| :--- | :---: | :---: | :---: | :--- |
| **D2rD** | device | device | write | Device 写往远程 Device |
| **rD2D** | device | device | read | 从远程 Device 读回 Device |
| **D2rH** | device | host | write | Device 写往远程 Host |
| **rH2D** | device | host | read | 从远程 Host 读回 Device |
| **H2rH** | host | host | write | Host 写往远程 Host |
| **rH2H** | host | host | read | 从远程 Host 读回 Host |
| **H2rD** | host | device | write | Host 写往远程 Device |
| **rD2H** | host | device | read | 从远程 Device 读回 Host |

### 单机运行

使用 `run_comm_benchmark.py`（推荐）。默认在本机同时拉起 target 与 initiator。

```bash
# 快速测试一个方向（单机默认 transport=hccs、direction=D2rD）
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --direction=D2rD --transport=hccs

# 指定设备和 block size 范围
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --direction=D2rH --transport=roce --device_ids=0,1 --block_sizes=16K:2M

# 一对多：device_ids 中前 N-1 个为 target，最后一个为 initiator
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=one_to_many --device_ids=0,1,2,3,4 \
  --direction=D2rD --transport=hccs

# 多对一：第一个为 target，其余为 initiator
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=many_to_one --device_ids=0,1,2,3,4 \
  --direction=D2rD --transport=hccs

# 传入 HIXL Initialize 选项（与 hixl_comm_bench 的 -H=KEY=VALUE 相同，可多次 -H）
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --direction=D2rD --transport=hccs \
  -H 'LocalCommRes={"version":"1.3"}'
```

> 单机**不支持** `--transport=all`（会报错退出）。若显式指定了 `--transport` 且未写 `--direction`，direction 默认为该 transport 在当前平台上支持的全部方向。

### 双机运行

先在 **target 机**启动，再在 **initiator 机**启动。target 会打印一条可复制的 initiator 命令；也可按下述模板手写。

**要点**：

- 必须指定 `--role=target` 或 `--role=initiator`
- initiator **必须**带 `--target-host=<target 机 IP>`
- 双机若省略 `--transport` / `--direction`，默认跑 **`all`**（当前平台支持的全部 transport × direction），耗时长；多轮时 target 默认 `peer_wait_s=300`
- `--transport=all` **仅双机**可用；双机 `all` 时 A2 实际只扩 `roce`（不含 hccs）
- 默认 HIXL base port：`16000`；peer TCP 协调端口由各 engine 端口内部派生（+10000 或 -10000）
- 跨机建议 target 侧加 `--host=<本机对外 IP>`，避免广告到错误地址
- 双机默认 `--device_ids=0`（与单机默认 `0,1` 不同）

**1:1（默认 pairwise）**：

```bash
# === Target 机 ===
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=roce --direction=D2rD --host=<TARGET_IP>

# === Initiator 机（也可直接复制 target 打印的命令）===
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=initiator --transport=roce --direction=D2rD \
  --target-host=<TARGET_IP>
```

**one_to_many**（target 多 NPU，initiator 单 NPU）：

```bash
# Target 机
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=roce --direction=D2rD \
  --pattern=one_to_many --device_ids=0,1,2 --host=<TARGET_IP>

# Initiator 机（需 --num_targets 与 target 侧 lane 数一致）
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=initiator --transport=roce --direction=D2rD \
  --pattern=one_to_many --num_targets=3 --target-host=<TARGET_IP>
```

**many_to_one**（target 单 NPU，initiator 多 NPU）：

```bash
# Target 机
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=roce --direction=D2rD \
  --pattern=many_to_one --num_initiators=3 --host=<TARGET_IP>

# Initiator 机
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=initiator --transport=roce --direction=D2rD \
  --pattern=many_to_one --num_initiators=3 --device_ids=0,1,2 \
  --target-host=<TARGET_IP>
```

### 直接运行 `hixl_comm_bench`

target 进程先启动。peer TCP 协调端口由 engine 端口自动派生（+10000 或 -10000）。

binary 默认值与 Python 启动器不同：`loops=1`，未指定 `--block_sizes` 时等于 `transfer_size`（默认 `128M`），`buffer_size` 默认 `1G`。`loops=1` 时首轮常为 warm-up，稳态吞吐建议 `loops>1`。

```bash
# Target（D2rD / rD2D 场景：远端为 device）
build/benchmarks/comm_benchmark/hixl_comm_bench \
  --role=target --device_id=1 \
  --local_engine=127.0.0.1:16001 \
  --memory=device --peer_count=1 --peer_wait_s=30 \
  --transport=hccs

# Initiator：D2rD（write）
build/benchmarks/comm_benchmark/hixl_comm_bench \
  --role=initiator --device_id=0 \
  --local_engine=127.0.0.1:16000 \
  --remote_engine=127.0.0.1:16001 \
  --memory=device --remote_memory=device --op=write \
  --transport=hccs --transfer_size=128M --block_sizes=16K:2M --loops=5

# Initiator：rD2D（read）—— 仅改 --op=read
#   --memory=device --remote_memory=device --op=read
```

### Python 启动器参数（`run_comm_benchmark.py`）

| 参数 | 说明 | 可选值 | 默认值 |
|---|---|---|---|
| `--direction` | 传输方向 | `D2rD`…`rD2H`, `all` | 单机：未指定 transport 时 `D2rD`；已指定 transport 或双机：`all` |
| `--transport` | 传输路径 | `hccs` / `roce` / `fabric_mem` / `uboe` / `ub_rtp` / `ub` / `all` | 单机 `hccs`；双机 `all`（`all` **仅双机**） |
| `--pattern` | 通信拓扑 | `pairwise` / `one_to_many` / `many_to_one` | `pairwise` |
| `--role` | 双机角色；省略则为单机 | `target` / `initiator` | （无，单机） |
| `--target-host` | target 机 IP；`--role=initiator` 时必填 | IP | （无） |
| `--host` | 本机 IP；双机 target 用作广告地址 | IP | `127.0.0.1` |
| `--base_hixl_port` | HIXL engine 起始端口 | 正整数 | `16000` |
| `--num_targets` | one_to_many 的 target lane 数 | 正整数 | （无） |
| `--num_initiators` | many_to_one 的 initiator lane 数 | 正整数 | （无） |
| `--block_sizes` | block size 列表或 2 倍递增范围（单位按 1024） | `16K:2M`, `4K,64K,1M` | `16K:2M` |
| `--transfer_size` | 每个 block-size 档位的总传输量 | `128M`, `1G` | binary 默认 `128M` |
| `--buffer_size` | 分配/注册 buffer 大小，需 ≥ `transfer_size` | `1G`, `512M` | binary 默认 `1G` |
| `--loops` | 完整 block ladder 重复次数 | 正整数 | `5`（binary 默认 `1`） |
| `--device_ids` | 设备 ID 列表 | 逗号分隔 | 单机 `0,1`；双机 `0` |
| `--host_roce_ip` | A5 RoCE host NIC IP（数据面 `LocalCommRes`） | IP，逗号分隔 | （无） |
| `--peer_wait_s` | target 等待 initiator 连接的最大秒数 | 正整数 | 单次 30，多轮 300 |
| `--connect_timeout_ms` | initiator 连接超时（ms） | 正整数 | `60000` |
| `--inter_run_delay_s` | 多轮方向之间的间隔秒数 | 非负整数 | `3` |
| `--output_dir` | CSV 输出目录（相对 cwd） | 路径 | `comm_benchmark/output` |
| `--plot` / `--skip_plot` | 是否为本次 CSV 生成 PNG | 开关 | 默认生成 |
| `--report` / `--skip_report` | 双机 initiator 是否生成 perf.md | 开关 | 默认生成 |
| `--report_path` | perf.md 路径 | 路径 | `{output_dir}/perf.md` |
| `--bench_bin` | `hixl_comm_bench` 路径 | 路径 | 自动探测 `build/benchmarks/...` |
| `-H` / `--hixl_option` | 传给 binary 的 Initialize 选项 | `KEY=VALUE`，可重复 | （无） |

### 直接运行 binary 的常用参数（`hixl_comm_bench`）

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--role` | `target` / `initiator` | 必填 |
| `--memory` | 本地 buffer：`host` / `device` | 必填（按角色） |
| `--remote_memory` | initiator：远端 buffer 类型 | initiator 必填 |
| `--op` | initiator：`read` / `write` / `mix` | initiator 必填 |
| `--transport` | `hccs` / `roce` / `fabric_mem` / `uboe` / `ub_rtp` / `ub` | `hccs` |
| `--device_id` | 设备 ID（可逗号列表） | initiator `0`，target `1` |
| `--local_engine` / `--remote_engine` | HIXL endpoint `host:port` | 见 binary usage |
| `--peer_count` | target 等待的 initiator 数 | `1` |
| `--peer_wait_s` | target 连接阶段超时（秒） | `30` |
| `--transfer_size` | 每档总传输量 | `128M` |
| `--buffer_size` | 分配/注册大小 | `1G` |
| `--block_sizes` | block 列表/范围；省略则等于 `transfer_size` | `= transfer_size` |
| `--loops` / `-n` | 重复完整 ladder | `1` |
| `--use_async` / `--async_batch_num` | 异步传输 | 关闭 / `1` |
| `--connect_timeout_ms` | 连接超时 | `60000` |
| `--host_roce_ip` | A5 RoCE 数据面 IP | （无） |
| `--group` | 结果分组名 | `default` |
| `--output_dir` | CSV/JSONL 目录 | `output` |
| `-H` / `--hixl_option` | `Initialize()` 选项 `KEY=VALUE` | （无） |

完整列表见：`build/benchmarks/comm_benchmark/hixl_comm_bench`（无参数时打印 usage）。

### 支持情况

| 平台 | HCCS | RoCE | FabricMem | UBOE / UB_RTP / UB |
|---|------------------------|---|-----------|---|
| **A2** | D2rD, rD2D | 全部 8 个方向 | 不支持 | 不支持 |
| **A3** | D2rD, rD2D, H2rD, rD2H | 全部 8 个方向 | 全部 8 个方向 | 不支持 |
| **A5** | 不支持 | 全部 8 个方向（需 `--host_roce_ip`） | 不支持 | 全部 8 个方向 |

双机 `--transport=all` 时各平台实际 transport 列表：

- A2：`roce`
- A3：`hccs`, `roce`, `fabric_mem`
- A5：`roce`, `uboe`, `ub_rtp`, `ub`

---

## KV Benchmark (`hixl_kv_bench`)

模拟KV池化场景，按模型形状和 KV block 数量测试 put/get 性能。

### 模型支持

| 模型 | 层数 | Attention 类型 | KV 策略  | 说明 |
|---|---|---|--------|---|
| `deepseek-r1` | 61 | MLA | shared | 每 key 等量 MLA cache |
| `glm5` | 78 | MLA + DSA | shared | 每 key 等量 MLA + DSA cache |
| `deepseek-v4` | 61 | Hybrid CSA/HCA + SWA | shared | SWA（`max_key_count=1`）仅 key0 传输，每层一份 |

**shared 策略**：MLA 模型所有推理 rank 共享同一份 KV Cache。测试中 rank 0 负责写入（put）全部 key，所有 rank 并行读取（get）。

日志与 CSV/JSON 的 `total_bytes` / `total_transfer_bytes` 表示**该 workload 下所有 key 的实际传输字节总和**（按 slice 汇总，尊重 `max_key_count`），不是「单 key 大小 × key_count」。

### 运行示例

```bash
# transport 默认随平台：A2=roce，A3/A5=fabric_mem
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

### KV 参数

| 参数 | 说明 | 默认值 |
|---|---|---|
| `--model` | 模型配置名 | `deepseek-r1` |
| `--key_counts` | 测试的 KV block/key 数量，逗号分隔 | `16,32,48,64` |
| `--num_processes` | 并发进程数（模拟推理 rank 数） | 随平台，通常 `8` |
| `--devices` | 设备 ID 列表 | 随平台 `0..N-1` |
| `--transport` | 传输路径：`roce` / `fabric_mem` / `uboe` / `ub_rtp` / `ub`（后三者仅 A5） | A2=`roce`；A3/A5=`fabric_mem` |
| `--platform` | 强制平台 `a2`/`a3`/`a5`（影响默认 rank 数与 transport） | 自动检测 |
| `--warmup` / `--repeat` | 预热次数 / 正式重复次数 | `1` / `10` |
| `--transfer_threads` | 并发传 key 的工作线程数 | `8` |
| `--local_buffer_min` | 本地 device buffer 下限 | `1G` |
| `--output_dir` | 输出目录（相对 cwd） | `kv_benchmark/output` |
| `--skip_plot` | 跳过画图 | 关闭 |

---

## 目录结构

```sh
benchmarks/
├── README.md / README_en.md
├── run_all_bench.sh                        # 一键运行全部测试（入口）
├── run_all_benchmarks.py                   # Python 编排脚本
├── platform_detect.py                      # npu-smi 平台检测
├── kv_defaults.py                          # KV 按平台默认参数
├── benchmark_log.py                        # 日志配置
├── performance.md / performance_en.md      # 多平台汇总（手动维护）
├── performance/
│   └── render_perf_md.py                   # CSV → perf.md 渲染 + 图表
├── comm_benchmark/
│   ├── hixl_comm_bench.cpp                 # 通信测试主程序
│   ├── common/
│   │   ├── benchmark_config.h/cpp          # 参数配置
│   │   ├── tcp_client_server.h/cpp         # peer TCP 协调
│   │   ├── client_runner.cc                # Initiator 逻辑
│   │   └── server_runner.cc                # Target 逻辑
│   ├── scripts/
│   │   ├── run_comm_benchmark.py           # 启动脚本
│   │   └── plot_comm_benchmark.py          # 画图脚本
│   └── output/                             # 测试输出 (CSV，运行后生成)
└── kv_benchmark/
    ├── hixl_kv_bench.cpp                   # KV 测试主程序
    ├── kv_transfer_executor.h/cpp          # 传输执行
    ├── kvstore/
    │   ├── kvstore.h/cpp                   # KV 存储模拟
    │   ├── model_config.h/cpp              # 模型配置加载
    │   ├── segment_manager.h/cpp           # 内存段管理
    │   └── kv_slice_layout.h/cpp           # slice 布局
    ├── config/
    │   └── models.json                     # 模型参数配置
    ├── scripts/
    │   ├── run_kv_benchmark.py             # 启动脚本
    │   └── plot_kv_benchmark.py            # 画图脚本
    └── output/                             # 测试输出（运行后生成）
```
