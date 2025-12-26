## HIXL在昇腾A3芯片上部分场景实测性能数据

HIXL在昇腾A3芯片上，存在如下约束条件：

- Atlas A3 训练/推理系列产品，该场景下采用HCCS传输协议时，不支持Host内存作为远端Cache（开启中转内存池时无此限制）。

所以未给出D2H/H2H场景下，采用HCCS传输协议时的实测性能数据。

另外，在采用HCCS传输协议时，D2D场景下，由于中转内存池仅在传输内存块 < 256K时用于性能提升[[TransferSync接口约束](../docs/cpp/HIXL接口.md#transfersync)]，测试场景下的传输内存块大小均大于256K，所以未给出相关实测数据。

### 单机场景

- WRITE:

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

- READ:

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

### 双机场景

- WRITE

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

- READ

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

