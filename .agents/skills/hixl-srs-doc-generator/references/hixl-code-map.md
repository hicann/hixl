# HIXL/ADXL 代码地图

用于生成 SRS 前快速定位事实，不替代实际阅读代码。

## 核心路径

- `src/hixl/engine/`：HIXL Engine 对外引擎、client/server、notify client、factory、endpoint 生成。
- `src/hixl/cs/`：控制面服务、endpoint、channel、连接/内存消息处理、transfer pool、完成通知抽象（`CompletionWait` / `NotifyWait` / `ValueWait`）。
- `src/hixl/common/`：日志、校验、segment、thread pool、ctrl msg 等通用能力。
- `src/hixl/profiling/`：profiling 注册与上报。
- `src/hixl/proxy/`：hcomm/hccp proxy 适配层。
- `src/ops/hixl_kernel/`：Device kernel 的传输上下文和异常回调实现。
- `src/llm_datadist/adxl/`：ADXL 内部引擎、channel 管理、控制消息、buffer/fabric mem 传输、segment table、virtual memory、stream pool、统计。

## 公开接口与文档边界

- HIXL：`include/hixl/hixl.h`、`include/hixl/hixl_types.h`、`docs/cpp/HIXL接口.md`、`docs/cpp/HIXL数据结构.md`、`docs/cpp/HIXL错误码.md`。
- HIXL CS：`include/cs/hixl_cs.h`、`docs/cpp/HIXL_CS接口.md`。
- ADXL：`include/adxl/adxl_engine.h`、`include/adxl/adxl_types.h`、`docs/cpp/待废弃_ADXL接口.md`、`docs/cpp/待废弃_ADXL数据结构.md`、`docs/cpp/待废弃_ADXL错误码.md`。
- LLM-DataDist：`include/llm_datadist/`、`docs/cpp/LLM-DataDist接口.md`、`docs/cpp/LLM-DataDist数据结构.md`、`docs/cpp/LLM-DataDist错误码.md`。
- 设计文档：`docs/design/`，已有 FabricMem 和 HIXL 传输后端相关设计可作为风格参考。
- 其他文档：`docs/cpp/不支持的接口.md`、`docs/cpp/README.md`。

## ADXL 常见设计切入点

- `adxl_inner_engine.*`：内部引擎生命周期、初始化、资源申请、传输入口。
- `channel_manager.*`、`channel.*`：通道创建、复用、关闭、并发访问。
- `control_msg_handler.*`、`channel_msg_handler.*`：控制消息、对端交互、协议字段。
- `fabric_mem_transfer_service.*`：FabricMem 路径、内存映射、segment 使用。
- `buffer_transfer_service.*`：Buffer 中转路径。
- `segment_table.*`：远端/本端 segment 管理和 key 查询。
- `virtual_memory_manager.*`：虚拟内存申请、映射、释放。
- `stream_pool.*`：stream 生命周期和复用。
- `statistic_manager.*`：统计、可观测性、性能数据。
- `adxl_utils.*`：工具函数、配置选项常量、错误码转换。
- `adxl_checker.h`：ADXL 校验宏。
- `acl_compat.h`：ACL 兼容层，前向兼容虚拟内存预留接口。

## 测试映射

- HIXL Engine：`tests/cpp/hixl/engine/`。
- HIXL CS：`tests/cpp/hixl/cs/`。
- ADXL/LLM-DataDist：`tests/cpp/llm_datadist/`，重点参考 `adxl_engine_api_unittest.cc`、`fabric_mem_transfer_service_unittest.cc`、`segment_table_unittest.cc`、`virtual_memory_manager_unittest.cc`、`control_msg_handler_unittest.cc`。
- Python API 变化：`tests/python/test_*.py`。

## CS 完成通知与异常路径

- `src/hixl/common/soc_type.{h,cc}` 是统一机型判定入口：A2 为 `kGen1`、A3 为 `kGen2`；Ascend 950 系列和未知型号均归入 `kDefault`。
- `TransferPool::Initialize`：A2/A3 在所有 HCOMM 版本下保留 device 异常回调和原有 NotifyWait；仅 `kDefault` 且 HCOMM ≥ 9.3.0 时尝试启用 host 异常回调，注册失败则保留 device 路径。
- `HcommDeviceChannel::InitRdmaNotifyResources` 根据 Host 回调是否启用为非 HCCS、非 UB_MEM 的 Device 通道选路。启用时走 `ValueWait`，否则走 `NotifyWait`。
  `err_flag` 映射失败时仍按 Host 回调状态选路。HCCS 和 UB_MEM 沿用各自的完成通知流程。
- Host 回调启用后，完成 flag block 的申请或初始清零失败会使 `TransferPool::Initialize` 直接返回错误。
  选定 `ValueWait` 后，内存注册失败时 `RegisterFlagBlock` 直接返回 `RegisterMem` 的错误码。
- `TransferPoolLegacySocTest.Hcomm930UsesDeviceExceptionRegistration` 覆盖 A2/A3 的 Device 回调注册。
  `HixlCSClientLegacyDeviceFixture.NonHccsReadsIntoNotifyAndWaitsForNotify` 验证旧路径下发 `NotifyWait`。
  `HixlCSClientErrFlagMappingFailureFixture.KeepsValueWaitWhenErrorFlagMappingFails` 验证 `err_flag` 映射失败时仍走 `ValueWait`。

## 推荐检索命令

```bash
rg -n "<需求关键词|类名|接口名>" src/hixl src/llm_datadist/adxl include docs tests/cpp
rg -n "class|struct|enum|Status|Error|Init|Finalize|Transfer|Malloc|Free" src/hixl src/llm_datadist/adxl include
```

## 设计检查点

- 是否改变公开 API、ABI、错误码、配置项或文档承诺。
- 是否影响多进程/多节点建链、通道复用、内存注册/释放、stream 生命周期。
- 是否区分 FabricMem、HCCL 直传、Buffer 中转等路径。
- 是否明确线程安全、重复调用、失败回滚、资源泄漏风险。
- 是否需要补充日志、统计、profiling 或问题定位信息。
