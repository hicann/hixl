## 目录

- [目录](#目录)
- [Benchmarks](#benchmarks)
- [目录结构](#目录结构)
- [环境要求](#环境要求)
- [程序编译](#程序编译)
- [执行前准备](#执行前准备)
- [Benchmark运行](#benchmark运行)
- [FabricMem KV benchmark](#fabricmem-kv-benchmark)
  - [推荐：使用脚本启动并汇总日志](#推荐使用脚本启动并汇总日志)
- [性能数据](#性能数据)

## Benchmarks

该目录提供了HIXL的benchmark性能用例，支持用户根据需要传输的数据大小对benchmark进行改造以快速进行性能测试和评估。

## 目录结构

```
├── benchmarks
|   ├── common                                         // 公共函数目录
|   ├── benchmark.cpp                                  // HIXL的数据传输benchmark用例
|   ├── fabric_mem_kv_benchmark.cpp                    // FabricMem KV 块传输 benchmark（AdxlEngine）
|   ├── fabric_mem_kv_benchmark_summary.awk            // 与运行脚本配套的日志汇总脚本
|   ├── run_fabric_mem_kv_benchmark.sh                 // 多进程启动与日志合并脚本
|   ├── CMakeLists.txt                                 // 编译脚本
```

## 环境要求

### 1. 硬件和软件准备
-   芯片：Atlas A3 训练/推理系列产品、Atlas 800I A2 推理产品/A200I A2 Box 异构组件、Ascend 950PR/Ascend 950DT
-   参考 [环境准备](../docs/build.md#环境准备) 完成昇腾AI软件栈在运行环境上的部署

### 2. Device连通性检查
在执行样例前，请先使用驱动包提供的 [hccn_tool工具](https://support.huawei.com/enterprise/zh/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference) 检查**两个device之间的连通性**。以A2场景为例，检查示例如下：
> 若hccn_tool命令找不到，可在CANN驱动包安装目录下搜索hccn_tool可执行文件(默认路径为`/usr/local/Ascend/driver/tools/hccn_tool`)，并通过`ln -s /usr/local/Ascend/driver/tools/hccn_tool /usr/bin/hccn_tool`手动建立软链。

- step1：查询所需device的ip信息，以8卡为例：
```shell
for i in {0..7}; do hccn_tool -i $i -ip -g; done
```
- step2：检查两个device之间的连通性，以设备a和b连通性检查为例：
```shell
# 检查设备a是否能ping通设备b
hccn_tool -i ${device_id_a} -ping -g address ${ip_address_b}
# 检查设备b是否能ping通设备a
hccn_tool -i ${device_id_b} -ping -g address ${ip_address_a}
```
其中`device_id`为设备id，可通过`npu-smi info`查询；`ip_address`为上一步查询的设备ip地址。

若返回recv time out seq字样，说明两个设备之间不连通，请尝试其他设备。

**注意：** A3环境单卡双die之间不互通，如0号和1号device不通，2号和3号device不通，以此类推，在A3环境执行样例时，请注意传入的device id是否满足连通要求。

- step3：检查设备之间TLS证书配置的一致性
```shell
# 检查设备的TLS状态
for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch

# TLS使能的设备和TLS不使能的设备无法建链，建议统一保持TLS关闭
for i in {0..7}; do hccn_tool -i $i -tls -s enable 0; done
```
**注意：** 如果执行上述命令出现`hccn_tool is busy, please try again`，请确保没有其他进行并发执行该命令，然后重试。

## 程序编译

1. 参考[构建](../docs/build.md)里的**编译执行**章节，利用build.sh附加指定--examples参数进行编译。

2. 编译结束后，在**build/benchmarks**目录下生成可执行文件。

## Benchmark运行

- 说明：
    - 所有benchmark需要成对执行，client侧和server侧启动执行间隔时间不要过长，代码中默认设置kWaitTransTime为20s，用户可根据实际情况自行修改此变量的值以保证用例成功运行。
    - 默认总传输缓冲 `--total_size=134217728`（字节，约 128MiB）。未指定 `--block_size` 时，**第一档块大小与 `total_size` 相同**（整块一次传完）；`--block_steps` 默认为 **1**（只跑第一档）；`--loops` 默认为 **1**。可用 `--block_size`、`--block_steps`、`--loops` 调整块阶梯与重复次数。当 **`loops=1`** 时，程序会提示：**第一次传输多为预热**，稳定吞吐建议看 **第二次及以后** 的日志输出，或将 **`loops` 设为大于 1**（`-n` / `--loops`）。执行成功后会打印类似如下的日志，其中 block size 表示每次传输的内存块大小；transfer num 表示传输次数；time cost 表示总的传输耗时；throughput 表示传输的吞吐（带宽）。
      ```
      [INFO] Transfer success, loop 1/1, block size: 134217728 Bytes, transfer num: 1, time cost: 1044 us, throughput: 119.732 GB/s
      ```
      - 异步传输模式（`--use_async=true`）：批量下发 `async_batch_num` 个异步请求，地址连续划分（每个请求传输 `total_size/async_batch_num` 大小的数据），统一等待完成后统计性能。输出包含 **submit time**（下发阶段耗时）和 **wait time**（等待阶段耗时）：
      ```
      [INFO] Async transfer success, loop 1/1, step 0, block size: 4194304 Bytes, trans_num: 32, async_batch_num: 4, total time: 1044 us (submit: 200 us, wait: 844 us), throughput: 119.732 GB/s
      ```

- 配置环境变量
    - 若运行环境上安装的“Ascend-cann-toolkit”包，环境变量设置如下：

        ```
        source ${HOME}/Ascend/cann/set_env.sh
        ```

      “$HOME/Ascend”请替换相关软件包的实际安装路径。

    - 若运行环境上安装的“CANN-XXX.run”包，环境变量设置如下：

        ```
        source ${HOME}/Ascend/latest/bin/setenv.bash
        ```

      “$HOME/Ascend”请替换相关软件包的实际安装路径。

- 在运行环境执行可执行文件。

  - 执行 benchmark，client-server 模式，使用 **`参数名=值`** 形式传参（除 `--hixl_option` / `-H` 的选项键名外，其余参数名均为小写；支持长名 `--xxx` 与短名 `-x`，见下表）。**必选**：`--role=client` 或 `--role=server`（或 `-r=client` / `-r=server`）。**推荐启动顺序**：先启动 **server** 进程（在 `--tcp_port` 上监听并接受 TCP），再启动 **client**（作为 TCP 客户端连接 `remote_engine` 中 **主机名/IP** 与 `--tcp_port`）。**多 client**：可并行启动多个 client 进程连同一 `tcp_port`；server 在独立线程内 accept，对每个连接下发同一内存地址，建连阶段在达到 **`--tcp_client_count`** 个 TCP 握手完成或墙钟超出 **`--tcp_accept_wait_s`** 时结束（先满足者为准；未满 `tcp_client_count` 且超时则失败）。未指定的项使用默认值（client 默认 `device_id=0`、`local_engine=127.0.0.1:16000`、`remote_engine=127.0.0.1:16001`；server 默认 `device_id=1`、`local_engine=127.0.0.1:16001`、`remote_engine=127.0.0.1`；公共默认 `transfer_mode=d2d`、`transfer_op=read`、`use_buffer_pool=false`、`use_async=false`、`async_batch_num=1`、`connect_timeout=60000`、`tcp_port=20000`、`tcp_accept_wait_s=30`、`tcp_client_count=1`；`total_size=134217728`；**未指定 `-k`/`--block_size` 时 `block_size` 与 `total_size` 相同**；`block_steps=1`；`loops=1`）。可选 **`--hixl_option=KEY=VALUE`**（或 **`-H=KEY=VALUE`**）可重复传入，合并为 `Hixl::Initialize` 的 options；若未指定 `BufferPool` / `adxl.BufferPool` 且 `--use_buffer_pool=false`，程序会自动补 `BufferPool=0:0`（与历史行为一致）。client/server 若使用自定义 HIXL 选项，两端需保持一致。运行 `./benchmark --help` 可查看帮助。

    - 参数说明
        | **参数名** | **短名** | **可选/必选** | **描述** |
        |:-----------|:--------|:------------:|:------------|
        | `--role` | `-r` | 必选 | `client` 或 `server` |
        | `--device_id` | `-d` | 可选 | 单个整数或逗号分隔列表（如 `0,1`）；与 `local_engine`/`remote_engine` 列表长度对齐（单值自动广播到 `max(n_d,n_l,n_r)`） |
        | `--local_engine` | `-l` | 可选 | 本端 HIXL endpoint，单个或逗号分隔；IPv6 用 `[ip]:port`。多值时 client 为每 lane 独立 `Hixl` 与线程；server 仅支持单值 |
        | `--remote_engine` | `-e` | 可选 | client：单个或多个 `host:hixl_port`（逗号分隔）；HIXL 用完整串，TCP 协调用 **host + 对应下标的 `--tcp_port`**（见下）。单 `local_engine` + 多 `remote_engine` 时共用一个 `Hixl`，多路 TCP/HIXL/传输并发，client 缓冲按 remote 数分段。server：单值即可 |
        | `--tcp_port` | `-p` | 可选 | TCP 协调端口（默认 20000）；**由 server 进程监听**。多对多 client 时可为逗号列表，长度与展开后的 endpoint 数一致或填 1 表示广播（与 `device_id` 规则相同）。两台 server 在同一 IP 上须使用不同协调端口，例如 `-p=20000,20001` |
        | `--tcp_accept_wait_s` | `-a` | 可选 | **仅 server**：TCP **建连阶段**从 listen 就绪起的**最长墙钟时间**（秒，默认 30）；达到 `--tcp_client_count` 个连接时可提前结束 |
        | `--tcp_client_count` | `-c` | 可选 | **仅 server**：建连阶段需完成的 TCP client 数（默认 1，最大 65535）。完成后对每个连接下发同一内存地址；随后等待这 `N` 个 client 各发一次完成通知 |
        | `--transfer_mode` | `-m` | 可选 | `d2d`、`h2d`、`d2h`、`h2h` |
        | `--transfer_op` | `-o` | 可选 | `read` 或 `write`（仅 client 侧传输使用该配置） |
        | `--use_buffer_pool` | `-b` | 可选 | `true`/`false` 或 `1`/`0` |
        | `--total_size` | `-t` | 可选 | 总缓冲大小，**十进制字节**（默认 134217728，即 128MiB） |
        | `--block_size` | `-k` | 可选 | 第一档块大小，**十进制字节**（未指定时与 `total_size` 相同） |
        | `--block_steps` | `-s` | 可选 | 块大小档位数：第 i 档为 `block_size * 2^i`（默认 1） |
        | `--loops` | `-n` | 可选 | 整档阶梯重复次数（默认 1；仅 1 次时首传多为预热，可看第二次输出或加大 `loops`） |
        | `--use_async` | `-x` | 可选 | `true`/`false` 或 `1`/`0`（默认 false）。启用异步传输模式，批量下发多个异步请求后统一等待完成 |
        | `--async_batch_num` | `-y` | 可选 | 每批异步请求数量（默认 1）。启用异步模式时，`total_size` 必须能被 `async_batch_num` 整除，且每请求大小 `(total_size/async_batch_num)` 必须能被各档 `block_size` 整除 |
        | `--connect_timeout` | `-C` | 可选 | 建链超时时间，毫秒（默认 60000，即 60 秒） |
        | `--hixl_option` | `-H` | 可选，可重复 | 传入 `Hixl::Initialize(local_engine, options)` 的一项：`--hixl_option=KEY=VALUE`，`KEY` 为完整选项名（区分大小写），如 `LocalCommRes`、`BufferPool`、`RdmaTrafficClass`、`RdmaServiceLevel` 或 `adxl.*` 等；同键多次出现时以后者为准 |

    - 测试HIXL引擎通过HCCS链路进行传输的带宽, 以d2d场景，写操作，不开启中转内存池为例：

        - 执行client benchmark：
            ```
            ./benchmark --role=client --device_id=0 --local_engine=10.10.10.0 --remote_engine=10.10.10.0:16000 --transfer_mode=d2d --transfer_op=write
            ```

        - 执行server benchmark：
            ```
            ./benchmark --role=server --device_id=1 --local_engine=10.10.10.0:16000
            ```

    - 测试HIXL引擎通过RDMA链路进行传输的带宽, 以d2d场景，写操作，不开启中转内存池为例：

        - 执行client benchmark：
            ```
            HCCL_INTRA_ROCE_ENABLE=1 /benchmark --role=client --device_id=0 --local_engine=10.10.10.0 --remote_engine=10.10.10.0:16000 --transfer_mode=d2d --transfer_op=write
            ```

        - 执行server benchmark：
            ```
            HCCL_INTRA_ROCE_ENABLE=1 ./benchmark --role=server --device_id=1 --local_engine=10.10.10.0:16000
            ```
  **注**：HCCL_INTRA_ROCE_ENABLE=1表示使用RDMA进行传输
- 约束说明

    - Atlas 800I A2 推理产品/A200I A2 Box 异构组件，该场景下Server内采用HCCS传输协议时，仅支持d2d。
    - Atlas A3 训练/推理系列产品，该场景下采用HCCS传输协议时，不支持Host内存作为远端Cache。

## FabricMem KV benchmark

### 推荐：使用脚本启动并汇总日志

```shell
cd build/benchmarks

# path_of_benchmark需替换为具体benchmarks所在目录
# 默认 16 进程、日志 fabric_mem_kv_benchmark.log
${path_of_benchmark}/run_fabric_mem_kv_benchmark.sh 127.0.0.1 22000 ./fabric_mem_kv_benchmark

# 指定 world_size=2（烟测）、日志文件
${path_of_benchmark}/run_fabric_mem_kv_benchmark.sh 127.0.0.1 22000 ./fabric_mem_kv_benchmark 2 ./kv_bench.log
```

参数顺序：`host_ip` `base_port` `可执行文件路径` `[world_size]` `[合并日志文件]`。

脚本行为：为每个 rank 落盘独立日志，再按 rank 顺序合并到指定文件，并在文末追加 **SUMMARY**（对各 rank 的 Get 时间与带宽做平均；rank 0 的 Put / Get-max 行从原始日志解析）。

## 性能数据

HIXL在昇腾A2/A3芯片上部分场景传输数据的实测性能,可参见[A2性能数据](A2_benchmark_performance.md)/[A3性能数据](A3_benchmark_performance.md)。

