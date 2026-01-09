## 目录

- [样例介绍](#样例介绍)
- [目录结构](#目录结构)
- [环境要求](#环境要求)
- [程序编译](#程序编译)
- [样例运行](#样例运行)

## 样例介绍

功能：通过LLM-DataDist接口实现分离部署场景下KvCache管理功能。

## 目录结构

```
├── cpp
|   ├── prompt_pull_cache_and_blocks.cpp               // pull cache和pull blocks的prompt侧实现
|   ├── decoder_pull_cache_and_blocks.cpp              // pull cache和pull blocks的decoder侧实现
|   ├── prompt_push_cache_and_blocks.cpp               // push cache和push blocks的prompt侧实现
|   ├── decoder_push_cache_and_blocks.cpp              // push cache和push blocks的decoder侧实现
|   ├── prompt_switch_roles.cpp                        // switch_roles的prompt侧实现
|   ├── decoder_switch_roles.cpp                       // switch_roles的decoder侧实现
|   ├── client_server_h2d.cpp                          // HIXL的client-server模式, h2d场景样例
|   ├── server_server_d2d.cpp                          // HIXL的server-server模式, d2d场景样例
|   ├── CMakeLists.txt                                 // 编译脚本
```

## 环境要求

-   操作系统及架构：Euleros x86系统、Euleros aarch64系统
-   编译器：g++
-   芯片：Atlas A3 训练/推理系列产品、Atlas 800I A2 推理产品/A200I A2 Box 异构组件
-   已完成昇腾AI软件栈在运行环境上的部署  

以下所有用例的执行命令以A2环境为例演示，在执行前请先确认**两个device之间互通**，例如A3环境一卡双带之间不互通，0号和1号device不通，2号和3号device不通，以此类推，需要在执行时将device_id进行替换。可以用hccn_tool按照以下步骤确认两个设备之间的连通性，假设要测试a和b两台设备间的连通性：  

1. 用hccn_tool查询b的device_ip
```
hccn_tool -i ${device_id_b} -ip -g  
```
其中\${device_id_b}为b设备的device_id。

2. 用hccn_tool检测a到b的连通性
```
hccn_tool -i ${device_id_a} -ping -g address ${ip_address_b}
```
其中\${device_id_a}为a设备的device_id，\${ip_address_b}为第一步中查出的b设备的device_ip。  

3. 将ab互换重复执行步骤1和2，检测b到a的连通性    

假如返回结果出现类似于recv time out seq=0的字样，说明两个设备之间不连通，请更换device_id，选择连通的一对执行用例。

4. 执行用例前检查设备之间TLS设置是否一致：
```shell
# 检查设备的TLS状态
for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch

# TLS使能的设备和TLS不使能的设备无法建链，建议统一保持TLS关闭
for i in {0..7}; do hccn_tool -i $i -tls -s enable 0; done
```
## 程序编译


1. 参考[构建](../../docs/build.md)里的**编译执行**章节，利用build.sh的--examples参数进行编译。

2. 编译结束后，在**build/examples/cpp**目录下生成多个可执行文件。

## 样例运行

### 1. prompt/decoder样例
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

    (3) 执行switch_roles

    此样例介绍了prompt和decoder进行角色切换，并结合pull以及push使用流程

    - 执行prompt_switch_roles, 参数为device_id、local_ip和remote_ip, 其中device_id为prompt要使用的device_id, local_ip为prompt所在host的ip, remote_ip为decoder所在host的ip，如:
        ```
        ./prompt_switch_roles 0 10.10.170.1 10.170.10.1
        ```

    - 执行decoder_switch_roles, 参数为device_id、local_ip和remote_ip, 其中device_id为decoder要使用的device_id, local_ip为decoder所在host的ip，remote_ip为prompt所在host的ip，如:
        ```
        ./decoder_switch_roles 2 10.170.10.1 10.170.10.1

### 2. HIXL样例
  - 说明：
    - 所有样例需要成对运行，client侧和server侧执行间隔时间不要过长，client-server用例中设置WAIT_REG_TIME为5s，WAIT_TRANS_TIME为20s，server-server用例中设置WAIT_TIME为5s，用户可根据实际情况自行修改这两个变量的值以保证用例成功运行。
    - 下面所有用例都只能在单机上执行，local_engine和remote_engine的ip部分设为相同，server侧engine为ip:port形式，client侧engine为ip形式。如果需要多机执行，需对用例进行改造。

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

    (1) 执行client_server_h2d, client-server模式，h2d场景

    - 执行client client_server_h2d, 参数为device_id、local engine和remote engine, 其中device_id为client要使用的device_id，如:
        ```
        HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d 0 10.10.10.0 10.10.10.0:16000
        ```

    - 执行server client_server_h2d, 参数为device_id、local engine, 其中device_id为server要使用的device_id, 如:
        ```
        HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d 1 10.10.10.0:16000
        ```

    (2) 执行server_server_d2d, 均作为server，d2d场景

    - 执行server1 server_server_d2d, 参数为device_id、local engine和remote engine, 其中device_id为当前engine要使用的device_id，如:
        ```
        HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d 0 10.10.10.0:16000 10.10.10.0:16001
        ```

    - 执行server2 server_server_d2d, 参数为device_id、local engine和remote engine, 其中device_id为当前engine要使用的device_id, 如:
        ```
        HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d 1 10.10.10.0:16001 10.10.10.0:16000
        ```
    **注**：HCCL_INTRA_ROCE_ENABLE=1表示使用RDMA进行传输
