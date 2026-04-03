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

## 快速索引

先看首条致命错误，再按下面的关键词跳读对应章节，不要整份从头到尾顺读。

| 常见关键词 / 线索 | 优先查看章节 | 备注 |
|---|---|---|
| `wait socket establish timeout` | `1.建链超时` | 先确认是否存在 `HcclCommPrepare`、`LINK_ERROR_INFO` |
| `HcclCommPrepare` | `1.建链超时` | 多机场景优先核对双端下发时间 |
| `LINK_ERROR_INFO` | `1.建链超时 -> 场景二/三` | 重点看链路一致性与 TLS 配置 |
| `super_device_id`、`invalid host device id 65535` | `1.建链超时 -> 场景四` | HCCS + ranktable 场景常见 |
| `device_ip is not set correctly` | `2.建链报错 -> 报错现象二` | 常与 `hccn.conf` 或 CANN 版本相关 |
| `CheckMemCpyAttr` | `2.数据传输时内存类型校验失败` | 先判断是否落入 Buffer 路径 |
| `Can't find remoteBuffer by key` | `3.报错：Can't find remoteBuffer by key` | 常见于对端内存未注册或注册时机不对 |
| `out of memory`、`DevMemAllocHugePageManaged` | `2.建链报错 -> 场景三` | 重点看 device 侧空闲内存 |
| `stream sync timeout`、`RtStreamSynchronizeWithTimeout` | `1.数据传输超时` | 结合 RDMA 重传参数一起分析 |
| `roce`、`ping`、`ib_send_bw` | `环境ROCE不通` | 先确认设备间联通性 |
| `PFC`、`DSCP`、`priority 4` | `网卡和交换机PFC配置不匹配问题` | 环境类问题，和业务日志分开看 |

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

#### 场景三：out of memory导致建链失败

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

在建链时底层每条链路会占用大约2M的device侧内存，推测由于内存申请失败导致建链失败

- 解决方法：减少device内存占用，以便有空闲内存可以申请。

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
