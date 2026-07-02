<!--
Copyright (c) 2026 Huawei Technologies Co., Ltd.
This program is free software, you can redistribute it and/or modify it under the terms and conditions of
CANN Open Software License Agreement Version 2.0 (the "License").
Please refer to the License for details. You may not use this file except in compliance with the License.
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
See LICENSE in the root of the software repository for the full text of the License.
-->

# 建链失败类问题

<!-- 离线兜底快照，来源: https://gitcode.com/cann/hixl/wiki/HIXL常见问题定位手册.md -->
<!-- 同步日期: 2026-07-02。运行时优先 WebFetch Wiki，本地仅在无网络时使用。 -->
<!-- 注意: 中转(Buffer)模式已下线；FabricMem 为独立 FabricMemEngine，不再走 AdxlInnerEngine 内部分支。 -->

## 快速索引

先看首条致命错误，再按下面的关键词跳读对应章节，不要整份从头到尾顺读。

| 常见关键词 / 线索 | 优先查看章节 | 备注 |
|---|---|---|
| `wait socket establish timeout` | `1.建链超时` | 先确认是否存在 `HcclCommPrepare`、`LINK_ERROR_INFO` |
| `HcclCommPrepare` | `1.建链超时` | 多机场景优先核对双端下发时间 |
| `LINK_ERROR_INFO` | `1.建链超时 -> 场景二/三` | 重点看链路一致性与 TLS 配置 |
| `super_device_id`、`invalid host device id 65535` | `1.建链超时 -> 场景四` | HCCS + ranktable 场景常见 |
| `device_ip is not set correctly` | `2.建链报错 -> 报错现象二` | 常与 `hccn.conf` 或 CANN 版本相关 |
| `IP is used repeatedly`、`device_ip.*already exist`、`Ranktable_Check_Failed`、`EI0014` | `2.建链报错 -> 报错现象三` | 同 NPU 多进程；各进程配不同 `comm_resource_config.listen_port` |
| `comm_resource_config.listen_port`、`ASCEND_GLOBAL_RESOURCE_CONFIG` | `2.建链报错 -> 报错现象三` | Mooncake 经 env 透传；默认 16666，多进程须显式区分 |
| `CheckMemCpyAttr` | `2.数据传输时内存类型校验失败` | 内存类型不匹配或未注册内存 |
| `Can't find remoteBuffer by key` | `3.报错：Can't find remoteBuffer by key` | 常见于对端内存未注册或注册时机不对 |
| `out of memory`、`DevMemAllocHugePageManaged` | `2.建链报错 -> 场景三` | **用户 HBM** 不足（建链占 HBM ~7MB/链路） |
| `ibv_cmd_create_cq failed, ret 12`、`create cq failed ret -259`、`qp create failed` | `2.建链报错 -> 场景四` | **device 系统内存**不足（非 HBM）；ROCE 注册 HOST 池化内存消耗页表/CQ 资源，量级约几百 MB |
| `stream sync timeout`、`RtStreamSynchronizeWithTimeout` | `1.数据传输超时` | 结合 RDMA 重传参数一起分析 |
| `roce`、`ping`、`ib_send_bw` | `环境ROCE不通` | 先确认设备间联通性 |
| `PFC`、`DSCP`、`priority 4` | `网卡和交换机PFC配置不匹配问题` | 环境类问题，和业务日志分开看 |
| `HCCL_NPU_SOCKET_PORT_RANGE`、`isAutoPort` | `1.建链超时 -> 场景五` | 容器化场景随机端口 |
| `halMemImportFromShareableHandleV2 failed` | `FabricMem模式报错` | 可能跨超节点 |
| `suspect remote error` | `4.FabricMem模式报错` | 灵渠版本需 > 1.5.0 |
| `cpu stuck`、`fabric_memory.max_capacity` | `5.CANN9.0 FabricMem cpu stuck` | CANN 9.0 需配置资源上限 |

## 1.建链超时

### 报错现象：

用户调用建链接口时，底层会调用HCCL接口`HcclCommPrepare`执行建链流程。当plog中存在`HcclCommPrepare`接口的调用栈且存在`wait socket establish timeout`超时日志时，则属于建超时类问题，通常plog中有如下类似报错：

```bash
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.031.224 [hccl_socket_manager.cc:816] [673943][Wait][LinkEstablish]wait socket establish timeout, role[0] rank[0] timeout[10 s]
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.031.580 [hccl_socket_manager.cc:883] [673943][Wait][LinksEstablishCompleted] is failed. ret[9].
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.031.583 [hccl_socket_manager.cc:510] [673943][Create][Sockets]Wait links establish completed failed, local role is client. ret[9]
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.031.743 [hccl_socket_manager.cc:642] [673943]   _________________________LINK_ERROR_INFO___________________________
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.031.746 [hccl_socket_manager.cc:643] [673943]   |  comm error, device[0]
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.031.748 [hccl_socket_manager.cc:645] [673943]   |  dest_ip(user_rank)  |   dest_port   |  src_ip(user_rank)   |   src_port   |   MyRole   |   Status   |    TlsStatus   |
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.031.750 [hccl_socket_manager.cc:647] [673943]   |----------------------|---------------|----------------------|--------------|------------|------------|----------------|
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.031.756 [hccl_socket_manager.cc:599] [673943]   |  192.7.3.198(1)   |  16666  |   192.7.2.199(0)   |  0  |  server  | time out |   DISABLE  |
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.032.113 [hccl_one_sided_conn.cc:68] [673943][Connect]call trace: hcclRet -> 9
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.032.116 [hccl_one_sided_conn.cc:366] [673943][ConnectWithRemote]call trace: hcclRet -> 9
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.032.127 [hccl_one_sided_service.cc:699] [673943][ConnectByThread] Connect failed. userrank[0], ret[9].
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.032.524 [hccl_one_sided_service.cc:743] [671395][HcclOneSidedService][CreateLinkFullmesh] Create links failed. commIdentifier[141.61.29.108:20311_141.61.29.108:21035].
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.032.539 [hccl_one_sided_service.cc:608] [671395][PrepareFullMesh]call trace: hcclRet -> 9
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.032.580 [hccl_one_sided_service.cc:653] [671395][HcclOneSidedService][Prepare] Prepare failed. commIdentifier[141.61.29.108:20311_141.61.29.108:21035]
[ERROR] HCCL(669593,python3):2025-12-17-18:13:19.032.583 [one_sided_service_adapt.cc:586] [671395][HcclCommPrepare]call trace: hcclRet -> 9
```

### 常见报错原因分析：

#### 场景一：建链时间不够

- **问题定位：**

由于Server侧和Client侧因为处理的业务不同（如PD分离场景），启动时间存在一定的差异，且当前建链接口的默认超时时间比较短，一般只有几秒钟，因此常会由于启动时间差超过了建链超时时间，导致建链失败的场景，针对此场景，需要排查建链超时的双端日志信息，根据信息排查两端的建链请求下发时间是否一致。

以上面的建链超时日志为例，当前建链的通信域为`141.61.29.108:20311_141.61.29.108:21035`（通信域名一般由两端ip和port信息组合而成），且`HcclCommPrepare`接口调用会在CANN日志的run目录下有下发记录，可以通过搜索如下命令查询两端的建链请求下发时间：

```bash
$ grep -r "HcclCommPrepare" |grep "141.61.29.108:20311_141.61.29.108:21035"
[INFO] HCCL(648214, python3):2025-12-17-18:13:19.250.498 [one_sided_service_adapt.cc:584] [651246]Entry-HcclCommPrepare:comm[141.61.29.108:20311_141.61.29.108:21035], timeout[3 s]

[INFO] HCCL(669593, python3):2025-12-17-18:13:16.032.551 [one_sided_service_adapt.cc:584] [671395]Entry-HcclCommPrepare:comm[141.61.29.108:20311_141.61.29.108:21035], timeout[3 s]
```

从建链请求的下发日志中可以看出，两端建链请求下发时间分别为`2025-12-17-18:13:19.250.498`和`2025-12-17-18:13:16.032.551`，且超时时间为3s，因此下发的时间间隔刚好超过了超时时间，导致建链失败。

- **解决方法：**

1. 如果是业务侧直接调用的hixl或者llmdatadist建链接口的场景，可通过入参配置更长的超时时间；
2. 如果时MoonCake间接调用hixl建链接口的场景，可以通过配置`ASCEND_CONNECT_TIMEOUT`环境变量配置更长的超时时间。

#### 场景二：建链两端链路不一致

[参考ISSUE](https://gitcode.com/cann/hixl/issues/15)

- **问题定位：**

当前hixl传输数据支持走ROCE或HCCS通信链路，但建链双端链路必须保持一致。一般日志中建链报错信息`LINK_ERROR_INFO`出现`192.xxx`开头的ip，说明本端走HCCS链路，使用的虚拟ip地址；否则走的是ROCE链路。

- **解决方法：**

排查建链两端是否配置了`HCCL_INTRA_ROCE_ENABLE`环境变量，两端需要同时配置为1，或者同时都不配置。

#### 场景三：建链两端TLS配置不一致

[参考ISSUE](https://gitcode.com/cann/hixl/issues/72)

- **问题定位：**

参与建链的两端设置的TLS配置必须保持一致，TLS使能的设备和TLS不使能的设备之间无法建链，报错现象为建链超时。

- **解决方法：**

检查设备之间TLS设置是否一致：

  ```bash
  # 检查设备的TLS状态
  for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch

  # TLS使能的设备和TLS不使能的设备无法建链，建议统一保持TLS关闭
  for i in {0..7}; do hccn_tool -i $i -tls -s enable 0; done
  ```

#### 场景四：HCCS场景调用llm_datadist.link接口建链未在ranktable中提供super_device_id信息

[参考ISSUE](https://gitcode.com/cann/hixl/issues/105)

- **问题定位：**

用户A3环境走HCCS链路调用`llm_datadist.link`接口建链时，需要使用version1.2的ranktable，并且必须提供`super_device_id`字段的内容，否则会建链超时，通常报错如下：

  ```bash
  [ERROR] DRV(28499, hccp_service.bin):2026-02-09-11:36:23.530.484 [devdrv_pcie.c:1064][devmng][drvGetDeviceDevIDByHostDevID 1064] invalid host device id 65535
  [ERROR] DRV(28499, hccp_service.bin):2026-02-09-11:36:23.530.504 [devdrv_info.c:628][devmng][devdrv_get_vnic_ip_by_sdid 628] Host devid transform to local devid failed. (devid=65535; ret=2)
  [ERROR] HCCP(28499, hccp_service.bin):2026-02-09-11:36:23.530.514 [rs.c:1847]tid:28494,rs_get_vnic_ip_info(1847) : phy_id:9 devdrv_get_vnic_ip_by_sdid fail! sdid:0xffffffff ret:1
  [ERROR] HCCP(28499, hccp_service.bin):2026-02-09-11:36:23.530.524 [rs.c:1874]tid:28494,rs_get_vnic_ip_infos(1874) : phy_id:9 get vnic ip info fail! ids[0]:0xffffffff type:1
  [ERROR] HCCP(28499, hccp_service.bin):2026-02-09-11:36:23.530.531 [ra_adp.c:1422]tid:28494,ra_rs_get_vnic_ip_infos(1422) : rs get vnic ip infos failed, ret 1
  [ERROR] HCCL(14799, python3.1):2026-02-09-11:36:23.191.491 [hccl_ip_address.cc:27][14957] ip addr [00000000 00000000 00000000 00000000] is invalid IPv6 address.
  [ERROR] HCCL(14830, python3.1):2026-02-09-11:31:31.825.623 [hccl_socket_manager.cc:816][14263][Wait][LinkEstablish]wait socket establish timeout, role[1] rank[1] timeout[120 s]
  ```

- **解决方法**

使用version1.2的ranktable，并且配置`super_device_id`字段，可以参考：[rank table配置资源信息](https://www.hiascend.com/document/detail/zh/canncommercial/850/commlib/hcclug/hcclug_000066.html)中的超节点模式组网。
其中，`super_device_id`的获取方式为：

  ```bash
  # 结果中的“SDID”即为super_device_id中写入的内容
  npu-smi info -t spod-info -i id -c chip_id
  ```
- id：设备id。通过`npu-smi info -l`命令查出的NPU ID即为设备id。
- chip_id：芯片id。通过`npu-smi info -m`命令查出的Chip ID即为芯片id。

#### 场景五：容器化场景下，p端日志中出现随机的端口号

- **问题定位：**

需开启 INFO 级别日志：

```bash
export ASCEND_GLOBAL_LOG_LEVEL=1
```

在 p 端日志中执行：

```bash
grep -nr ${p_virtual_ip} plog/ | grep "Listen"
```

其中 `${p_virtual_ip}` 是 p 端的虚拟 ip，获取方式为：

```bash
hccn_tool -i ${device_id} -vnic -g
```

日志里出现两次监听端口号不同（如 42155 与 45655），但 ranktable 中默认端口为 16666，则建链会超时。

- **解决方法：**

随机端口通常由容器化场景设置了 `HCCL_NPU_SOCKET_PORT_RANGE="auto"` 导致，关闭该环境变量即可。

## 2.建链报错

### 报错现象一：

用户在调用建链接口前，一般会先调用`RegisterMem`相关接口注册内存，把传输数据所申请的内存地址、大小以及类型等信息传递给底层，但实际上用户调用`RegisterMem`相关接口时，只是将注册的内存信息保存了起来，并没有触发内存的注册和信息交换。真正执行内存注册和交换的动作是在建链过程中触发的。因此，当注册内存时传入的内存信息不合法或者无效时，在建链阶段会触发内存注册失败相关日志，通常plog中有如下类似报错：

```bash
[ERROR] HCCL(76558,python3):2025-10-28-10:59:43.652.527 [hccl_mem.cc:53] [76619][HcclMemRegIpc]localbuffer init failed 15.
[ERROR] HCCL(76558,python3):2025-10-28-10:59:43.652.543 [hccl_one_sided_service.cc:792] [76619][RegisterBoundMems]call trace: hcclRet -> 15
[ERROR] HCCL(76558,python3):2025-10-28-10:59:43.652.546 [hccl_one_sided_service.cc:601] [76619][PrepareFullMesh]call trace: hcclRet -> 15
[ERROR] HCCL(76558,python3):2025-10-28-10:59:43.652.731 [hccl_one_sided_service.cc:644] [76619][HcclOneSidedService][Prepare] Prepare failed. commIdentifier[33.215.119.85:20359_33.215.116.115:20515]
[ERROR] HCCL(76558,python3):2025-10-28-10:59:43.652.734 [one_sided_service_adapt.cc:586] [76619][HcclCommPrepare]call trace: hcclRet -> 15
```

### 常见报错原因分析：

#### 场景一：HCCS场景内存首地址没有2M对齐

[参考ISSUE](https://gitcode.com/cann/hixl/issues/14)

- **问题定位：**

当前走HCCS链路传输数据时，底层驱动要求要求注册内存的首地址需要2MB对齐，否则建链时内存注册会失败，通常plog首报错如下：

```bash
[ERROR] DRV(76558,python3):2025-10-28-10:59:43.651.960 [drv_log_user.c:621][ascend][curpid:76558,76619][drv][devmm][share_log_read_in_single_module]Invalid para. (va=0x12c1c0f40200; page_size=2097152; devid=0; export=0; import=0) Create_para_check fail. (va=0x12c1c0f40200) Ipc node attr pack fail. (ret=-22; vptr=0x12c1c0f40200; len=1998848)
[ERROR] DRV(76558,python3):2025-10-28-10:59:43.651.977 [devmm_svm.c:1451][ascend][curpid:76558,76619][drv][devmm][halShmemCreateHandle]<errno:22, 8> Share memory create device error. (ret=8; vptr=0x12c1c0f40200)
```

可以从日志中看出，注册的内存地址`0x12c1c0f40200`无法被2MB整除，即没有2M对齐。

- **解决方法：**

1. 当注册的内存小于2MB时，用户申请内存时应指定`ALC_MEM_MALLOC_HUGE_ONLY`内存申请策略来强制申请大页内存，确保申请的内存最小为2M；
2. 当用户申请的内存不是大页内存，而是普通页内存时，需要用户自己保证内存首地址2M对齐，对齐方法可参考如下示例：

假设用户申请的内存地址首地址为`addr`，内存大小为`mem_size`字节，对齐内存块为`ALIGNMENT_BLOCK = 2 * 1024 *1024`字节，则对齐后的内存首地址为：

```python
aligned_addr = (addr + ALIGNMENT_BLOCK - 1) // ALIGNMENT_BLOCK * ALIGNMENT_BLOCK
```

同时，为了避免地址2M对齐后的内存越界，注册的内存大小应减去偏移的大小：

```python
aligned_mem_size = mem_size - (aligned_addr - addr)
```

#### 场景二：注册内存大小超过申请的内存大小

[参考ISSUE](https://gitcode.com/cann/hixl/issues/26)

- **问题定位：**

当用户调用内存注册接口时传入的内存大小超过了实际申请的内存大小范围时，建链时内存注册会失败，通常plog存在如下类似报错：
报错为注册MR失败，也常见于注册的内存时不支持的内存类型，比如使用HOST内存使用MallocPhysical申请物理内存/再映射成虚拟内存时，当前不支持注册注册给ROCE网卡。

```bash
[ERROR] HCCP(130245,python3):2025-11-08-08:55:21.692.085 [ra_hdc_rdma.c:609]tid:131014,ra_hdc_typical_mr_reg : [reg][ra_hdc_typical_mr]ra hdc message process failed ret(-13) phy_id(2)
[ERROR] HCCL(130245,python3):2025-11-08-08:55:21.692.090 [adapter_hccp.cc:332] [131014][hrtRaRegGlobalMr]errNo[0x0000000005000013] ra reg global mr fail. return[128102], params: addr[0x12c085200000], size[143917056 Byte], access[7]
[ERROR] HCCL(130245,python3):2025-11-08-08:55:21.692.093 [local_rdma_rma_buffer_impl.cc:120] [131014][Init]call trace: hcclRet -> 19
```

- **解决方法**

1. 排查业务代码中，内存注册传入的内存大小是否超过了内存申请时传入的大小；
2. 当申请的是普通页内存并且手动对齐了内存时，需确保偏移后的内存大小不会越界。

#### 场景三：out of memory 导致建链失败（用户 HBM 不足）

> **与场景四区分：** 本场景 plog 中能见到 `DevMemAllocHugePageManaged` / `halMemAlloc ... out of memory`（**用户 HBM** 不足）。若首错为 `ibv_cmd_create_cq failed, ret 12` 且无 HBM OOM 日志，见 **场景四（device 系统内存）**。

- 问题定位：

在PD分离部署场景中，D端向P端发起建链请求时建链失败，D端plog有如下类似报错：

```bash
[ERROR] HCCL(128258,ker_DP51_EP51):2026-02-27-16:24:15.682.776 [adapter_hccp.cc:1505] [151808][Recv][RaSocket]errNo[0x000000000500000d] recv fail, data[0xfffceb7ee5e8], size[48], rtRet[228205]
[ERROR] HCCL(128258,ker_DP51_EP51):2026-02-27-16:24:15.682.785 [hccl_socket.cc:440] [151808][Recv]call trace: hcclRet -> 13
[ERROR] HCCL(128258,ker_DP51_EP51):2026-02-27-16:24:15.682.788 [transport_roce_mem.cc:726] [151808][ExchangeNotifyValueBuffer]call trace: hcclRet -> 13
[ERROR] HCCL(128258,ker_DP51_EP51):2026-02-27-16:24:15.682.792 [transport_roce_mem.cc:331] [151808][ConnectImpl]call trace: hcclRet -> 13
[ERROR] HCCL(128258,ker_DP51_EP51):2026-02-27-16:24:15.682.870 [transport_roce_mem.cc:363] [151738][ConnectImplWithTimeout]ConnectImplWithTimeout Prepare failed
```

同时，P端有内存不足的相关日志打印，PD两端的报错日志时间一致，P端类似日志如下：

```bash
ERR0R] RUNTIME(27271,ker_DP7_EP7) :2026-02-27-16:24:15.595.677 [npu_driver.cc:1534]34603 DevMemAllocHugePageManaged:[drv api] halMemAlloc failed:size=2122688(bytes), type=2, moduleId=3, drvFlag=216172782123369479, drvRetCode=6, device_id 7, ErrCode=207001, desc=[driver error:out of memory], InnerCode=0x7020016
ERROR] RUNTIME(27271,ker_DP7_EP7) :2026-02-27-16:24:15.735.937 [npu_driver.cc:1534]39787 DevMemAllocHugePageManaged:[drv api] halMemAlloc failed:size=2097152(bytes), type=2, moduleId=3, drvFlag=216172782123369479, d
7, ErrCode=207001, desc=[driver error:out of memory], InnerCode=0x7020016

```

在建链时底层每条链路会占用大约2M的**用户 HBM**，推测由于 HBM 申请失败导致建链失败

- 解决方法：减少 P 端 **HBM** 占用（如调低 vLLM `--gpu-memory-utilization`）。ROCE 池化 HBM 与 device 系统内存区分见 Wiki「池化方案总览 · HOST 内存大小限制」。

#### 场景四：ROCE 注册 HOST 内存导致 device 系统内存不足（非 HBM）

[参考ISSUE](https://gitcode.com/cann/hixl/issues/382)

- **与场景三的区别：**
  - 场景三：`DevMemAllocHugePageManaged` / `halMemAlloc ... out of memory` → **用户 HBM** 不足。
  - 场景四：`ibv_cmd_create_cq failed, ret 12`（errno 12 = ENOMEM）→ **NPU device 系统内存**（页表/RDMA CQ·QP，单卡约几百 MB）不足；**不是**调低 `--gpu-memory-utilization` 能直接解决的 HBM 问题。

- **问题定位：**

  ROCE 池化需把 **HOST(DRAM) 内存注册给 RDMA 网卡**。注册会在 device 侧消耗**系统内存**（页表等）：
  - HDK < 25.5.0：按 **4KB** 粒度建页表，大池极易打满 device 系统内存；
  - HDK ≥ 25.5.0：按 host 实际页粒度（2MB 大页 → 每 2MB 约 1 个 device 页表项），全大页可显著降低消耗。

  典型 plog（首错常在 HCCL 建链失败之前约 1s）：

  ```bash
  [ERROR] ROCE(...,hccp_service.bin):... ibv_cmd_create_cq failed, ret 12
  [ERROR] HCCP(...,hccp_service.bin):... create cq failed ret -259
  [ERROR] HCCP(...,hccp_service.bin):... qp create failed ret[-259]
  [ERROR] HCCL(...):... Connect failed. userrank[...], ret[13].
  [ERROR] HCCL(...):... [HcclCommPrepare]call trace: hcclRet -> 13
  [ERROR] GE(...):... ErrorNo: 503900() ... Call hccl api failed
  ```

- **Wiki 参考（HOST 注册与 device 系统内存）：**
  [Mooncake + HIXL 池化方案总览（A2 - A3）](https://gitcode.com/cann/hixl/wiki/Mooncake%20+%20HIXL%20%E6%B1%A0%E5%8C%96%E6%96%B9%E6%A1%88%E6%80%BB%E8%A7%88%EF%BC%88A2%20-%20A3%EF%BC%89.md) → **「HOST 内存大小限制」** 小节（注册消耗 device 系统内存、大页、`MC_STORE_USE_HUGEPAGE`）。

- **解决方法：**
  1. **保证 HOST 池化内存用大页申请**（`aclrtMallocHost` 优先 2MB 大页；启动前 `drop_caches` + `compact_memory`）；
  2. 大池使用 **预留大页 + mmap**：`nr_hugepages` + `export MC_STORE_USE_HUGEPAGE=1`（mooncake ≥ 0.3.11）；
  3. **降低池化 HOST 内存规模**（如减小 `global_segment_size_gb`）；
  4. 升级 HDK ≥ 25.5.0，使页表按大页粒度计费；
  5. 勿与场景三混淆：若无 `DevMemAllocHugePageManaged`，优先按本场景排查。

### 报错现象二：

用户在建链时，需要获取device_ip信息生成rank_table，如果获取不到会触发rank_table校验失败导致建链失败。通常plog中包含如下信息：
``` tex
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.320 [topoinfo_ranktableConcise.cc:446] [737][Get][SingleDeviceIp]'device_ip' is not set correctly,must be set when multi Server or HCCL_INTRA_ROCE_ENABLE enabled
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.329 [topoinfo_ranktableConcise.cc:379] [737][GetSingleDevice]call trace: hcclRet -> 1
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.337 [topoinfo_ranktableConcise.cc:314] [737][GetDeviceList]call trace: hcclRet -> 1
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.344 [topoinfo_ranktableConcise.cc:295] [737][Get][SingleServer]get dev list error:serverId[10.44.115.130]
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.349 [topoinfo_ranktableConcise.cc:245] [737][GetServerList]call trace: hcclRet -> 1
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.359 [topoinfo_ranktableConcise.cc:195] [737][GetRanktableInfo]call trace: hcclRet -> 1
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.364 [topoinfo_ranktableConcise.cc:112] [737][ParserClusterInfo]call trace: hcclRet -> 1
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.368 [topoinfo_ranktableConcise.cc:50] [737][Init]call trace: hcclRet -> 1
[ERROR] HCCL(705,python):2026-02-11-06:09:23.236.373 [config.cc:61] [737][CfgGetClusterInfo]call trace: hcclRet -> 1
```

### 常见报错原因分析：

#### 场景一：使用低版本CANN包时，缺乏hccn.conf文件

[参考ISSUE](https://gitcode.com/cann/hixl/issues/113)

- **问题定位：**

  通常发生在容器场景中，未挂载`/etc/hccn.conf`文件导致

- **解决方法**

    1. 修改容器配置，将`/etc/hccn.conf`文件挂载到容器
    2. hixl目前已经支持通过hccn_tool自动获取device_ip，升级CANN包版本以支持hixl新特性

#### 场景二：hccn.conf文件缺少device信息

[参考ISSUE](https://gitcode.com/cann/hixl/issues/70)

- **问题定位：**

  查看hccn.conf文件内容，发现没有device相关信息

- **解决方法**

  参考[hccn_tool文档](https://support.huawei.com/enterprise/zh/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference)，使用hccn_tool工具查询并将device信息补充到hccn.conf文件中。

### 报错现象三：

多个进程共用同一张 NPU（同一张 device 网卡）时，ADXL 建链会在 ranktable 校验阶段失败。典型场景包括：Mooncake store 与 client 调度到同一物理节点且均 `set_device(0)`；standalone `mooncake_client` 与推理进程 embedded client 同卡共存。通常 plog 首错如下：

```bash
Ranktable_Check_Failed(EI0014): The ranktable information check failed, Reason:[The IP is used repeatedly].
[ERROR] HCCL(...):... [topoinfo_ranktableParser.cc:522] ... [RanktableCheck] ... [device_ip]:[172.16.0.70] is already exist
[ERROR] HCCL(...):... [topoinfo_ranktableConcise.cc:308] ... get dev list error:serverId[172.30.126.149]
[ERROR] GE(...):... [adxl_engine_impl.cc:205] ... Failed to connect, remote engine:172.30.126.149:20010
```

> **机制说明：** device 侧网卡默认监听端口为 **16666**。未配置 `comm_resource_config.listen_port` 时，HIXL 自动生成的 ranktable **不携带** `device_port` 字段；多进程共用同一 NPU 时，合并后的 ranktable 无法区分各进程通信资源，HCCL 校验失败。配置 `listen_port` 后，ADXL 将其写入 ranktable 的 `device_port`，支持单卡多进程建链。详见 [HIXL GlobalResourceConfig — listen_port](https://gitcode.com/cann/hixl/blob/master/docs/zh/api/cpp/HIXL-interface.md)。

#### 场景一：Mooncake store 与 client 同节点同卡

[参考ISSUE](https://gitcode.com/cann/hixl/issues/185)

- **问题定位：**

  store 与 client 在同一物理节点绑定同一张 NPU（如均为 `torch_npu.npu.set_device(0)`）。Pod 重调度到不同节点时可能偶发成功。

- **解决方法：**

  为 **每个进程** 配置 **不同** 的 `comm_resource_config.listen_port`（取值范围 [1, 65535]），通过环境变量 `ASCEND_GLOBAL_RESOURCE_CONFIG` 传入 Mooncake / ADXL：

  ```bash
  # mooncake-store 侧
  export ASCEND_GLOBAL_RESOURCE_CONFIG='{"comm_resource_config.listen_port":26666}'

  # client 侧（端口必须与 store 不同）
  export ASCEND_GLOBAL_RESOURCE_CONFIG='{"comm_resource_config.listen_port":26667}'
  ```

  也可在 HIXL 初始化时通过 `OPTION_GLOBAL_RESOURCE_CONFIG` 传入同等 JSON。

- **备选规避：** 两进程使用不同 logic device（如 `set_device(0)` / `set_device(1)`），或调度策略避免同节点同卡共存。

#### 场景二：standalone mooncake_client 与 embedded client 同卡

- **问题定位：**

  除 **16666 端口冲突** 外，未区分 `listen_port` 时亦可能触发 ranktable 校验失败（见报错现象三日志）。

- **解决方法：**

  为 standalone `mooncake_client` 配置与 embedded client **不同** 的 `listen_port`，参见 [Mooncake Store 各种部署模式介绍](https://gitcode.com/cann/hixl/wiki/Mooncake%20Store%20%E5%90%84%E7%A7%8D%E9%83%A8%E7%BD%B2%E6%A8%A1%E5%BC%8F%E4%BB%8B%E7%BB%8D.md) §2.6。

### FabricMem模式报错：halMemImportFromShareableHandleV2 failed

- **可能原因：** 机器跨了超节点

# 数据传输失败类问题

## 1.数据传输超时

[参考ISSUE](https://gitcode.com/cann/hixl/issues/53)
### 报错现象：

业务侧调用传输接口时超时，通常plog中有如下类似报错：

```bash
[ERROR] RUNTIME(14163,python3):2025-12-13 12:15:25.025.554 [stream.cc:1402]14595 SynchronizeExecutedTask:Stream synchronize timeout, device_id=11, stream_id=103, timeout=10000, tryCount=10.
[ERROR] RUNTIME(14163,python3):2025-12-13 12:15:25.025.562 [stream.cc:1465]14595 SynchronizeImpl:failed, stream_id=103, error=0x7030010
[ERROR] RUNTIME(14163,python3):2025-12-13 12:15:25.080.727 [api_error.cc:960]14595 RtStreamSynchronizeWithTimeout:Failed, stream_id=103, timeout=10000ms.
[ERROR] RUNTIME(14163,python3):2025-12-13 12:15:25.080.730 [api_error.cc:1783]14595 stream_sync_error:StreamSyncError: code=507046, des=[stream sync timeout], InnerCode=0x7030010
[ERROR] RUNTIME(14163,python3):2025-12-13 12:15:25.080.727 [error_message_manage.cc:511]14595 FuncErrorReason:report error module name=EE1002
[ERROR] RUNTIME(14163,python3):2025-12-13 12:15:25.080.730 [error_message_manage.cc:511]14595 FuncErrorReason:RtStreamSynchronizeWithTimeout execute failed, reason=[stream sync timeout] (507046)
```

#### 场景一：ROCE网络存在丢包重传
当出现建链成功，但是数据传输过程中出现超时报错的问题，可能是由于网络问题(网络闪断、网络拥塞等)导致超时，进一步引发重传超次，从而导致业务报错退出。

重要：如果匹配到次场景，一定要提醒用户：确保调用HIXL传输接口的超时时间 > RDMA网卡重传超时时间 * RDMA网卡重传次数，否则超时不会报出底层重传超次的错误。可以调小HCCL_RDMA_TIMEOUT，也可以增大总的超时时间。
* 在run plog中查看RDMA网卡重传超时系数[`HCCL_RDMA_TIMEOUT`](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/850/maintenref/envvar/envref_07_0091.html)和RDMA网卡重传次数`HCCL_RDMA_RETRY_CNT`的配置值。
* HCCL_RDMA_TIMEOUT环境变量控制RDMA网卡单个数据包的重传时间，计算公式为：`4.096μs * 2^$HCCL_RDMA_TIMEOUT`，默认值为20，对应具体时间是4s左右，建议调小。
* HCCL_RDMA_RETRY_CNT环境变量控制RDMA网卡重传次数，默认是7次。
* 如果是Mooncake间接调用hixl传输接口的场景，通过`ASCEND_TRANSFER_TIMEOUT`环境变量配置合适的传输超时时间。
* 如果出现"error cqe status", 说明出现了重传超次，网卡存在故障或闪断，报告给用户。

HCCL_RDMA_TIMEOUT超时时间速算表格：

|HCCL_RDMA_TIMEOUT  | 具体时间|
|--|--|
|10  |  0.004s |
|11  |  0.008s |
|12  |  0.016s |
|13  |  0.033s |
|14  |  0.067s |
|15  |  0.134s |
|16  |  0.268s |
|17  |  0.536s |
|18  |  1.07s |
|20  |  4s |


#### 场景二： 网卡和交换机PFC配置不匹配问题
网卡和交换机PFC配置不匹配时会导致传输性能很差，当使用ROCE且传输性能很差时，推荐用户查看交换机和网卡PFC是否匹配。
总体原则：NPU和交换机关于PFC参数保持一致，推荐开启队列4。

查询NPU侧PFC参数：
```
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -pfc -g
PFC configuration:
priority   0 1 2 3 4 5 6 7
enabled    0 0 0 0 1 0 0 0
```

查询并修改交换机的值，方法参考

| Leaf-01                                              | 命令说明                                                     |
| ---------------------------------------------------- | ------------------------------------------------------------ |
| dcb pfc dscp-mapping enable slot 1                   | 使能PFC功能基于DSCP映射后的优先级进行反压。                  |
| dcb pfc server<br>  priority 4 <br>  undo priority 3 | 1.全局使能PFC。<br> 2.使能无损队列为4队列（priority命令是累增式命令，多次配置时，配置结果按多次累加生效）。<br> 3.缺省情况下，优先级队列3已使能PFC功能，取消优先级队列3的PFC功能。 |

## 2.数据传输时内存类型校验失败

### 报错现象

常见于中转模式，当前HIXL默认配置了BufferPool的Option，如果有内存没注册就会被当作HOST内存走入中转模式，查看plog，包含如下关键字：

``` tex
CheckMemCpyAttr: src's real memory type is * , but * is inputed, or real device id is * , but * is inputed.
```

### 常见报错原因分析
* 使用torch申请HOST内存没有带pin_memory=True, 申请的HOST内存RUNTIME无法识别
* 系统默认开启了BufferPool=4:8(通过日志核对是否开启)，没有注册的device内存会被当作HOST内存进行中转传输，拷贝类型就会设置错误。
  补充：`没有注册的device内存会被当作HOST内存进行中转传输`对应的代码（MemType local_mem_type = local_segment != nullptr ? local_segment->GetMemType() : MemType::MEM_HOST;）
* 初始化了多个Hixl或多个AdxlEngine，注册内存的Hixl/AdxlEngine和传输时用的Hixl/AdxlEngine不是同一个，行为和device内存没有注册时一样的。

附上RUNTIMECheckMemCpyAttr的代码，枚举值RT_MEMORY_LOC_HOST：0，RT_MEMORY_LOC_DEVICE：1
``` cpp
rtError_t ApiImpl::CheckMemCpyAttr(const void * const dst, const void * const src, const rtMemcpyBatchAttr &memAttr,
    rtPtrAttributes_t &dstAttr, rtPtrAttributes_t &srcAttr)
{
    rtMemLocationType memType;
    rtError_t error = PtrGetAttributes(dst, &dstAttr);
    ERROR_RETURN(error, "get dst attribute failed, error=%#x", error);
    memType = (dstAttr.location.type == RT_MEMORY_LOC_UNREGISTERED) ? RT_MEMORY_LOC_HOST : dstAttr.location.type;
    COND_RETURN_OUT_ERROR_MSG_CALL(((memType != memAttr.dstLoc.type) ||
        ((memType == RT_MEMORY_LOC_DEVICE) && (dstAttr.location.id != memAttr.dstLoc.id))), RT_ERROR_INVALID_VALUE,
        "The real memory type of dst is %d, but the input type is %d. Or the real device ID is %d, but the input ID is %d.",
        memType, memAttr.dstLoc.type, dstAttr.location.id, memAttr.dstLoc.id);

    error = PtrGetAttributes(src, &srcAttr);
    ERROR_RETURN(error, "get src attribute failed, error=%#x.", error);
    memType = (srcAttr.location.type == RT_MEMORY_LOC_UNREGISTERED) ? RT_MEMORY_LOC_HOST : srcAttr.location.type;
    COND_RETURN_OUT_ERROR_MSG_CALL(((memType != memAttr.srcLoc.type) ||
        ((memType == RT_MEMORY_LOC_DEVICE) && (srcAttr.location.id != memAttr.srcLoc.id))), RT_ERROR_INVALID_VALUE,
        "The real memory type of src is %d, but the input type is %d. Or the real device ID is %d, but the input ID is %d.",
        memType, memAttr.srcLoc.type, srcAttr.location.id, memAttr.srcLoc.id);

    return RT_ERROR_NONE;
}
```
#### 场景一：未使用aclrtMallocHost / rtMallocHost接口申请host内存导致底层无法识别内存类型

[参考ISSUE](https://gitcode.com/cann/hixl/issues/17)

- **问题定位**

  当用户调用传输接口时传入的类型和实际的内存类型不一致，导致传输失败。

- **解决方法**

1. 排查业务代码中，数据传输接口传入的内存类型和注册的内存类型是否一致；

2. 当前系统底层只能识别rtMallocHost类型的host内存，通过torch创建cpu tensor时需要添加`pin_memory=True`, 这样创建的内存才是使用rtMallocHost创建的内存，如：

   ```python
   host_tensor = torch.ones(xxx, pin_memory=True)
   data_ptr = host_tensor.data_ptr()
   ```

#### 场景二： 使用未注册内存

- **问题定位**
  在使用Mooncake + HIXL Ascend_direct_transport 进行PD分离传输任务时，使用未注册内存导致传输失败。通过以下步骤进行排查：
    1. 确保Mooncake编译时，使用 `USE_ASCEND_DIRECT=ON` ，使能HIXL
    2. 通过设置环境变量 `MC_LOG_LEVEL=INFO` ,开启Mooncake INFO日志，查找关键字 “AscendDirectTransport register mem addr”，可以查看注册内存的首地址、长度、类别等信息
    3. 上层在进行内存操作（例如调用put或者get时），目前可以通过添加打印日志的方式查看操作的内存地址
    4. 排查内存操作是否发生在未注册内存，导致出现校验失败

- **解决方法**

  在业务编码中，确保内存成功注册之后再进行相关传输操作

## 3.报错：Can't find remoteBuffer by key
错误日志实例：
```
[ERROR] HCCL(654918,python3):2026-03-18-11:23:42.403.633 [exception_handler.cc:28] [656133]HcclBatchPut: Logic error, what: hccl_one_sided_conn.cc:271,BatchWrite, Error: Logic error, ret=4 Can't find remoteBuffer by key
```
可能情况:
* 没有注册对端的内存，或者在建链之后注册的内存：在对端的日志查找注册内存的日志，比如INFO日志：Add local mem range start ...
* 没有配置HCCL_INTRA_ROCE_ENABLE=1，对端是HOST内存 （HCCS不支持HOST内存）

## 4.FabricMem模式报错：suspect remote error

- **原因：** 灵渠版本没有升级，要求大于 1.5.0

## 5.使用CANN9.0 FabricMem模式出现cpu stuck

CANN 9.0 需要开启下面的配置，否则可能出现 cpu stuck：

```bash
export ASCEND_GLOBAL_RESOURCE_CONFIG='{"fabric_memory.max_capacity": "32"}'
```

# 典型案例分析

## RoCE 直传多对一场景下性能退化的定位与修复

### 1.案例背景

模拟在 PD 分离架构中，利用 RoCE 协议进行 KV Cache 数据传输。在 8 卡环境中分别做 Put/Get 操作，0 卡将 32 个 Block 数据分发到 Host 内存，0-7 卡同时从 Host 内存中 Get 这 32 个 Block。

### 2.问题现象

通过 Host 侧 CPU 展开传输平均耗时 200ms 左右，而通过 AICPU 展开时部分任务耗时高达 10s 左右，严重不符合预期。

### 3.定位与分析

- 通过 Profiling 发现 AICPU 多线程展开存在排队；
- AICPU 展开 HCCL 单边通讯过程中存在 WQE 耗时很长（20s 左右）；
- 将 WR 队列深度从 128 提升至 1024，重传间隔从 1ms 缩短至 100μs 后，单 Block 耗时大部分 200μs；
- 网卡统计存在大量 RDMA 重传，多打一（多个口进单口出）冲突导致带宽下降；
- 在交换机上配置 PFC 与 NPU 参数一致后，单次任务传输耗时约 30ms。

### 4.解决方法

总体原则：NPU 和交换机关于 PFC 参数保持一致。NPU 侧检查 priority 4 与 DSCP 映射，交换机侧参考 Wiki 配置 PFC 队列 4。

# 常用配置及查询方式

### hccl_tool常用命令

查询节点 device ip 信息，以 8 卡为例：

```bash
for i in {0..7}; do hccn_tool -i $i -ip -g; done
```

查询 device 之间网络是否连通：

```bash
hccn_tool -i 0 -ping -g address x.x.x.x
```

检查设备之间 TLS 设置是否一致（**只读查询**，勿在此执行修改命令）：

```bash
for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch
```

若两端 TLS 不一致且需统一关闭，参见上文「建链超时 → 场景三」中的修改命令（含风险说明）。

查询 device 网络连接状态及历史状态：

```bash
for i in {0..7}; do hccn_tool -i $i -link -g; done
for i in {0..7}; do hccn_tool -i $i -link_stat -g; done
```

### RoCE网口网络配置

Atlas A3 训练/推理系列产品默认同卡双 die 之间网络不互通，若需配置同卡双 die 通信，请参考 RoCE 网口网络配置文档。
