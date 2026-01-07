## 目录

- [Benchmarks](#Benchmarks)
- [目录结构](#目录结构)
- [环境要求](#环境要求)
- [程序编译](#程序编译)
- [Benchmark运行](#Benchmark运行)
- [性能数据](#性能数据)

## Benchmarks

该目录提供了HIXL的benchmark性能用例，支持用户根据需要传输的数据大小对benchmark进行改造以快速进行性能测试和评估。

## 目录结构

```
├── benchmarks
|   ├── common                                         // 公共函数目录
|   ├── benchmark.cpp                                  // HIXL的数据传输benchmark用例
|   ├── CMakeLists.txt                                 // 编译脚本
```

## 环境要求

-   操作系统及架构：Euleros x86系统、Euleros aarch64系统
-   编译器：g++
-   芯片：Atlas A3 训练/推理系列产品、Atlas 800I A2 推理产品/A200I A2 Box 异构组件
-   已完成昇腾AI软件栈在运行环境上的部署

## 程序编译


1. 参考[构建](../docs/build.md)里的**编译执行**章节，利用build.sh的--examples参数进行编译。

2. 编译结束后，在**build/benchmarks**目录下生成可执行文件。

## 执行前准备
执行前请先确认**两个device之间互通**，可以用hccn_tool按照以下步骤确认两个设备之间的连通性，假设要测试a和b两台设备间的连通性：  

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

4. 检查设备之间TLS设置是否一致：
```shell
# 检查设备的TLS状态
for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch

# TLS使能的设备和TLS不使能的设备无法建链，建议统一保持TLS关闭
for i in {0..7}; do hccn_tool -i $i -tls -s enable 0; done
```
**注**：Atlas A3 训练/推理系列产品一卡双带之间不互通，0号和1号device不通，2号和3号device不通，以此类推，需要在执行时将device_id进行替换。

## Benchmark运行

- 说明：
    - 所有benchmark需要成对执行，client侧和server侧启动执行间隔时间不要过长，代码中默认设置kWaitTransTime为20s，用户可根据实际情况自行修改此变量的值以保证用例成功运行。
    - 所有benchmark默认传输数据大小kTransferMemSize为128M，用户可根据需要自行修改。执行成功后会打印类似如下的日志，其中block size表示每次传输的内存块大小；transfer num表示传输次数；time cost表示总的传输耗时；throughput表示传输的吞吐（带宽）。
      ```
      [INFO] Transfer success, block size: 8388608 Bytes, transfer num: 16, time cost: 1044 us, throughput: 119.732 GB/s
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

  - 执行benchmark，client-server模式，可通过参数传递来执行多种传输场景
    - 参数说明
        | **参数名** | **可选/必选** | **描述** |
        |:-----------:|:------------:|:------------:|
        |     device_id      | <div style="width:4cm"> 必选 </div> |  <div style="width: 10cm"> 当前engine要使用的device_id </div>  |
        |     local_engine      | 必选 | 当前engine的ip<br>其中，client侧格式为 ip；server侧格式为 ip:port |
        |     remote_engine      | 必选 | 远端engine的ip<br>其中，client侧格式为 ip；server侧格式为 ip:port|
        |     tcp_port      | 必选 | tcp通信端口 |
        |     transfer_mode      | 必选 | 传输的模式<br>取值范围：d2d、h2d、d2h 和 h2h |
        |     transfer_op      | 必选 | 传输的操作<br>取值范围：write 或 read |
        |     use_buffer_pool      | 必选 | 是否开启中转内存池<br>取值范围：true 或 false |

    - 测试HIXL引擎通过HCCS链路进行传输的带宽, 以d2d场景，写操作，不开启中转内存池为例：

        - 执行client benchmark：
            ```
            ./benchmark 0 10.10.10.0 10.10.10.0:16000 20000 d2d write false
            ```

        - 执行server benchmark：
            ```
            ./benchmark 1 10.10.10.0:16000 10.10.10.0 20000 d2d write false
            ```

    - 测试HIXL引擎通过RDMA链路进行传输的带宽, 以d2d场景，写操作，不开启中转内存池为例：

        - 执行client benchmark：
            ```
            HCCL_INTRA_ROCE_ENABLE=1 ./benchmark 0 10.10.10.0 10.10.10.0:16000 20000 d2d write false
            ```

        - 执行server benchmark：
            ```
            HCCL_INTRA_ROCE_ENABLE=1 ./benchmark 1 10.10.10.0:16000 10.10.10.0 20000 d2d write false
            ```
  **注**：HCCL_INTRA_ROCE_ENABLE=1表示使用RDMA进行传输
- 约束说明

    - Atlas 800I A2 推理产品/A200I A2 Box 异构组件，该场景下Server内采用HCCS传输协议时，仅支持d2d。
    - Atlas A3 训练/推理系列产品，该场景下采用HCCS传输协议时，不支持Host内存作为远端Cache。

## 性能数据

HIXL在昇腾A2/A3芯片上部分场景传输数据的实测性能,可参见[A2性能数据](A2_benchmark_performance.md)/[A3性能数据](A3_benchmark_performance.md)。

