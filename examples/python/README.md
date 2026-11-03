## 目录

- [目录](#目录)
- [样例介绍](#样例介绍)
- [目录结构](#目录结构)
- [环境准备](#环境准备)
- [样例配置说明](#样例配置说明)
- [样例运行](#样例运行)

## 样例介绍

功能：通过LLM-DataDist接口实现分离部署场景下KvCache的管理功能，以及通过HIXL接口实现D2D传输。

## 目录结构

```
├── python
|   ├── llm_datadist
|   |   ├── pull_blocks_sample.py
|   |   ├── pull_blocks_xpyd_sample.py
|   |   ├── pull_cache_sample.py
|   |   ├── pull_from_cache_to_blocks.py
|   |   ├── push_blocks_sample.py
|   |   ├── push_cache_sample.py
|   |   ├── switch_role_sample.py
|   |   ├── transfer_cache_async_sample.py
|   |   ├── hixl_transfer_backend_sample.py
|   ├── hixl
|   |   ├── hixl_d2rd_multiproc_sample.py
```

## 环境准备

- 安装根目录下requirements.txt依赖（如果前面已经安装则跳过此步）：

  ```bash
  pip3 install -r requirements.txt
  ```
- 安装**pytorch**与**torch_npu**包

  根据Python版本、CANN版本和CPU架构，在[TorchNPU版本配套说明](https://gitcode.com/Ascend/pytorch/blob/master/COMPATIBILITY.md)中选择兼容版本组合，再按照[TorchNPU安装指南](https://gitcode.com/Ascend/pytorch/blob/master/README.zh.md#安装)安装对应版本的`pytorch`和`torch_npu`。

  例如，使用Python 3.11且CANN版本为9.1.0时，可安装PyTorch 2.10.0和对应的TorchNPU安装包2.10.0.post4：

  ```bash
  pip3 install torch==2.10.0 --index-url https://download.pytorch.org/whl/cpu
  pip3 install torch-npu==2.10.0.post4
  ```

  该样例仅用于说明安装方式，实际版本组合仍以版本配套说明中的表格为准。

  安装完成后执行以下命令检查环境：

  ```bash
  python3 -c "import torch, torch_npu; print(torch.__version__, torch_npu.__version__, torch.npu.is_available())"
  ```

## 样例配置说明

以下所有用例运行均需正确设置Ascend环境变量，所有双机示例需尽量保证同步执行。
```
source ${HOME}/Ascend/cann/set_env.sh
```
`${HOME}/Ascend`请替换相关软件包的实际安装路径。

- 更改样例中的device信息
  - 将PROMPT_IP_LIST中的device_ip修改为Prompt主机的各device_ip。
  - 将PROMPT_HOST_IP修改为Prompt主机的host_ip。
  - 将DECODER_IP_LIST中的device_ip修改为Decoder主机的各device_ip。
  - 将DECODER_HOST_IP修改为Decoder主机的host_ip。
  - 两台机器脚本保持一致。

- 下面个别用例支持在A5环境使用RDMA链路执行，并且需要在双机上执行。在A5环境中未手动配置local_comm_res时默认使用UB协议；如果需要使用RDMA链路，需要手动配置local_comm_res，配置方法参考[**表 2** options（Ascend 950PR/Ascend 950DT）](../../docs/zh/api/cpp/HIXL-interface.md#initialize)。可通过以下操作获取 host 网卡的 ip 信息：
  ```shell
  # 查询RoCE设备和网口的对应关系，查看状态为Up的网口名
  ibdev2netdev

  # 根据网口名找出对应的ip信息
  ifconfig
  ```

## 样例运行
- 执行pull cache样例程序，此样例程序展示了配置内存池场景下，使用allocate_cache，双向建链，并从远端pull_cache
  - 说明：
    本示例支持双机和单机执行。双机执行时，参考[样例配置说明](#样例配置说明)配置两台主机的device信息。

  双机执行时，分别在Prompt主机与Decoder主机执行样例程序，其中device_id为要使用的device_id，cluster_id为集群ID且在所有参与建链的范围内需要确保唯一：
    ```
    # Prompt主机:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_cache_sample.py --device_id 0 --cluster_id 1
    # Decoder主机:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_cache_sample.py --device_id 0 --cluster_id 2
    ```

  单机执行时，需要在同一台主机上同时执行Prompt与Decoder进程。其中host_ip为本机host_ip：
    ```
    # Prompt进程:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_cache_sample.py --device_id 0 --cluster_id 1 --is_single true --host_ip 10.10.10.1
    # Decoder进程:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_cache_sample.py --device_id 1 --cluster_id 2 --is_single true --host_ip 10.10.10.1
    ```
- 执行pull blocks样例程序，此样例程序使用torch自行申请内存，双向建链，并从远端pull_cache
  - 说明：
    本示例支持双机和单机执行。双机执行时，参考[样例配置说明](#样例配置说明)。

  双机执行时，分别在Prompt主机与Decoder主机执行样例程序，其中device_id为要使用的device_id，cluster_id为集群ID且在所有参与建链的范围内需要确保唯一：
    ```
    # Prompt主机:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_sample.py --device_id 0 --cluster_id 1
    # Decoder主机:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_sample.py --device_id 0 --cluster_id 2
    ```

  单机执行时，需要在同一台主机上同时执行Prompt与Decoder进程。其中host_ip为本机host_ip：
    ```
    # Prompt进程:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_sample.py --device_id 0 --cluster_id 1 --is_single true --host_ip 10.10.10.1
    # Decoder进程:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_sample.py --device_id 1 --cluster_id 2 --is_single true --host_ip 10.10.10.1
    ```
- 执行pull_from_cache_to_blocks样例程序：
  - 说明：
    本示例支持双机和单机执行。双机执行时，参考[样例配置说明](#样例配置说明)。

  双机执行时，分别在Prompt主机与Decoder主机执行样例程序，其中device_id为要使用的device_id，cluster_id为集群ID且在所有参与建链的范围内需要确保唯一：
    ```
    # Prompt主机:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_from_cache_to_blocks.py --device_id 0 --cluster_id 1
    # Decoder主机:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_from_cache_to_blocks.py --device_id 0 --cluster_id 2
    ```

  单机执行时，需要在同一台主机上同时执行Prompt与Decoder进程。其中host_ip为本机host_ip：
    ```
    # Prompt进程:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_from_cache_to_blocks.py --device_id 0 --cluster_id 1 --is_single true --host_ip 10.10.10.1
    # Decoder进程:
    HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_from_cache_to_blocks.py --device_id 1 --cluster_id 2 --is_single true --host_ip 10.10.10.1
    ```
- 执行push_blocks样例程序，此样例程序使用单侧建链方式，申请内存并注册blocks,  decoder发起建链并push blocks
  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip。默认走通信域传输后端，可通过`--transfer_backend hixl`切换为hixl cs后端：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/push_blocks_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/push_blocks_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```
  若在A5环境执行，需指定`--transfer_backend hixl`；未手动配置local_comm_res时默认走UB链路，也可手动配置local_comm_res使用RDMA链路，如：
    ```
    # Prompt主机:
    python llm_datadist/push_blocks_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1 --transfer_backend hixl
    # Decoder主机:
    python llm_datadist/push_blocks_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0 --transfer_backend hixl
    ```
- 执行push_cache样例程序：此样例程序使用单侧建链方式，申请内存并注册cache,  decoder发起建链并push cache
  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip。默认走通信域传输后端，可通过`--transfer_backend hixl`切换为hixl cs后端：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/push_cache_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/push_cache_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```
  若在A5环境执行，需指定`--transfer_backend hixl`；未手动配置local_comm_res时默认走UB链路，也可手动配置local_comm_res使用RDMA链路，如：
    ```
    # Prompt主机:
    python llm_datadist/push_cache_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1 --transfer_backend hixl
    # Decoder主机:
    python llm_datadist/push_cache_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0 --transfer_backend hixl
    ```
- 执行switch_role样例程序：此样例程序使用单侧建链方式，首先torch自行申请内存并注册blocks, decoder发起建链并pull blocks, 然后两侧切换角色, 并prompt发起建链， decoder进行push_blocks
  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/switch_role_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/switch_role_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```
- 执行pull_blocks_xpyd样例程序：此样例程序支持xPyD测试场景，使用单侧建链方式，每个进程申请内存并注册blocks, 每个decoder和所有的prompt发起建链, 并pull blocks到本地，local_ip_port指定本地host ip和端口
  - 说明：
    此用例可在任意个主机上执行，无论prompt侧拉起多少个进程，decoder侧结尾的prompt_ip_list由prompt侧的所有\${local_ip:port}组成
  ```
  #prompt侧：
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port ${local_ip_0:port_0}
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id 1 --role p --local_ip_port ${local_ip_1:port_1}
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id n --role p --local_ip_port ${local_ip_n:port_n}
  #decoder侧：
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id n + 1 --role d --local_ip_port ${local_ip_n+1:port_n+1} --remote_ip_port ${prompt_ip_list}
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id n + 2 --role d --local_ip_port ${local_ip_n+2:port_n+2} --remote_ip_port ${prompt_ip_list}
  ```

  其中 \${prompt_ip_list}由 **所有prompt侧的\${local_ip:port}** 组成，之间用 **;** 连接分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_ip_port为本地host的ip和端口，remote_ip_port为对端host的ip和端口：
  ```
  # 任意个Prompt主机:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port 10.10.10.0:26000
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id 1 --role p --local_ip_port 10.10.10.0:26001
  # 任意个Decoder主机:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id 2 --role d --local_ip_port 10.10.10.0:26002 --remote_ip_port '10.10.10.0:26000;10.10.10.0:26001'
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/pull_blocks_xpyd_sample.py --device_id 3 --role d --local_ip_port 10.10.10.0:26003 --remote_ip_port '10.10.10.0:26000;10.10.10.0:26001'
  ```
- 执行transfer_cache_async_sample样例程序：此样例程序使用单侧建链方式，申请内存并注册cache，prompt侧发起建链并异步分层传输cache。

  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip。默认走通信域传输后端，可通过`--transfer_backend hixl`切换为hixl cs后端：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```
  若在A5环境执行，需指定`--transfer_backend hixl`；未手动配置local_comm_res时默认走UB链路，也可手动配置local_comm_res使用RDMA链路，如：
    ```
    # Prompt主机:
    python llm_datadist/transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1 --transfer_backend hixl
    # Decoder主机:
    python llm_datadist/transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0 --transfer_backend hixl
    ```
- 执行hixl_transfer_backend_sample样例程序，此样例程序使用hixl作为llm_datadist的传输后端，完成内存注册、建链和传输。样例申请内存并注册blocks, decoder发起建链并push blocks，prompt发起建链并pull blocks。
  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/hixl_transfer_backend_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python llm_datadist/hixl_transfer_backend_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```
- 执行hixl_d2rd_multiproc_sample样例程序，此样例程序使用HIXL接口实现D2D多进程传输。
  分别在Server与Client主机执行样例程序，其中device为要使用的device_id，protocol为传输协议（如roce:device、hccs:device）：
    ```
    # Server:
    python hixl/hixl_d2rd_multiproc_sample.py --role server --device 2 --local-engine 10.10.10.1:16101 --protocol roce:device
    # Client:
    python hixl/hixl_d2rd_multiproc_sample.py --role client --device 0 --local-engine 10.10.10.0:16100 --remote-engine 10.10.10.1:16101 --protocol roce:device
    ```
**注**：**GLOO_SOCKET_IFNAME**为本地网卡名，可通过ifconfig查询；**HCCL_INTRA_ROCE_ENABLE=1**代表使用roce方式进行通信；
