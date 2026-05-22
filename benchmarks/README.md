# HIXL Benchmarks

本目录包含通信与 KV Cache 场景的基准测试，用于测量 HIXL 在不同配置下的传输性能。

- [环境要求](#环境要求)
- [快速开始](#快速开始)
- [通信 Benchmark](#通信-benchmark-hixl_comm_bench)
- [KV Benchmark](#kv-benchmark-hixl_kv_bench)
- [目录结构](#目录结构)


## 环境要求

### 1. 硬件和软件准备

- 芯片：Atlas A3 训练/推理系列产品、Atlas 800I A2 推理产品/A200I A2 Box 异构组件、Ascend 950PR/Ascend 950DT
- 参考 [环境准备](../docs/build.md#环境准备) 完成昇腾AI软件栈在运行环境上的部署

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

- **HCCS**：在 **A2（Ascend910B-class）** 上仅 **D2D**（`--type=D2rD` / `rD2D`）；在 **A3（Ascend910-class）** 上还支持 **H2rD** / **rD2H**。


## 快速开始

### 1. 编译

在仓库根目录运行：

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
- 依次跑完所有支持的通信方向 × 传输类型的组合
- 运行 KV Cache 基准测试
- 生成 `benchmarks/perf.md` 和折线统计图
- 在终端打印性能结果

跑完后打开 `benchmarks/perf.md` 即可看到结果。

### 3. 自定义参数

```bash
# 自定义通信基准重复轮数和设备
bash benchmarks/run_all_bench.sh --loops 10 --device-ids 0,1,2,3,4,5,6,7

# 只跑通信测试
bash benchmarks/run_all_bench.sh --skip-comm

# 只跑 KV 测试
bash benchmarks/run_all_bench.sh --skip-kv

# 向通信基准传入 HIXL Initialize() 选项（与 hixl_comm_bench 的 -H=KEY=VALUE 一致，可重复）
bash benchmarks/run_all_bench.sh --hixl-option 'LocalCommRes={"version":"1.3"}'
```

### 4. 性能数据汇总

- **`perf.md`**：由 `run_all_bench.sh` 在单平台（A2 / A3 / A5）上自动生成，包含当前平台的性能表格和折线图。
- **`performance.md`**：多平台汇总文档（按 A2 / A3 / A5 等章节区分），由开发者手动维护，内容来源于各平台跑出来的 `perf.md`。

---

## 通信 Benchmark (`hixl_comm_bench`)

测量 HIXL 在不同方向、不同传输类型下的 block 传输带宽。 带宽数据「GB/s」按 **十进制** 定义：**1 GB = 10⁹ 字节**。

**概念**：
- **Initiator**：发起传输的一方（read / write）
- **Target**：响应传输的一方（注册内存，等待 initiator 连接）
- **方向**：由 Initiator 内存类型 + Target 内存类型 + 操作类型决定

### 方向命名

方向名格式为 `源 → 远程目标`，其中 **D**=Device、**H**=Host、**r**=remote。

| 方向 | 含义 | 操作 |
| :--- | :--- | :---: |
| **D2rD** | Device 写往远程 Device | write |
| **rD2D** | 从远程 Device 读回 Device | read |
| **D2rH** | Device 写往远程 Host | write |
| **rH2D** | 从远程 Host 读回 Device | read |
| **H2rH** | Host 写往远程 Host | write |
| **rH2H** | 从远程 Host 读回 Host | read |
| **H2rD** | Host 写往远程 Device | write |
| **rD2H** | 从远程 Device 读回 Host | read |

### 单机运行

```bash
# 快速测试一个方向
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py --type=D2rD --transport=hccs

# 指定设备和端口
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --type=D2rH --transport=rdma --device_ids=0,1

# 一对多模式
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=one_to_many --device_ids=0,1,2,3,4 \
  --type=D2rD --transport=hccs

# 多对一模式
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --pattern=many_to_one --device_ids=0,1,2,3,4 \
  --type=D2rD --transport=hccs

# 传入 HIXL Initialize 选项（与 hixl_comm_bench 的 -H=KEY=VALUE 相同，可多次 -H）
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --type=D2rD --transport=hccs \
  -H 'LocalCommRes={"version":"1.3"}'
```

### 双机运行

**1:1（默认）**：

```bash
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=rdma
# initiator：复制 target 打印的命令
```

**one_to_many**（target 多 NPU，initiator 单 NPU）：

```bash
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=rdma --pattern=one_to_many --device_ids=0,1,2
# initiator：复制 target 打印的命令
```

**many_to_one**（target 单 NPU，initiator 多 NPU）：

```bash
python3 benchmarks/comm_benchmark/scripts/run_comm_benchmark.py \
  --role=target --transport=rdma --pattern=many_to_one --num_initiators=3
# initiator：复制 target 打印的命令
```

### 关键参数

| 参数 | 说明                                                                                  | 可选值 | 默认值          |
|---|-------------------------------------------------------------------------------------|---|--------------|
| `--type` | 传输方向                                                                                | `D2rD`, `rD2D`, `D2rH`, `rH2D`, `H2rH`, `rH2H`, `H2rD`, `rD2H` | `D2rD`       |
| `--transport` | 传输路径                                                                                | `hccs` / `rdma` / `fabric_mem` | `hccs`       |
| `--pattern` | 通信拓扑                                                                                | `pairwise` / `one_to_many` / `many_to_one` | `pairwise`   |
| `--start_block_size` | 起始 block 大小（字节）                                                                     | 整数 | 16384 (16K)  |
| `--max_block_size` | 最大 block 大小，2 倍递增                                                                   | 整数 | 2097152 (2M) |
| `--loops` | 重复运行的次数                                                                             | 正整数 | 5            |
| `--device_ids` | 使用的设备 ID 列表                                                                         | 逗号分隔 | `0,1`        |
| `--plot` | 为本次运行新增或更新的 CSV 生成 PNG 图                                                            | 开关 | 开启           |
| `--skip_plot` | 跳过 PNG 图生成                                                                          | 开关 | 关闭           |
| `--soc_variant` | 传给 `hixl_comm_bench` 的SOC：`auto`／`a2`／`a3`／`a5` | `auto`、`a2`、`a3`、`a5` | 默认自动推断 |
| `-H` / `--hixl_option` | 传给 `hixl_comm_bench` 的初始化选项                                                         | `KEY=VALUE`，可重复 | （无）          |

### 支持情况

| 平台 | HCCS                   | ROCE (RDMA) | FabricMem |
|---|------------------------|---|-----------|
| **A2** | D2rD, rD2D             | 全部 8 个方向 | 不支持       |
| **A3** | D2rD, rD2D, H2rD, rD2H | 全部 8 个方向 | 全部 8 个方向  |

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
python3 benchmarks/kv_benchmark/scripts/run_kv_benchmark.py \
  --model=deepseek-r1 \
  --transport=fabric_mem

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
| `--num_processes` | 并发进程数（模拟推理 rank 数） | `8` |
| `--transport` | 传输路径 | `fabric_mem` |

---

## 目录结构

```
benchmarks/
├── README.md
├── run_all_bench.sh                        # 一键运行全部测试（入口）
├── run_all_benchmarks.py                   # Python 编排脚本
├── performance.md                          # 多平台汇总文档（手动维护）
├── performance/
│   ├── render_perf_md.py                   # CSV → perf.md 渲染 + 图表
│   └── figures/                            # 自动生成的折线图
├── comm_benchmark/
│   ├── hixl_comm_bench.cpp                 # 通信测试主程序
│   ├── common/
│   │   ├── benchmark_config.h/cpp          # 参数配置
│   │   ├── client_runner.cc                # Initiator 逻辑
│   │   └── server_runner.cc                # Target 逻辑
│   ├── scripts/
│   │   ├── run_comm_benchmark.py           # 启动脚本
│   │   └── plot_comm_benchmark.py          # 画图脚本
│   └── output/                             # 测试输出 (CSV)
└── kv_benchmark/
    ├── hixl_kv_bench.cpp                   # KV 测试主程序
    ├── kvstore/
    │   ├── kvstore.h/cpp                   # KV 存储模拟
    │   ├── model_config.h/cpp              # 模型配置加载
    │   └── segment_manager.h/cpp           # 内存段管理
    ├── config/
    │   └── models.json                     # 模型参数配置
    ├── scripts/
    │   ├── run_kv_benchmark.py             # 启动脚本
    │   └── plot_kv_benchmark.py            # 画图脚本
    └── output/                             # 测试输出
```
