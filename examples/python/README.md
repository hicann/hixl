## 目录

- [目录](#目录)
- [样例介绍](#样例介绍)
- [目录结构](#目录结构)
- [环境准备](#环境准备)
- [样例运行](#样例运行)
  - [执行前准备](#执行前准备)
  - [执行](#执行)

## 样例介绍

功能：通过LLM-DataDist接口实现分离部署场景下KvCache的管理功能。

## 目录结构

```
├── python
|   ├── pull_blocks_sample.py
|   ├── pull_blocks_xpyd_sample.py
|   ├── pull_cache_sample.py
|   ├── pull_from_cache_to_blocks.py
|   ├── push_blocks_sample.py
|   ├── push_cache_sample.py
|   ├── switch_role_sample.py
|   ├── transfer_cache_async_sample.py
|   ├── hixl_tranfer_backend_sample.py
```

## 环境准备

安装根目录下requirements.txt依赖（如果前面已经安装则跳过此步）：

```bash
pip3 install -r requirements.txt
```
根据实际环境，安装对应的**torch**与**torch_npu**包(建议使用大于等于2.1.0的版本)， [获取方法](https://gitcode.com/Ascend/pytorch)。

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

- 下面个别用例支持在A5环境使用RDMA链路执行，并且需要在双机上执行，会在对应用例中进行特别说明。在执行前需要手动配置local_comm_res，配置格式参考：[通信设备配置](https://gitcode.com/cann/hixl/issues/37)。可通过以下操作获取 host 网卡的 ip 信息：
  ```shell
  # 查询RoCE设备和网口的对应关系，查看状态为Up的网口名
  ibdev2netdev

  # 根据网口名找出对应的ip信息
  ifconfig
  ```

## 样例运行
- 执行pull cache样例程序，此样例程序展示了配置内存池场景下，使用allocate_cache，双向建链，并从远端pull_cache
  - 说明：
    本示例必须使用双机，参考[执行前准备](#执行前准备)

  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，cluster_id为集群ID且在所有参与建链的范围内需要确保唯一：
    ```
    # Prompt主机:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_cache_sample.py --device_id 0 --cluster_id 1
    # Decoder主机:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_cache_sample.py --device_id 0 --cluster_id 2
    ```
- 执行pull blocks样例程序，此样例程序使用torch自行申请内存，双向建链，并从远端pull_cache
  - 说明：
    本示例必须使用双机，参考[执行前准备](#执行前准备)

  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，cluster_id为集群ID且在所有参与建链的范围内需要确保唯一：
    ```
    # Prompt主机:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_sample.py --device_id 0 --cluster_id 1
    # Decoder主机:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_sample.py --device_id 0 --cluster_id 2
    ```
- 执行pull_from_cache_to_blocks样例程序：
  - 说明：
    本示例必须使用双机，参考[执行前准备](#执行前准备)

  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，cluster_id为集群ID且在所有参与建链的范围内需要确保唯一：
    ```
    # Prompt主机:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_from_cache_to_blocks.py --device_id 0 --cluster_id 1
    # Decoder主机:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_from_cache_to_blocks.py --device_id 0 --cluster_id 2
    ```
- 执行push_blocks样例程序，此样例程序使用单侧建链方式，申请内存并注册blocks,  decoder发起建链并push blocks
  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
  若在A5环境执行还需要增加参数local_comm_res，如：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1 --local_comm_res ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}’
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0 --local_comm_res ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}’
    ```
- 执行push_cache样例程序：此样例程序使用单侧建链方式，申请内存并注册cache,  decoder发起建链并push cache
  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
  若在A5环境执行还需要增加参数local_comm_res，如：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1 --local_comm_res ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}’
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0 --local_comm_res ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}’
    ```
- 执行switch_role样例程序：此样例程序使用单侧建链方式，首先torch自行申请内存并注册blocks, decoder发起建链并pull blocks, 然后两侧切换角色, 并prompt发起建链， decoder进行push_blocks
  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python switch_role_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python switch_role_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
- 执行pull_blocks_xpyd样例程序：此样例程序支持xPyD测试场景，使用单侧建链方式，每个进程申请内存并注册blocks, 每个decoder和所有的prompt发起建链, 并pull blocks到本地，local_ip_port指定本地host ip和端口
  - 说明：
    此用例可在任意个主机上执行，无论prompt侧拉起多少个进程，decoder侧结尾的prompt_ip_list由prompt侧的所有\${local_ip:port}组成
  ```  
  #prompt侧：   
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port ${local_ip_0:port_0}  
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 1 --role p --local_ip_port ${local_ip_1:port_1}
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n --role p --local_ip_port ${local_ip_n:port_n}
  #decoder侧：  
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n + 1 --role d --local_ip_port ${local_ip_n+1:port_n+1} --remote_ip_port ${prompt_ip_list}  
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n + 2 --role d --local_ip_port ${local_ip_n+2:port_n+2} --remote_ip_port ${prompt_ip_list}  
  ```

  其中 \${prompt_ip_list}由 **所有prompt侧的\${local_ip:port}** 组成，之间用 **;** 连接分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_ip_port为本地host的ip和端口，remote_ip_port为对端host的ip和端口：
  ```
  # 任意个Prompt主机:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port 10.170.10.0:26000
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 1 --role p --local_ip_port 10.170.10.0:26001
  # 任意个Decoder主机:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 2 --role d --local_ip_port 10.170.10.0:26002 --remote_ip_port '10.170.10.0:26000;10.170.10.0:26001'
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 3 --role d --local_ip_port 10.170.10.0:26003 --remote_ip_port '10.170.10.0:26000;10.170.10.0:26001'
  ```
- 执行transfer_cache_async_sample样例程序：此样例程序使用单侧建链方式，申请内存并注册cache，prompt侧发起建链并异步分层传输cache。

  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
  若在A5环境执行还需要增加参数local_comm_res，如：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1 --local_comm_res ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}’
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0 --local_comm_res ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}’
    ```
- 执行hixl_tranfer_backend_sample样例程序，此样例程序使用hixl作为llm_datadist的传输后端，完成内存注册、建链和传输。样例申请内存并注册blocks, decoder发起建链并push blocks，prompt发起建链并pull blocks。
  分别在Prompt主机与Decoder主机，执行样例程序，其中device_id为要使用的device_id，role为集群角色，local_host_ip为本地host的ip，remote_host_ip为对端host的ip：
    ```
    # Prompt主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python hixl_tranfer_backend_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder主机:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python hixl_tranfer_backend_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
**注**：**GLOO_SOCKET_IFNAME**为本地网卡名，可通过ifconfig查询；**HCCL_INTRA_ROCE_ENABLE=1**代表使用roce方式进行通信；