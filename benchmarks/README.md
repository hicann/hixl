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
|   ├── benchmark_d2d_throughput.cpp                   // HIXL的d2d数据传输benchmark用例
|   ├── CMakeLists.txt                                 // 编译脚本
```

## 环境要求

-   操作系统及架构：Euleros x86系统、Euleros aarch64系统
-   编译器：g++
-   芯片：Atlas A3 训练/推理系列产品、Atlas 800I A2 推理产品/A200I A2 Box 异构组件
-   已完成昇腾AI软件栈在运行环境上的部署

## 程序编译


1. 参考[构建](../docs/build.md)里的**编译执行**章节，利用build.sh进行编译。

2. 编译结束后，在**build/benchmarks**目录下生成多个可执行文件。

## Benchmark运行

- 说明：
    - 所有benchmark需要成对执行，client侧和server侧启动执行间隔时间不要过长，代码中默认设置kWaitRegTime为5s，kWaitTransTime为20s，用户可根据实际情况自行修改这两个变量的值以保证用例成功运行。
    - 所有benchmark默认传输数据大小kTransferMemSize为128M，用户可根据需要自行修改。执行成功后会打印类似如下的日志，其中block size表示每次传输的内存块大小；transfer num表示传输次数；time cost表示总的传输耗时；throughput表示传输的吞吐（带宽）。
      ```
      [INFO] Transfer success, block size: 8388608 Bytes, transfer num: 16, time cost: 1044 us, throughput: 119.732 GB/s
      ```

- 配置环境变量
    - 若运行环境上安装的“Ascend-cann-toolkit”包，环境变量设置如下：

        ```
        source ${HOME}/Ascend/set_env.sh
        ```

      “$HOME/Ascend”请替换相关软件包的实际安装路径。

    - 若运行环境上安装的“CANN-XXX.run”包，环境变量设置如下：

        ```
        source ${HOME}/Ascend/latest/bin/setenv.bash
        ```

      “$HOME/Ascend”请替换相关软件包的实际安装路径。

- 在运行环境执行可执行文件。

  (1) 执行benchmark_d2d_throughput, client-server模式，d2d场景

    - 测试HIXL引擎通过HCCS链路进行d2d传输的带宽

        - 执行client benchmark_d2d_throughput, 参数为device_id、local_engine、remote_engine和tcp_port, 其中device_id为client要使用的device_id，local_engine为client的host ip，remote_engine为server的host ip和port，tcp_port为tcp通信端口，如:
            ```
            ./benchmark_d2d_throughput 0 10.10.10.0 10.10.10.0:16000 20000
            ```

        - 执行server benchmark_d2d_throughput, 参数为device_id、local_engine、remote_engine和tcp_port, 其中device_id为server要使用的device_id, local_engine为server的host ip和port，remote_engine为client的host ip，tcp_port为tcp通信端口，如:
            ```
            ./benchmark_d2d_throughput 1 10.10.10.0:16000 10.10.10.0 20000
            ```

    - 测试HIXL引擎通过RDMA链路进行d2d传输的带宽

        - 执行client benchmark_d2d_throughput, 参数含义同上，如:
            ```
            HCCL_INTRA_ROCE_ENABLE=1 ./benchmark_d2d_throughput 0 10.10.10.0 10.10.10.0:16000 20000
            ```

        - 执行server benchmark_d2d_throughput, 参数含义同上, 如:
            ```
            HCCL_INTRA_ROCE_ENABLE=1 ./benchmark_d2d_throughput 1 10.10.10.0:16000 10.10.10.0 20000
            ```
    
  **注**：HCCL_INTRA_ROCE_ENABLE=1表示使用RDMA进行传输

## 性能数据

本节展示了HIXL在昇腾A3芯片上部分场景传输数据的实测性能：

| **传输内存块大小** | **HCCS D2D** | **RDMA D2D** |
|:-----------:|:------------:|:------------:|
|     1M      | 90.777 GB/s  | 21.201 GB/s  |
|     2M      | 105.574 GB/s | 21.739 GB/s  |
|     4M      | 114.469 GB/s | 22.329 GB/s  |
|     8M      | 119.732 GB/s | 22.333 GB/s  |
