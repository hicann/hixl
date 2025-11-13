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

本节展示了HIXL在昇腾A3芯片上部分场景传输数据的实测性能：

- 单机场景

    (1) WRITE:

    | **传输内存块大小** | **HCCS D2D** | **HCCS D2D BufferPool** | **RDMA D2D** | **RDMA D2D BufferPool** |
    |:-----------:|:------------:|:------------:|:------------:|:------------:|
    |     1M      | 106.293 GB/s  | ——  | 22.657 GB/s  | ——  |
    |     2M      | 115.101 GB/s | ——  | 22.661 GB/s  | ——  |
    |     4M      | 120.192 GB/s | ——  | 22.670 GB/s  | ——  |
    |     8M      | 123.518 GB/s | ——  | 22.665 GB/s  | ——  |
    | **传输内存块大小** | **HCCS H2D** | **HCCS H2D BufferPool** | **RDMA H2D** | **RDMA H2D BufferPool** |
    |     1M      | 32.748 GB/s  | 35.331 GB/s  | 22.649 GB/s  | 19.072 GB/s  |
    |     2M      | 33.494 GB/s | 35.572 GB/s  | 22.653 GB/s  | 19.084 GB/s  |
    |     4M      | 33.811 GB/s | 36.263 GB/s  | 22.657 GB/s  | 19.078 GB/s  |
    |     8M      | 33.940 GB/s | 33.793 GB/s  | 22.653 GB/s  | 18.521 GB/s  |
    | **传输内存块大小** | **HCCS D2H** | **HCCS D2H BufferPool** | **RDMA D2H** | **RDMA D2H BufferPool** |
    |     1M      | ——  | 30.157 GB/s  | 22.661 GB/s  | 18.651 GB/s  |
    |     2M      | —— | 30.532 GB/s  | 22.657 GB/s  | 18.916 GB/s  |
    |     4M      | —— | 30.414 GB/s  | 22.649 GB/s  | 18.971 GB/s  |
    |     8M      | —— | 29.453 GB/s  | 22.657 GB/s  | 18.469 GB/s  |
    | **传输内存块大小** | **HCCS H2H** | **HCCS H2H BufferPool** | **RDMA H2H** | **RDMA H2H BufferPool** |
    |     1M      | ——  | 28.617 GB/s  | 22.633 GB/s  | 18.431 GB/s  |
    |     2M      | —— | 28.822 GB/s  | 22.649 GB/s  | 18.464 GB/s  |
    |     4M      | —— | 28.775 GB/s  | 22.641 GB/s  | 18.508 GB/s  |
    |     8M      | —— | 28.571 GB/s  | 22.645 GB/s  | 18.345 GB/s  |

    (2) READ:
    | **传输内存块大小** | **HCCS D2D** | **HCCS D2D BufferPool** | **RDMA D2D** | **RDMA D2D BufferPool** |
    |:-----------:|:------------:|:------------:|:------------:|:------------:|
    |     1M      | 124.131 GB/s  | ——  | 22.616 GB/s  | ——  |
    |     2M      | 137.363 GB/s | ——  | 22.633 GB/s  | ——  |
    |     4M      | 144.342 GB/s | ——  | 22.620 GB/s  | ——  |
    |     8M      | 148.104 GB/s | ——  | 22.629 GB/s  | ——  |
    | **传输内存块大小** | **HCCS H2D** | **HCCS H2D BufferPool** | **RDMA H2D** | **RDMA H2D BufferPool** |
    |     1M      | 33.940 GB/s  | 33.684 GB/s  | 22.608 GB/s  | 18.939 GB/s  |
    |     2M      | 34.877 GB/s | 34.771 GB/s  | 22.624 GB/s  | 19.003 GB/s  |
    |     4M      | 35.261 GB/s | 34.751 GB/s  | 22.629 GB/s  | 19.151 GB/s  |
    |     8M      | 35.481 GB/s | 32.826 GB/s  | 22.624 GB/s  | 19.011 GB/s  |
    | **传输内存块大小** | **HCCS D2H** | **HCCS D2H BufferPool** | **RDMA D2H** | **RDMA D2H BufferPool** |
    |     1M      | ——  | 41.681 GB/s  | 22.596 GB/s  | 19.278 GB/s  |
    |     2M      | —— | 42.720 GB/s  | 22.604 GB/s  | 19.263 GB/s  |
    |     4M      | —— | 43.298 GB/s  | 22.596 GB/s  | 19.362 GB/s  |
    |     8M      | —— | 41.848 GB/s  | 22.600 GB/s  | 19.254 GB/s  |
    | **传输内存块大小** | **HCCS H2H** | **HCCS H2H BufferPool** | **RDMA H2H** | **RDMA H2H BufferPool** |
    |     1M      | ——  | 27.685 GB/s  | 19.626 GB/s  | 18.738 GB/s  |
    |     2M      | —— | 28.185 GB/s  | 22.633 GB/s  | 18.710 GB/s  |
    |     4M      | —— | 27.156 GB/s  | 22.629 GB/s  | 18.735 GB/s  |
    |     8M      | —— | 29.426 GB/s  | 22.629 GB/s  | 18.637 GB/s  |

- 双机场景

    (1) WRITE
    | **传输内存块大小** | **HCCS D2D** | **HCCS D2D BufferPool** | **RDMA D2D** | **RDMA D2D BufferPool** |
    |:-----------:|:------------:|:------------:|:------------:|:------------:|
    |     1M      | 91.777 GB/s  | ——  | 17.919 GB/s  | ——  |
    |     2M      | 106.383 GB/s | ——  | 22.649 GB/s  | ——  |
    |     4M      | 115.101 GB/s | ——  | 22.653 GB/s  | ——  |
    |     8M      | 119.732 GB/s | ——  | 22.653 GB/s  | ——  |
    | **传输内存块大小** | **HCCS H2D** | **HCCS H2D BufferPool** | **RDMA H2D** | **RDMA H2D BufferPool** |
    |     1M      | 18.334 GB/s  | 36.433 GB/s  | 22.608 GB/s  | 18.857 GB/s  |
    |     2M      | 18.651 GB/s | 36.765 GB/s  | 22.612 GB/s  | 18.825 GB/s  |
    |     4M      | 18.822 GB/s | 36.808 GB/s  | 22.616 GB/s  | 18.662 GB/s  |
    |     8M      | 18.942 GB/s | 35.982 GB/s  | 22.608 GB/s  | 18.187 GB/s  |
    | **传输内存块大小** | **HCCS D2H** | **HCCS D2H BufferPool** | **RDMA D2H** | **RDMA D2H BufferPool** |
    |     1M      | ——  | 28.842 GB/s  | 22.608 GB/s  | 18.593 GB/s  |
    |     2M      | —— | 28.888 GB/s  | 22.616 GB/s  | 18.499 GB/s  |
    |     4M      | —— | 29.097 GB/s  | 22.620 GB/s  | 18.524 GB/s  |
    |     8M      | —— | 28.261 GB/s  | 22.616 GB/s  | 18.437 GB/s  |
    | **传输内存块大小** | **HCCS H2H** | **HCCS H2H BufferPool** | **RDMA H2H** | **RDMA H2H BufferPool** |
    |     1M      | ——  | 27.741 GB/s  | 22.629 GB/s  | 18.177 GB/s  |
    |     2M      | —— | 27.933 GB/s  | 22.637 GB/s  | 18.195 GB/s  |
    |     4M      | —— | 28.166 GB/s  | 22.637 GB/s  | 18.108 GB/s  |
    |     8M      | —— | 27.376 GB/s  | 22.624 GB/s  | 18.084 GB/s  |

    (2) READ
    | **传输内存块大小** | **HCCS D2D** | **HCCS D2D BufferPool** | **RDMA D2D** | **RDMA D2D BufferPool** |
    |:-----------:|:------------:|:------------:|:------------:|:------------:|
    |     1M      | 104.515 GB/s  | ——  | 22.604 GB/s  | ——  |
    |     2M      | 123.885 GB/s | ——  | 22.612 GB/s  | ——  |
    |     4M      | 135.281 GB/s | ——  | 22.612 GB/s  | ——  |
    |     8M      | 143.184 GB/s | ——  | 22.612 GB/s  | ——  |
    | **传输内存块大小** | **HCCS H2D** | **HCCS H2D BufferPool** | **RDMA H2D** | **RDMA H2D BufferPool** |
    |     1M      | 19.029 GB/s  | 28.223 GB/s  | 22.596 GB/s  | 18.022 GB/s  |
    |     2M      | 19.266 GB/s | 26.864 GB/s  | 22.608 GB/s  | 18.140 GB/s  |
    |     4M      | 19.389 GB/s | 28.782 GB/s  | 22.600 GB/s  | 18.200 GB/s  |
    |     8M      | 19.419 GB/s | 30.428 GB/s  | 22.600 GB/s  | 17.035 GB/s  |
    | **传输内存块大小** | **HCCS D2H** | **HCCS D2H BufferPool** | **RDMA D2H** | **RDMA D2H BufferPool** |
    |     1M      | ——  | 41.974 GB/s  | 22.579 GB/s  | 19.272 GB/s  |
    |     2M      | —— | 42.445 GB/s  | 22.596 GB/s  | 19.332 GB/s  |
    |     4M      | —— | 41.890 GB/s  | 22.604 GB/s  | 19.353 GB/s  |
    |     8M      | —— | 41.377 GB/s  | 22.592 GB/s  | 19.216 GB/s  |
    | **传输内存块大小** | **HCCS H2H** | **HCCS H2H BufferPool** | **RDMA H2H** | **RDMA H2H BufferPool** |
    |     1M      | ——  | 26.795 GB/s  | 17.199 GB/s  | 18.571 GB/s  |
    |     2M      | —— | 25.025 GB/s  | 22.612 GB/s  | 18.783 GB/s  |
    |     4M      | —— | 27.245 GB/s  | 22.616 GB/s  | 18.791 GB/s  |
    |     8M      | —— | 29.274 GB/s  | 22.608 GB/s  | 18.679 GB/s  |

