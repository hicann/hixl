# FabricMem传输模式需求

##### 介绍

**需求的背景**：
1. 随着大语言模型(LLM)推理规模的扩大，KV Cache规模越来越大，Mooncake store等分布式DRAM缓存池场景对NPU到DRAM(D2RH)传输性能提出了更高要求。
2. A3服务器提供FabricMemory技术，支持超节点内DRAM内存统一编址，能够利用HCCS链路进行D2RH/RH2D传输。
3. 其他模式的限制与劣势：
   * 底层调用HCCL接口的HCCS传输模式不支持D2RH传输。
   * 中转模式在A3上会占用HBM带宽，对模型推理影响较大。

##### 输入&输出

**使用时的输入介绍**：
1. **配置选项**：通过`OPTION_ENABLE_USE_FABRIC_MEM`选项启用FabricMem模式，值为"1"表示启用。
2. **内存描述**：注册内存时使用`MemDesc`结构体，包含内存地址和长度。
3. **传输操作**：传输时使用`TransferOp`枚举(READ/WRITE)描述方向，使用`TransferOpDesc`描述传输地址。

**使用example**：
```cpp
// 初始化HIXL引擎，启用FabricMem模式
Hixl engine1;
std::map<AscendString, AscendString> options1;
options1[OPTION_ENABLE_USE_FABRIC_MEM] = "1";
engine1.Initialize("127.0.0.1:26000", options1);

// 注册内存
std::vector<uint8_t> buffer(size, 0xAA);
hixl::MemDesc mem_desc{};
mem_desc.addr = reinterpret_cast<uintptr_t>(buffer.data());
mem_desc.len = size;
MemHandle handle = nullptr;
engine1.RegisterMem(mem_desc, MEM_HOST, handle);

// 建立连接
engine1.Connect("127.0.0.1:26001");

// 执行传输
TransferOpDesc desc{src_addr, dst_addr, size};
engine1.TransferSync("127.0.0.1:26001", WRITE, {desc});
```

**使用时传出的输出介绍**：
1. **内存句柄**：注册内存时返回`MemHandle`，用于标识已注册的内存区域。
2. **传输请求**：异步传输时返回`TransferReq`，用于异步传输状态查询。
3. **传输状态**：查询异步任务状态时返回`TransferStatus`枚举。

##### 处理

FabricMem由独立的`FabricMemEngine`承载，不再侵入`HixlEngine`、`AdxlInnerEngine`、`ChannelMsgHandler`或`Channel`。`EngineFactory`在发现`OPTION_ENABLE_USE_FABRIC_MEM=1`时创建`FabricMemEngine`，后续注册、建链、传输、统计和资源清理由`src/hixl/fabric_mem`与`src/hixl/engine/fabric_mem_engine.cc`负责。

**类图**：
```mermaid
classDiagram
    class EngineFactory {
        +CreateEngine(string, map~AscendString, AscendString~) unique_ptr~Engine~
    }

    class FabricMemEngine {
        +Initialize(HixlOptions) Status
        +RegisterMem(MemDesc, MemType, MemHandle&) Status
        +DeregisterMem(MemHandle) Status
        +Connect(AscendString, int32_t) Status
        +Disconnect(AscendString, int32_t) Status
        +TransferSync(AscendString, TransferOp, vector~TransferOpDesc~, int32_t) Status
        +TransferAsync(AscendString, TransferOp, vector~TransferOpDesc~, TransferArgs, TransferReq&) Status
        +GetTransferStatus(TransferReq, TransferStatus&) Status
        -fabric_mem_config_ : FabricMemConfig
        -fabric_mem_statistic_ : FabricMemStatistic
        -local_memory_ : FabricMemLocalMemory
        -fabric_mem_transfer_service_ : shared_ptr~FabricMemTransferService~
        -fabric_mem_control_server_ : unique_ptr~FabricMemControlServer~
    }

    class FabricMemTransferService {
        +Initialize(FabricMemTransferServiceInitParam) Status
        +Finalize() void
        +Connect(AscendString, int32_t) Status
        +Disconnect(AscendString, int32_t) Status
        +TransferSync(string, TransferOp, vector~TransferOpDesc~, int32_t) Status
        +TransferAsync(string, TransferOp, vector~TransferOpDesc~, TransferReq&) Status
        +GetTransferStatus(TransferReq, TransferStatus&, AsyncTransferPollInfo*) Status
        +CleanupAsyncTransfer(TransferReq) void
        +StartKeepaliveMonitor() Status
        -channel_manager_ : FabricMemChannelManager
        -slot_pool_ : FabricMemSlotPool
        -local_memory_ : FabricMemLocalMemory*
    }

    class FabricMemChannelManager {
        +Initialize(FabricMemChannelManagerInitParam) Status
        +Connect(AscendString, int32_t) Status
        +Disconnect(AscendString, int32_t) Status
        +GetChannel(string, shared_ptr~FabricMemChannel~&) Status
        +AddReqRoute(uint64_t, shared_ptr~FabricMemChannel~) void
        +FindChannelByReq(uint64_t, shared_ptr~FabricMemChannel~&) Status
        -channels_ : map~string, shared_ptr~FabricMemChannel~~
        -req_2_channel_ : map~uint64_t, shared_ptr~FabricMemChannel~~
        -keepalive_monitor_ : thread
    }

    class FabricMemChannel {
        +remote_memory : unique_ptr~FabricMemRemoteMemory~
        +async_records : map~uint64_t, AsyncRecord~
        +active_sync_slots : vector~AsyncSlot~
        +keepalive_fd : int32_t
    }

    class FabricMemSlotPool {
        +Initialize(int32_t, size_t, size_t) Status
        +AcquireAsync(AsyncSlot&) Status
        +AcquireWithTimeout(AsyncSlot&, uint64_t) Status
        +Release(AsyncSlot&, bool) void
    }

    class FabricMemLocalMemory {
        +RegisterMem(MemDesc, MemType, MemHandle&) Status
        +DeregisterMem(MemHandle) Status
        +GetShareHandles() vector~ShareHandleInfo~
        +TranslateLocalHostOpAddrs(vector~TransferOpDesc~&) Status
    }

    class FabricMemControlServer {
        +Start(string, ShareHandleProvider) Status
        +Stop() void
    }

    class FabricMemRemoteMemory {
        +Import(vector~ShareHandleInfo~, int32_t) Status
        +Finalize() void
        +GetNewVaToOldVa() unordered_map~uintptr_t, VaInfo~
    }

    class VirtualMemoryManager {
        +Initialize() Status
        +ReserveMemory(size_t, uintptr_t&) Status
        +ReleaseMemory(uintptr_t) Status
    }

    class FabricMemStatistic {
        +RegisterChannel(string) void
        +UpdateCosts(string, uint64_t, uint64_t, uint64_t, uint64_t) void
    }

    EngineFactory --> FabricMemEngine : EnableUseFabricMem=1时创建
    FabricMemEngine --> FabricMemTransferService : 持有
    FabricMemEngine --> FabricMemControlServer : 持有
    FabricMemEngine --> FabricMemLocalMemory : 持有
    FabricMemEngine --> FabricMemStatistic : 持有
    FabricMemTransferService --> FabricMemChannelManager : 持有
    FabricMemTransferService --> FabricMemSlotPool : 持有
    FabricMemTransferService ..> FabricMemLocalMemory : 引用(本地地址转换)
    FabricMemChannelManager --> FabricMemChannel : 每个remote持有
    FabricMemChannelManager ..> FabricMemSlotPool : disconnect时abort/release
    FabricMemChannel --> FabricMemRemoteMemory : 持有
    FabricMemRemoteMemory --> VirtualMemoryManager : 使用
    FabricMemLocalMemory --> VirtualMemoryManager : 使用
    FabricMemTransferService --> FabricMemStatistic : 更新统计
```

**时序图**（FabricMem模式下的数据传输）：
```mermaid
sequenceDiagram
   participant User
   participant FabricMemEngine
   participant LocalMemory as FabricMemLocalMemory
   participant Service as FabricMemTransferService
   participant ChannelMgr as FabricMemChannelManager
   participant SlotPool as FabricMemSlotPool
   participant Control as FabricMemControl
   participant RemoteMemory as FabricMemRemoteMemory
   participant Runtime as AscendRuntime

   User->>FabricMemEngine: Initialize(options)
   FabricMemEngine->>FabricMemEngine: Parse FabricMemConfig + VirtualMemoryManager.Initialize()
   FabricMemEngine->>Control: Start(local_engine, share_handle_provider)
   FabricMemEngine->>Service: Initialize(FabricMemTransferServiceInitParam)
   Service->>SlotPool: Initialize(device, max_async_slot, task_stream)
   Service->>ChannelMgr: Initialize(FabricMemChannelManagerInitParam)

   User->>FabricMemEngine: RegisterMem(mem_desc, type, handle)
   FabricMemEngine->>LocalMemory: RegisterMem(mem_desc, type, handle)
   LocalMemory->>Runtime: aclrtMemRetainAllocationHandle()/aclrtMemExportToShareableHandleV2()
   LocalMemory->>LocalMemory: 存储共享句柄和虚拟地址映射

   User->>FabricMemEngine: Connect(remote_engine)
   FabricMemEngine->>Service: Connect(remote_engine)
   Service->>ChannelMgr: Connect(remote_engine)
   ChannelMgr->>Control: Fetch(remote_engine) 取 share_handles 与 keepalive_fd
   Control-->>ChannelMgr: remote_share_handles
   ChannelMgr->>RemoteMemory: Import(remote_share_handles, device_id)
   RemoteMemory->>Runtime: aclrtMemImportFromShareableHandleV2()/aclrtMapMem()
   ChannelMgr->>ChannelMgr: 登记 channel 至 channels_（含 keepalive_fd）

   User->>FabricMemEngine: TransferSync(remote_engine, WRITE, op_descs)
   FabricMemEngine->>Service: TransferSync(remote_engine, WRITE, op_descs, timeout)
   Service->>ChannelMgr: GetChannel + BuildTransferContext(remote_engine)
   Service->>SlotPool: AcquireWithTimeout(slot)
   Service->>Service: submit_gate 共享锁下将用户地址转换为映射地址并下发拷贝
   Service->>Runtime: aclrtMemcpyAsync(src_mapped_addr, dst_mapped_addr, size)
   Service->>Runtime: aclrtSynchronizeStream()
   Service->>SlotPool: Release(slot)
```

**整个特性的处理过程介绍**：
1. **初始化阶段**：
   - 用户通过`OPTION_ENABLE_USE_FABRIC_MEM`选项启用FabricMem模式。
   - `EngineFactory`根据该选项创建`FabricMemEngine`，不进入`AdxlInnerEngine`或`HixlEngine`。
   - `FabricMemEngine`解析`FabricMemConfig`，初始化`VirtualMemoryManager`、`FabricMemTransferService`、`FabricMemControlServer`和`FabricMemStatistic`。

2. **内存注册阶段**：
   - 用户调用`RegisterMem`注册内存。
   - `FabricMemTransferService`通过`aclrtMemRetainAllocationHandle`获取物理内存句柄。
   - 使用`aclrtMemExportToShareableHandleV2`导出为Fabric可共享句柄。
   - 将共享句柄信息存储在`share_handles_`中。

   **H2H传输模式的特殊性**：
   - 对于HOST内存，FabricMem传输需要额外的转换处理。
   - HOST内存需要先通过`aclrtMemRetainAllocationHandle`获取物理内存句柄。
   - 然后使用`aclrtMemExportToShareableHandleV2`导出为共享句柄。
   - 然后进行VMM映射，将物理内存映射到虚拟地址空间。

3. **连接建立阶段**：
   - 本端 `FabricMemChannelManager` 通过 `FabricMemControlClient` 向对端 `FabricMemControlServer` 拉取 `share_handles_`。
   - `FabricMemChannelManager` 通过 `FabricMemRemoteMemory` 导入远程内存的共享句柄。
   - 使用 `aclrtMemImportFromShareableHandleV2` 导入共享句柄，映射到虚拟地址空间。
   - 建立远端用户地址到本地映射地址的映射关系，并登记到 `channels_` 连接表。

4. **数据传输阶段**：
   - `FabricMemEngine` 通过 `FabricMemChannelManager` 获取 channel 并构建 `FabricMemTransferContext`。
   - `FabricMemTransferService` 进行用户地址和映射地址转换。
   - 从stream pool获取任务需要的流资源。
   - 使用`aclrtMemcpyAsync`执行内存拷贝操作。
   - 同步传输阻塞等待；异步传输在每个copy stream上追加host flag D2H，轮询host flag判定完成（不再使用EventRecord/query event）。
   - 传输耗时、真实拷贝耗时、总字节数和op desc数量记录在`FabricMemStatistic`中。

5. **资源清理阶段**：
   - 用户调用`DeregisterMem`注销内存。
   - 释放物理内存句柄和共享句柄。
   - 连接断开或 Finalize 时清理远端导入映射、stream、异步资源（含 host flag 池）和统计通道；`Disconnect` 立即 abort 该 channel 在途的 sync/async stream（不等待传输完成），清空其 async record 与请求路由后销毁 async slot。

##### 并发与锁设计

**设计前提**：不考虑 `Finalize` 与对外 API 并发；对外 API 入口仅做 `is_initialized_` 检查。

**组件职责**：
- `FabricMemEngine`：薄门面，编排 TransferService / LocalMemory / ControlServer；持有 `FabricMemLocalMemory`，不持有连接表或 async record。
- `FabricMemTransferService`：对外传输门面，持有 `FabricMemChannelManager` 与 `FabricMemSlotPool`；负责拷贝编排、async record 的查询/完成（含 prof 元数据）。
- `FabricMemChannelManager`：连接生命周期（Fetch/Install/Disconnect）、请求路由 `req_2_channel_`、出站 keepalive 线程、disconnect 时立即 abort。
- `FabricMemSlotPool`：async slot（stream + host flag）池。
- `FabricMemLocalMemory`：本地内存注册、Export、share handle 管理。
- `FabricMemRemoteMemory`：单 channel 导入的远端内存映射（由 channel 持有）。

**锁层级**：

| 组件 | 锁 | 保护对象 |
|------|-----|----------|
| Engine | `mutex_` | Initialize/Finalize 编排 |
| LocalMemory | `share_handle_mutex_` | `share_handles_`（含 overlap 校验） |
| ChannelManager | `connect_mutex_` | Fetch + Install 串行化（独立，网络 I/O 阶段不持 `channels_mutex_`） |
| ChannelManager | `channels_mutex_` | `channels_` 连接表与 `initialized_` |
| ChannelManager | `req_route_mutex_` | `req_2_channel_` 请求路由表 |
| Channel | `submit_gate`（`shared_mutex`） | 传输提交与断链互斥：提交持**共享锁**并行下发拷贝，断链 `AbortAndClearChannelRecords` 持**独占锁** drain 在途提交后再 abort/unmap（abort-before-unmap） |
| Channel | `records_mutex` | 单 channel 的 `disconnecting` / `async_records` / `active_sync_slots` 简短簿记（不含拷贝下发） |
| SlotPool | `pool_mutex_` | slot 池（叶锁） |
| RemoteMemory | `mutex_` | 单 channel 导入映射（叶锁） |
| ControlServer | `State::mutex` | `sessions` / `client_id_to_fd` / 监听与 epoll fd；发送一律在锁外 |

**固定加锁顺序**：
1. `channels_mutex_` 独立持有，禁止在其内执行网络 I/O。
2. 单 channel 内：`submit_gate` → `records_mutex`：传输提交持 `submit_gate` 共享锁，仅在登记 record/slot 时短暂持 `records_mutex`；`AbortAndClearChannelRecords` 先持 `submit_gate` 独占锁再持 `records_mutex`。
3. `submit_gate` → `req_route_mutex_`：`IssueAsyncCopyAndRegister` 在释放 `records_mutex` **之后**、仍持 `submit_gate` 共享锁时调用 `AddReqRoute`；`RemoveReqRoute` 在释放 `submit_gate` 之后调用。禁止逆序。
4. `connect_mutex_` 与 `channels_mutex_` 分离。
5. `pool_mutex_` / `share_handle_mutex_` / RemoteMemory `mutex_` 为叶锁，不与上述锁嵌套。

**跨组件规则**：Engine API 不在多个组件间嵌套持锁；典型传输路径为 TransferService 经 ChannelManager 取 channel → 持 `submit_gate` 共享锁下发拷贝，在 `records_mutex` 内登记 record/slot，异步路径再持 `submit_gate` 共享锁下调用 `AddReqRoute` → `GetTransferStatus` 在 `records_mutex` 内查询 stream 状态并取出 record，仅在锁外做阻塞 synchronize。

**典型并发场景**：

| 场景 | 行为 |
|------|------|
| 多线程 TransferSync 同一 remote | 拷贝在 `submit_gate` 共享锁下并行下发，`records_mutex` 仅用于登记 `active_sync_slots` 供 disconnect abort |
| TransferAsync + GetTransferStatus | 拷贝在 `submit_gate` 共享锁下并行下发；async record 由 channel `records_mutex` 保护；`GetTransferStatus` 在锁内查询 stream 状态并取出 record（避免与 disconnect 销毁 stream 竞争），仅需 `req` |
| Disconnect | 立即 abort、不等待传输完成：先设 `disconnecting=true`，持 `submit_gate` 独占锁 drain 在途提交，在 `records_mutex` 内取出并清空 async record、abort sync slot（仅 abort）；锁外销毁 async slot 并清理请求路由 |
| keepalive 线程 auto-disconnect | keepalive 线程内聚于 ChannelManager；不持 Engine 锁，独立 Disconnect |
| RemoveChannel | 先 abort/清理该 channel 的 async record 与请求路由，再清理导入映射、stream、统计通道 |

**Heartbeat**：`FabricMemControlClient::SendHeartBeat` / 入站 ADXL 解析均内聚于 `fabric_mem_control`；自连 session 与远端 session 统一启用 heartbeat 超时检查。`ControlServer` 的 worker 线程仅在 `State::mutex` 内做 session 读取与 map 变更，所有发送在锁外完成；`pending_connections` 仅由 worker 线程访问，`Stop()` 在 join worker 之后再清理 map。

1. **内存申请**：
   ```cpp
   void *fabric_ptr = nullptr;
   Hixl::MallocMem(MEM_HOST, mem_size, &fabric_ptr);
   ```
   - FabricMem host内存申请由`FabricMemTransferService::MallocMem`统一封装。
   - 底层会完成虚拟地址预留、物理内存申请和映射。
   - 传输完成后通过`Hixl::FreeMem`释放。

2. **引擎初始化和内存注册**：
   - 启用FabricMem模式：`options[OPTION_ENABLE_USE_FABRIC_MEM] = "1"`。
   - 初始化Hixl。
   - 注册内存：`engine.RegisterMem(desc, MEM_HOST, handle)`或`engine.RegisterMem(desc, MEM_DEVICE, handle)`。

3. **连接建立和数据交换**：
   - 调用`Connect`方法建立连接。
   - `FabricMemEngine`会拉取并导入远端共享句柄。

4. **数据传输和验证**：
   - 执行传输：`engine.TransferSync(remote_engine, WRITE, {desc})`。
   - 验证传输结果：读取远程写入的数据并验证。

##### 关键检查点

**检查点列表**：
1. **Engine路由检查**：`OPTION_ENABLE_USE_FABRIC_MEM=1`时必须由`EngineFactory`创建`FabricMemEngine`。
2. **配置合法性检查**：`FabricMemConfig`解析`EnableUseFabricMem`、`GlobalResourceConfig`、流数量、虚拟地址容量和起始地址。
3. **内存类型检查**：在FabricMem模式下，HOST内存注册需要额外的本地导入和映射处理。
4. **传输参数检查**：验证传输描述中的地址范围是否在本端已注册或对端已导入的内存范围内。
5. **流资源管理检查**：确保流池中的流资源正确分配和释放，避免资源泄漏。
6. **异步请求状态检查**：异步传输时正确跟踪请求状态，确保状态查询的准确性。
7. **内存映射清理检查**：连接断开时正确清理`FabricMemRemoteMemory`中的导入映射关系。
8. **并发安全检查**：多线程环境下对共享数据结构的访问安全。
9. **对端异常下线**：对端异常下线时，需要清理相关资源，避免资源泄漏。

**性能关键点**：
1. **流池管理**：预创建和管理设备流，避免频繁创建销毁的开销。
2. **多流并发**：支持一次使用多条流并发处理。
3. **异步操作**：支持异步传输，允许重叠计算和通信。

**兼容性考虑**：
1. **向后兼容**：默认不启用FabricMem模式，保持与传统ADXL/HCCL路径的兼容性。
2. **统计归属**：FabricMem传输统计由`FabricMemStatistic`维护，ADXL侧`StatisticManager`只维护ADXL/HCCL路径的建链与传输统计。
