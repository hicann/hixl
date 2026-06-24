## 目录

- [目录](#目录)
- [样例介绍](#样例介绍)
- [目录结构](#目录结构)
- [样例配置说明](#样例配置说明)
- [程序编译](#程序编译)
- [样例运行](#样例运行)
  - [1. LLM-DataDist样例](#1-llm-datadist样例)
  - [2. HIXL样例](#2-hixl样例)

## 样例介绍

功能：通过LLM-DataDist、HIXL接口实现分离部署场景下KvCache传输功能。

## 目录结构

```
├── cpp
|   ├── prompt_pull_cache_and_blocks.cpp               // pull cache和pull blocks的prompt侧实现
|   ├── decoder_pull_cache_and_blocks.cpp              // pull cache和pull blocks的decoder侧实现
|   ├── prompt_push_cache_and_blocks.cpp               // push cache和push blocks的prompt侧实现
|   ├── decoder_push_cache_and_blocks.cpp              // push cache和push blocks的decoder侧实现
|   ├── prompt_switch_roles.cpp                        // switch_roles的prompt侧实现
|   ├── decoder_switch_roles.cpp                       // switch_roles的decoder侧实现
|   ├── hixl_example_d2rd.cpp                          // HIXL的D2rD单进程场景样例
|   ├── hixl_example_d2rh.cpp                          // HIXL的D2rH单进程场景样例
|   ├── hixl_example_d2rd_multiproc.cpp                // HIXL的D2rD多进程场景样例
|   ├── fabric_mem_d2d.cpp                             // HIXL的fabric-mem模式下的d2d场景样例
|   ├── CMakeLists.txt                                 // 编译脚本
```

## 样例配置说明

部分用例支持在A5环境使用RDMA链路执行，且需要在双机上执行，会在对应用例中进行特别说明。在执行前需要手动配置local_comm_res，配置格式参考：[通信设备配置](https://gitcode.com/cann/hixl/issues/37)。可通过以下操作获取 host 网卡的 ip 信息：
```shell
# 查询RoCE设备和网口的对应关系，查看状态为Up的网口名
ibdev2netdev

# 根据网口名找出对应的ip信息
ifconfig
```

## 程序编译


1. 参考[构建](../../docs/zh/build.md)里的**编译执行**章节，利用 `bash build.sh --examples` 进行编译。

2. 编译结束后，在**build/examples/cpp**目录下生成多个可执行文件。

## 样例运行

### 1. LLM-DataDist样例
 - 说明：
    - 所有样例需要成对运行，prompt侧和decoder侧执行间隔时间不要过长，样例中decoder侧设置WAIT_PROMPT_TIME为5s，prompt侧设置WAIT_TIME为10s，用户可根据实际情况自行修改这两个变量的值以保证用例成功运行。
    - 下面所有样例是以prompt和decoder运行在相同机器上为前提编写，将local_ip和remote_ip设为相同。

 - 配置环境变量
    - 若运行环境上安装的“Ascend-cann-toolkit”包，环境变量设置如下：

        ```
        source ${HOME}/Ascend/cann/set_env.sh
        ```

        “${HOME}/Ascend”请替换相关软件包的实际安装路径。

    - 若运行环境上安装的“CANN-XXX.run”包，环境变量设置如下：

        ```
        source ${HOME}/Ascend/latest/bin/setenv.bash
        ```

        “${HOME}/Ascend”请替换相关软件包的实际安装路径。

 - 在运行环境执行可执行文件。

    (1) 执行pull_cache_and_blocks

    此样例介绍了decoder向prompt进行pull cache和pull blocks流程，其中link和pull的方向与角色无关，可以根据需求更改

    - 执行prompt_pull_cache_and_blocks, 参数为device_id和local_ip, 其中device_id为prompt要使用的device_id, local_ip为prompt所在host的ip, 如:
        ```
        ./prompt_pull_cache_and_blocks 0 10.10.170.1
        ```

    - 执行decoder_pull_cache_and_blocks, 参数为device_id、local_ip和remote_ip, 其中device_id为decoder要使用的device_id, local_ip为decoder所在host的ip，remote_ip为prompt所在host的ip，如:
        ```
        ./decoder_pull_cache_and_blocks 2 10.170.10.1 10.170.10.1
        ```

    - 若在A5环境执行还需要增加参数local_comm_res，如：
        ```
        # prompt主机
        HCCL_INTRA_ROCE_ENABLE=1 ./prompt_pull_cache_and_blocks 0 10.10.170.0 ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}’

        # decoder主机
        HCCL_INTRA_ROCE_ENABLE=1 ./decoder_pull_cache_and_blocks 0 10.170.10.1 10.170.10.0 ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}’
        ```

    (2) 执行push_cache_and_blocks

    此样例介绍了prompt向decoder进行push cache和push blocks流程，其中link和push的方向与角色无关，可以根据需求更改

    - 执行prompt_push_cache_and_blocks, 参数为device_id, local_ip与remote_ip 其中device_id为prompt要使用的device_id, local_ip为prompt所在host的ip，remote_ip为prompt所在host的ip, 如:
        ```
        ./prompt_push_cache_and_blocks 0 10.10.10.1 10.10.10.1
        ```

    - 执行decoder_push_cache_and_blocks, 参数为device_id与local_ip, 其中device_id为decoder要使用的device_id, local_ip为decoder所在host的ip, 如:
        ```
        ./decoder_push_cache_and_blocks 4 10.10.10.1
        ```

    - 若在A5环境执行还需要增加参数local_comm_res，如：
        ```
        # prompt主机
        HCCL_INTRA_ROCE_ENABLE=1 ./prompt_push_cache_and_blocks 0 10.10.10.0 10.10.10.1 ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}’

        # decoder主机
        HCCL_INTRA_ROCE_ENABLE=1 ./decoder_push_cache_and_blocks 0 10.10.10.1 ‘{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}’
        ```

    (3) 执行switch_roles

    此样例介绍了prompt和decoder进行角色切换，并结合pull以及push使用流程

    - 执行prompt_switch_roles, 参数为device_id、local_ip和remote_ip, 其中device_id为prompt要使用的device_id, local_ip为prompt所在host的ip, remote_ip为decoder所在host的ip，如:
        ```
        ./prompt_switch_roles 0 10.10.170.1 10.170.10.1
        ```

    - 执行decoder_switch_roles, 参数为device_id、local_ip和remote_ip, 其中device_id为decoder要使用的device_id, local_ip为decoder所在host的ip，remote_ip为prompt所在host的ip，如:
        ```
        ./decoder_switch_roles 2 10.170.10.1 10.170.10.1
        ```

### 2. HIXL样例
  - 说明：
    - 单进程用例（hixl_example_d2rd、hixl_example_d2rh）在一个进程内启动两个engine，无需分开终端，在大于等于CANN-9.1.0版本支持。
    - 多进程用例（hixl_example_d2rd_multiproc）需要分别在两个终端启动server和client，server先启动，无CANN版本要求。
    - fabric_mem_d2d需要成对运行，两个终端分别启动。

  - HIXL样例进程参数说明

    | 参数 | 适用样例 | 必选/可选 | 默认值 | 说明 |
    | --- | --- | --- | --- | --- |
    | `--protocol=<type>[,...]` | hixl_example_d2rd、hixl_example_d2rh、hixl_example_d2rd_multiproc | 必选 | - | 通信协议，支持逗号分隔多协议。hixl_example_d2rd支持`roce:device`、`uboe:device`、`ub_ctp:device`、`ub_tp:device`；hixl_example_d2rh支持`roce:device`、`uboe:device`、`ub_ctp:device`、`ub_tp:device`、`ub_ctp:host`、`ub_tp:host`；hixl_example_d2rd_multiproc支持`hccs:device`、`roce:device`、`uboe:device`、`ub_ctp:device`、`ub_tp:device`。协议硬件依赖如下：`hccs:device`、`roce:device`仅支持Atlas A2 训练系列产品/Atlas A2 推理系列产品、Atlas A3 训练系列产品/Atlas A3 推理系列产品；`uboe:device`、`ub_ctp:device`、`ub_tp:device`、`ub_ctp:host`、`ub_tp:host`仅支持Ascend 950PR/Ascend 950DT。协议定义详见[HIXL接口](../../docs/zh/api/cpp/HIXL-interface.md)中`comm_resource_config.protocol_desc`的option说明。 |
    | `--device=<id>`或`--device=id1,id2` | hixl_example_d2rd、hixl_example_d2rh、hixl_example_d2rd_multiproc | 可选 | hixl_example_d2rd、hixl_example_d2rh默认0,2；hixl_example_d2rd_multiproc默认client=0、server=2 | hixl_example_d2rd、hixl_example_d2rh使用`--device=id1,id2`指定两个engine分别绑定的device；hixl_example_d2rd_multiproc使用`--device=<id>`指定当前进程绑定的device。 |
    | <code>--version=0&#124;1</code> | hixl_example_d2rd、hixl_example_d2rh、hixl_example_d2rd_multiproc | 可选 | 1 | 配置为0时，表示使用HCCL集合通信通信域方式构筑的单边通信能力，仅支持Atlas A2 训练系列产品/Atlas A2 推理系列产品、Atlas A3 训练系列产品/Atlas A3 推理系列产品；配置为1时，表示HIXL调用HIXL CS接口实现的单边通信能力，解耦通信域，推荐使用，支持Atlas A2 训练系列产品/Atlas A2 推理系列产品、Atlas A3 训练系列产品/Atlas A3 推理系列产品、Ascend 950PR/Ascend 950DT。 |
    | <code>--role=client&#124;server</code> | hixl_example_d2rd_multiproc | 必选 | - | 指定当前进程为client或server。 |
    | `--local-engine=<ip:port>` | hixl_example_d2rd_multiproc | 可选 | client=127.0.0.1:16000、server=127.0.0.1:16001 | 指定当前进程的engine地址。 |
    | `--remote-engine=<ip:port>` | hixl_example_d2rd_multiproc | 可选 | client=127.0.0.1:16001、server=127.0.0.1:16000 | 指定对端进程的engine地址。 |

  - 配置环境变量
    - 若运行环境上安装的”Ascend-cann-toolkit”包，环境变量设置如下：

        ```
        source ${HOME}/Ascend/cann/set_env.sh
        ```

        “${HOME}/Ascend”请替换相关软件包的实际安装路径。

    - 若运行环境上安装的”CANN-XXX.run”包，环境变量设置如下：

        ```
        source ${HOME}/Ascend/latest/bin/setenv.bash
        ```

        “${HOME}/Ascend”请替换相关软件包的实际安装路径。

  - 在运行环境执行可执行文件。

    (1) 执行hixl_example_d2rd, D2RD单进程场景

      - 说明：
        - 单进程一个线程内启动两个engine，分别绑定不同device，由engine A发起WRITE传输到engine B的device buffer。

      - 运行示例：
          ```
          # 使用roce:device协议
          ./hixl_example_d2rd --protocol=roce:device

          # 指定device
          ./hixl_example_d2rd --protocol=roce:device --device=0,2

          # 使用version 0模式（仅支持roce:device）
          ./hixl_example_d2rd --protocol=roce:device --version=0
          ```

    (2) 执行hixl_example_d2rh, D2RH单进程场景

      - 说明：
        - 单进程一个线程内启动两个engine，分别绑定不同device，双方各自发起WRITE传输到对方的host buffer。

      - 运行示例：
          ```
          # 使用roce:device协议
          ./hixl_example_d2rh --protocol=roce:device

          # 使用host侧协议
          ./hixl_example_d2rh --protocol=ub_ctp:host,ub_ctp:device

          # 使用version 0模式
          ./hixl_example_d2rh --protocol=roce:device --version=0
          ```

    (3) 执行hixl_example_d2rd_multiproc, d2rd多进程场景

      - 说明：
        - 两个独立进程分别启动engine，通过socket交换buffer地址后，由client发起READ传输并本地校验。

      - 运行示例：
          ```
          # 使用roce协议
          ./hixl_example_d2rd_multiproc --role=server --protocol=roce:device
          ./hixl_example_d2rd_multiproc --role=client --protocol=roce:device

          # 使用hccs协议
          ./hixl_example_d2rd_multiproc --role=server --protocol=hccs:device
          ./hixl_example_d2rd_multiproc --role=client --protocol=hccs:device

          # 使用version 0模式
          ./hixl_example_d2rd_multiproc --role=server --protocol=roce:device --version=0
          ./hixl_example_d2rd_multiproc --role=client --protocol=roce:device --version=0
          ```

    (4) 执行fabric_mem_d2d, fabric mem模式下，d2d场景

    **注意**：要使用fabric mem模式，HDK需升级至26.0以上版本

      - 执行server1 fabric_mem_d2d, 参数为device_id、local engine和remote engine, 其中device_id为当前engine要使用的device_id，如:
          ```
          ./fabric_mem_d2d 0 127.0.0.1:16000 127.0.0.1:16001
          ```

      - 执行server2 fabric_mem_d2d, 参数为device_id、local engine和remote engine, 其中device_id为当前engine要使用的device_id, 如:
          ```
          ./fabric_mem_d2d 1 127.0.0.1:16001 127.0.0.1:16000
          ```
