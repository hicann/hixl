# vllm_hixl_connector

> **实验性模块**：本模块位于 `src/experimental/`，默认不参与编译，需通过 `build.sh --experimental` 打包进 hixl wheel。其 API 与行为可能随时变更或移除，不建议用于生产环境。

## 功能简介

`vllm_hixl_connector` 是一个面向昇腾 NPU 的 vLLM V1 KV Cache Connector。在 PD 分离（Prefill/Decode Disaggregation）推理场景中，它基于 HIXL Engine 的单边通信能力（HCCS/RDMA 点对点 D2D 传输），把 Prefill 实例计算出的 KV Cache 高速搬运到 Decode 实例，从而免去 Decode 侧对 Prompt 的重复预填充。

主要能力：

- **控制面与数据面分离**：控制面走 ZMQ（元数据握手、传输完成通知、块释放），数据面由 Decode 侧通过 HIXL 单边 READ 直接从 Prefill 侧显存拉取，无需 Prefill 侧参与逐块发送。
- **灵活的并行布局**：支持 P/D 两侧不同的 TP/DP 规格、Prefill 侧 PP（含自定义层切分）、PCP/DCP 上下文并行，以及跨节点部署。
- **广泛的模型覆盖**：GQA、MLA（DeepSeek 系列）、稀疏注意力（含 SFA DCP replicated indexer）、压缩 KV（`compress_ratios`）、混合架构（Mamba 等线性层 + 注意力层，HMA）、SWA 滑窗裁剪，以及 MTP/EAGLE3 投机解码。
- **Prefix Cache 非对称**：P/D 两侧命中不同时自动裁剪实际需要拉取的块。
- **拉取后重排**：GQA 多段拉取合并、NZ 格式转换、混合线性层转置，优先使用昇腾融合算子加速。

## 工作原理

连接器在 vLLM 的调度侧与执行侧各持有不同职责，整体传输时序如下：

1. Worker 初始化时创建 HIXL Engine 实例；KV Cache 分配完成后，vLLM 回调 `register_kv_caches` 把显存区域（分配器已按 2M 对齐）注册到引擎（`register_physical_regions`）。
2. Prefill 侧（`kv_producer`）启动 `KVCacheSendingThread`，在每个 rank 的握手端口上以 ZMQ ROUTER 监听；Decode 侧（`kv_consumer`）启动 `KVCacheRecvingThread`。
3. Decode 侧调度器发现新请求需要远端预填充后，Worker 按请求计算传输分片（远端端口、远端/本地块 id 映射），并从 Prefill 侧 ZMQ 拉取 `HIXLAgentMetadata`（含远端 KV 基地址、监听端口、分组布局）。
4. Decode 侧将源/目的地址与长度组织成批量描述符，调用 `Hixl.transfer_async`（READ）发起单边拉取并轮询至完成。
5. 拉取完成后按需重排 KV 布局，随后通过 ZMQ 向 Prefill 侧发送 `DONE_RECVING_MSG`；Prefill 侧确认所有分片完成后延迟释放 KV 块（超时未确认会强制释放并记录错误日志）。

端口规划（均基于 `kv_transfer_config.kv_port`）：

| 端口 | 计算方式 | 用途 |
|------|----------|------|
| 握手端口 | `kv_port + DP 偏移 + device_index` | ZMQ 控制面（元数据、完成信号） |
| 引擎监听端口 | `kv_port + 10000 + DP 偏移 + device_index` | HIXL Engine 数据面端点 |

其中 `DP 偏移 = dp_rank × tp_size × pp_size × pcp_size`，`device_index = (pp_rank × pcp_size + pcp_rank) × tp_size + tp_rank`，以保证每个 rank 端口唯一。

## 模块结构

| 文件 | 说明 |
|------|------|
| `connector.py` | `HIXLConnector` 入口，实现 vLLM V1 Connector 接口；调度侧逻辑（请求匹配、块延迟释放、元数据构建、多节点握手映射） |
| `worker.py` | Worker 侧逻辑：KV 显存注册、TP/PP/PCP/DCP 传输分片与块 id 映射、GQA/Mamba/MLA 等场景的拉取策略 |
| `transfer_threads.py` | 后台传输线程：P 侧 ZMQ ROUTER 服务线程；D 侧请求队列、按对端分组的并发拉取与 KV 重排 |
| `hixl_wrapper.py` | 原生 `hixl` pybind 的本地封装：连接复用、异步传输轮询为阻塞读、软/硬超时与孤儿句柄回收、对端失效重连 |
| `engine_options.py` | `hixl_engine` 扩展配置解析：后端选择（`hixl_cs`/`comm`）、选项规范化（`LocalCommRes`/`GlobalResourceConfig`）、监听端口与超时 |
| `metadata.py` | 线上协议与每步数据结构：`HIXLAgentMetadata`、`ReqMeta`、`GroupPull`、`HIXLConnectorMetadata` 等 |
| `task_tracker.py` | P/D 两侧共用的请求完成与延迟释放跟踪（超时强释放） |
| `utils.py` | ZMQ 收发重试、连续块分组、请求哈希、PP 层切分解析等工具函数 |
| `constants.py` | 消息类型、后端名、默认超时等常量 |

## 环境要求与安装

- 昇腾环境：CANN toolkit（构建 ≥9.0.0，运行 ≥8.5），并已 source 环境变量。
- 软件依赖：vllm（V1 Connector API）、vllm-ascend、torch/torch_npu、numpy、msgspec、pyzmq。
- 构建并安装含本模块的 hixl wheel：

```bash
bash build.sh --experimental
# 安装 build_out/ 中产出的 hixl wheel 后，vllm_hixl_connector 作为顶层 Python 包可用
```

## 使用方法

### 注册方式

接入 vLLM 有两条途径：**不编译直接用源码**，或**编译打包成 wheel 后使用**。两种途径下 vLLM 侧的启动命令完全相同，区别只在 Python 环境里 `vllm_hixl_connector` 包的来源。

#### 方式一：不编译，直接在 vLLM 中使用

适用于环境中已装有含 native `hixl` 库的 wheel、只需快速试用或迭代 connector 纯 Python 代码的场景。通过 `PYTHONPATH` 让源码包整体遮蔽 `site-packages` 中的旧版，共三步：

1. 把源码包放入运行环境（拷贝或挂载均可），需要的只有这一个目录（`PYTHONPATH` 要指到它的父目录 `src/experimental/python`）：

   ```
   /path/to/hixl/src/experimental/python/vllm_hixl_connector/   # 9 个模块 + __init__.py
   ```

2. 让源码包优先于 wheel 中的旧版，并在同一 shell 启动 vLLM（服务进程需继承该环境变量）：

   ```bash
   export PYTHONPATH=/path/to/hixl/src/experimental/python:$PYTHONPATH
   ```

3. 启动 vLLM。以 P 侧为例（D 侧把 `kv_role` 换成 `kv_consumer` 即可）：

   ```bash
   vllm serve <model> --tensor-parallel-size 8 \
     --kv-transfer-config '{
       "kv_connector": "HIXLConnector",
       "kv_connector_module_path": "vllm_hixl_connector.connector",
       "kv_role": "kv_producer",
       "kv_port": 23000,
       "extra_config": {
         "prefill": {"tp_size": 8, "dp_size": 1},
         "decode":  {"tp_size": 4, "dp_size": 1}
       }
     }'
   ```

方式一注意事项：

- `PYTHONPATH` 是整包遮蔽：`sys.path` 中源码路径优先命中后，wheel 里的旧版模块整体失效，不会出现新旧混用。
- P/D 两侧必须设置相同的 `PYTHONPATH`。否则两侧加载的是两个不同的 `HIXLConnector` 实现，不仅类对象不一致，两侧的 ZMQ 消息与 `HIXLAgentMetadata` 线上协议也可能不兼容，故障会表现为握手或传输阶段的异常，而非明确的导入错误。

#### 方式二：编译后在 vLLM 中使用

正式部署方式，connector 随 hixl wheel 一起安装到 Python 环境：

```bash
bash build.sh --experimental
pip install <build_out 中的 hixl wheel>
```

安装后无需任何环境变量，直接启动。以 D 侧为例：

```bash
vllm serve <model> --tensor-parallel-size 4 \
  --kv-transfer-config '{
    "kv_connector": "HIXLConnector",
    "kv_connector_module_path": "vllm_hixl_connector",
    "kv_role": "kv_consumer",
    "kv_port": 23000,
    "extra_config": {
      "prefill": {"tp_size": 8, "dp_size": 1},
      "decode":  {"tp_size": 4, "dp_size": 1}
    }
  }'
```

两种途径共同的要点：

- P/D 两侧只有 `kv_role` 不同，其余配置（含 `extra_config`）完全相同；
- `extra_config` 里 P/D 两段并行规格都要写全：本模块在两侧都会读取 `prefill` 与 `decode` 两段配置，用于推导对端 rank、端口与块映射；
- P/D 同机混部时必须使用不同的 `kv_port`（见「KVTransferConfig」）。

#### 安装自检

启动 vLLM 之前分两步验证；方式一还需额外确认加载的是源码而非旧版：

```bash
# 第一步：验证包可导入（轻量，不触发 vllm/torch 导入，无 NPU 也能跑）
python -c "import vllm_hixl_connector; print(vllm_hixl_connector.__file__)"
# 方式二下应输出 wheel 安装路径下的 vllm_hixl_connector/__init__.py；
# 方式一下应指向 PYTHONPATH 源码目录，若显示 site-packages/... 说明环境变量未生效
# （或 vLLM 由其他用户/服务启动，未继承到）。

# 第二步：验证连接器类能完整加载（需要 vllm、vllm-ascend、torch_npu 已装好）
python -c "from vllm_hixl_connector import HIXLConnector; print(HIXLConnector)"
# 预期输出 <class 'vllm_hixl_connector.connector.HIXLConnector'>
```

第一步报 `ModuleNotFoundError` → wheel 未安装或构建未带 `--experimental`；第二步报依赖缺失 → 运行环境未装齐（见「环境要求与安装」）。

### vLLM 的加载方式

`HIXLConnector` 不是 vLLM 内置的连接器。启动 PD 分离实例时，vLLM 根据 `--kv-transfer-config` 里的两个字段在运行时动态导入它：

- `kv_connector`：连接器**类名**；
- `kv_connector_module_path`（可选）：类所在的 **Python 模块路径**。

解析顺序（`KVConnectorFactory.create_connector`）：

1. 配置了 `kv_connector_module_path` → `importlib.import_module(该路径)`，再 `getattr(模块, kv_connector)` 取类；
2. 未配置但 `kv_connector` 含点号 → 按最后一个点拆成「模块路径 + 类名」，回到第 1 步；
3. 都没有 → 只在 vLLM 内置连接器中查找，找不到 `HIXLConnector` 会直接报错。

因此必须通过 `kv_connector_module_path`（或等价的点号全路径写法，即 `"kv_connector": "vllm_hixl_connector.connector.HIXLConnector"`，依赖所用 vLLM 版本支持该解析）显式告诉 vLLM 类的位置——这正是上一节示例中这两个字段必须出现的原因。

本模块的包结构决定了上述加载开销很小：

```
vllm_hixl_connector/
├── __init__.py      # 只做懒加载，import 时不会拉起 vllm/torch 等重依赖
├── connector.py     # HIXLConnector 类定义
└── ...              # 其余 8 个模块
```

`__init__.py` 通过 `__getattr__` 懒加载导出 `HIXLConnector`：`import vllm_hixl_connector` 本身非常轻量，只有真正访问 `HIXLConnector` 属性时才会导入 `connector.py` 及其全部依赖（vllm、torch_npu 等）。方式二示例（模块路径指向包 `vllm_hixl_connector`）正是利用了这一点；方式一示例与点号全路径写法则直接导入 `connector.py`。

### KVTransferConfig

`--kv-transfer-config` 接收一个 JSON 对象（对应 vLLM 的 `KVTransferConfig` 类），顶层字段如下：

| 字段 | 必填 | 说明 |
|------|------|------|
| `kv_role` | 是 | 实例角色：Prefill 侧填 `kv_producer`（算完 KV 等待拉取），Decode 侧填 `kv_consumer`（主动拉取远端 KV） |
| `kv_connector` | 是 | 连接器类名，固定填 `HIXLConnector` |
| `kv_connector_module_path` | 推荐 | 模块路径，填 `vllm_hixl_connector`（见「注册方式」） |
| `engine_id` | 否（推荐显式填写） | 本实例的唯一标识，P/D 两侧必须不同（D 侧拉取元数据时会校验）。未填写时较新版本 vLLM 会在 `KVTransferConfig.__post_init__` 中自动生成随机 uuid，两侧天然不同；显式填写可让日志与排障时更容易识别实例，旧版本 vLLM 无自动生成逻辑时必须显式填写 |
| `kv_port` | 是 | 端口基值：握手端口从 `kv_port` 起、引擎监听端口从 `kv_port + 10000` 起，各按 rank 递增占用一段连续端口（精确公式见「工作原理」的端口规划）；**同机混部 P/D 时两侧必须用不同的 `kv_port`** |
| `extra_config` | 是 | 本模块的扩展配置，见下一节 |

### 配置项说明

`extra_config` 字段说明：

| 字段 | 子项 | 说明 | 默认值 |
|------|------|------|--------|
| `prefill` | `tp_size` / `dp_size` | Prefill 侧并行规格，两侧必填 | 无（必填） |
| | `pp_size` | Prefill 侧流水线并行数 | 1 |
| | `pp_layer_partition` | 各 PP rank 层数，逗号分隔（如 `"16,16"`），缺省用 vLLM 默认切分 | 无 |
| `decode` | `tp_size` / `dp_size` | Decode 侧并行规格，两侧必填 | 无（必填） |
| | `pp_size` | Decode 侧流水线并行数，当前必须为 1 | 1 |
| `hixl_engine` | `backend` | HIXL 后端：`hixl_cs` 或 `comm` | `hixl_cs` |
| | `options` | 透传给引擎的选项字典（如 `GlobalResourceConfig`）；`hixl_cs` 在未显式配置协议描述时会自动补齐 `LocalCommRes` 1.3，`comm` 会剥离 CS 选择项 | `{}` |
| | `listen_port_base` | 引擎监听端口基值 | `kv_port + 10000` |
| | `link_timeout_ms` | 建链超时 | 5000 |
| | `transfer_timeout_ms` | 单次传输软超时（超过后再宽限 30 秒排空，仍未完成才放弃并告警） | 60000 |

传输完成后，连接器会输出每个请求的耗时、字节数与带宽（INFO 日志），可用于定位性能问题。

### 常见问题排查

| 现象 | 原因与处理 |
|------|------------|
| `ModuleNotFoundError: No module named 'vllm_hixl_connector'` | wheel 未安装，或构建时未带 `--experimental`；用「安装自检」第一步确认 |
| `AttributeError: module 'vllm_hixl_connector' has no attribute '...'` | `kv_connector` 类名拼写错误，`__getattr__` 只识别 `HIXLConnector` |
| 自检第二步报 vllm / vllm_ascend / torch_npu 导入失败 | 懒加载设计使这些依赖在类被真正访问时才导入；按「环境要求与安装」补齐运行依赖 |
| 启动即报 `prefill_tp_size ... must be greater than or equal to the decode_tp_size` | `extra_config.prefill.tp_size` 小于 `decode.tp_size`，调整并行规格 |
| 启动即报 `pp_size(x) and pcp_size(y) cannot both be greater than 1` 等并行规格错误 | 并行规格组合违反约束，逐条对照「约束与注意事项」 |
| 同机部署时 P/D 端口冲突、进程起不来 | 握手/监听端口均由 `kv_port` 推导（监听端口偏移 +10000），同机两侧换用不同的 `kv_port`，并保证 `kv_port` 到 `kv_port + 10000 + rank 总数` 范围内端口空闲 |
| 一侧正常启动、另一侧长时间无进展或握手超时 | 排查两侧网络连通性与防火墙（握手端口与引擎监听端口都要放通）；确认 P/D 两侧加载的是同一份实现（见「注册方式」方式一的注意事项） |

## 约束与注意事项

- `prefill.tp_size` 必须大于等于 `decode.tp_size`；混合/Mamba 模型要求前者可被后者整除。
- Decode 侧不支持 PP（`decode.pp_size` 必须为 1）；PP 与 PCP 不能同时大于 1。
- P/D 两侧 `block_size` 必须可整除；D 侧开启上下文并行（PCP/DCP 任一大于 1）时不支持 P 侧块大小大于 D 侧。
- 上下文并行场景要求远端 CP 规模是本地 CP 规模的整数倍。
- 本模块为实验特性，接口与行为可能随版本调整；如遇问题可先通过 DEBUG 日志观察握手与分片映射过程。
- 单次拉取超过 `hixl_engine.transfer_timeout_ms`（默认 60 秒）并再宽限 30 秒仍处于 WAITING 时会被放弃（同时输出正确性风险告警）：被放弃的 READ 晚到仍可能写入已回收的 KV 块。请按网络与负载状况调大该超时，出现此类告警时优先排查网络。
- P 侧收到 Decode 端"接收完成"信号后不会立即释放 KV 块，而是延迟释放；最长等待时间复用 vLLM 环境变量 `VLLM_MOONCAKE_ABORT_REQUEST_TIMEOUT`（单位秒，vLLM 默认 600，本连接器未引入独立变量）。超时后强制释放对应请求的 KV 块并输出 `Force freed expired request` ERROR 日志，以避免内存泄漏。
- 控制面 ZMQ 无认证与加密：握手端口与引擎监听端口仅可在受信网络（内网或专用集群网络）内开放，不可暴露到公网。
