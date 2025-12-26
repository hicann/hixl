## HIXL在昇腾A2芯片上部分场景实测性能数据

HIXL在昇腾A2芯片上有如下约束条件：

- Atlas 800I A2 推理产品/A200I A2 Box 异构组件，该场景下Server内采用HCCS传输协议时，仅支持D2D。

所以在单机场景下，未给出采用HCCS传输协议时的D2H/H2D/H2H的实测性能数据。

### 单机场景

- WRITE:

| **传输内存块大小** | **HCCS D2D** | **HCCS D2D BufferPool** | **RDMA D2D** | **RDMA D2D BufferPool** |
|:-----------:|:------------:|:------------:|:------------:|:------------:|
|     1M      | 19.058 GB/s | 19.000 GB/s  | 22.531 GB/s  | 22.466 GB/s  |
|     2M      | 19.234 GB/s | 19.216 GB/s  | 22.596 GB/s  | 22.531 GB/s  |
|     4M      | 19.287 GB/s | 19.329 GB/s  | 22.608 GB/s  | 22.563 GB/s  |
|     8M      | 19.371 GB/s | 19.398 GB/s  | 22.604 GB/s  | 22.579 GB/s  |
| **传输内存块大小** | **HCCS H2D** | **HCCS H2D BufferPool** | **RDMA H2D** | **RDMA H2D BufferPool** |
|     1M      |  —— | ——  | 17.234 GB/s  | 6.150 GB/s  |
|     2M      |  —— | ——  | 17.241 GB/s  | 7.431 GB/s  |
|     4M      |  —— | ——  | 17.249 GB/s  | 8.303 GB/s  |
|     8M      |  —— | ——  | 17.249 GB/s  | 4.586 GB/s  |
| **传输内存块大小** | **HCCS D2H** | **HCCS D2H BufferPool** | **RDMA D2H** | **RDMA D2H BufferPool** |
|     1M      |  —— | ——  | 19.688 GB/s  | 9.021 GB/s  |
|     2M      |  —— | ——  | 19.710 GB/s  | 9.294 GB/s  |
|     4M      |  —— | ——  | 19.713 GB/s  | 10.330 GB/s  |
|     8M      |  —— | ——  | 19.719 GB/s  | 8.655 GB/s  |
| **传输内存块大小** | **HCCS H2H** | **HCCS H2H BufferPool** | **RDMA H2H** | **RDMA H2H BufferPool** |
|     1M      |  —— | ——  |  16.583 GB/s  | 8.776 GB/s  |
|     2M      |  —— | ——  |  17.246 GB/s  | 9.845 GB/s  |
|     4M      |  —— | ——  |  17.260 GB/s  | 9.114 GB/s  |
|     8M      |  —— | ——  |  17.246 GB/s  | 8.541 GB/s  |

- READ:

| **传输内存块大小** | **HCCS D2D** | **HCCS D2D BufferPool** | **RDMA D2D** | **RDMA D2D BufferPool** |
|:-----------:|:------------:|:------------:|:------------:|:------------:|
|     1M      | 25.245 GB/s | 25.085 GB/s  | 22.535 GB/s  | 22.426 GB/s  |
|     2M      | 25.521 GB/s | 25.469 GB/s  | 22.539 GB/s  | 22.494 GB/s  |
|     4M      | 25.631 GB/s | 25.615 GB/s  | 22.543 GB/s  | 22.547 GB/s  |
|     8M      | 25.741 GB/s | 25.725 GB/s  | 22.543 GB/s  | 22.567 GB/s  |
| **传输内存块大小** | **HCCS H2D** | **HCCS H2D BufferPool** | **RDMA H2D** | **RDMA H2D BufferPool** |
|     1M      |  —— | ——  | 19.611 GB/s  | 6.081 GB/s  |
|     2M      |  —— | ——  | 19.602 GB/s  | 7.223 GB/s  |
|     4M      |  —— | ——  | 19.614 GB/s  | 8.224 GB/s  |
|     8M      |  —— | ——  | 19.626 GB/s  | 4.525 GB/s  |
| **传输内存块大小** | **HCCS D2H** | **HCCS D2H BufferPool** | **RDMA D2H** | **RDMA D2H BufferPool** |
|     1M      |  —— | ——  |  17.591 GB/s  | 7.903 GB/s  |
|     2M      |  —— | ——  |  17.700 GB/s  | 8.209 GB/s  |
|     4M      |  —— | ——  |  17.596 GB/s  | 8.164 GB/s  |
|     8M      |  —— | ——  |  17.720 GB/s  | 7.937 GB/s  |
| **传输内存块大小** | **HCCS H2H** | **HCCS H2H BufferPool** | **RDMA H2H** | **RDMA H2H BufferPool** |
|     1M      |  —— | ——  |  17.675 GB/s  | 8.852 GB/s  |
|     2M      |  —— | ——  |  17.547 GB/s  | 10.005 GB/s  |
|     4M      |  —— | ——  |  17.586 GB/s  | 9.119 GB/s  |
|     8M      |  —— | ——  |  17.524 GB/s  | 8.601 GB/s  |

### 双机场景

- WRITE

| **传输内存块大小** | **HCCS D2D** | **HCCS D2D BufferPool** | **RDMA D2D** | **RDMA D2D BufferPool** |
|:-----------:|:------------:|:------------:|:------------:|:------------:|
|     1M      | 22.579 GB/s | 22.466 GB/s  |  22.571 GB/s  | 22.458 GB/s  |
|     2M      | 22.596 GB/s | 22.535 GB/s  |  22.600 GB/s  | 22.535 GB/s  |
|     4M      | 22.612 GB/s | 22.567 GB/s  |  22.608 GB/s  | 22.567 GB/s  |
|     8M      | 22.608 GB/s | 22.596 GB/s  |  22.604 GB/s  | 22.592 GB/s  |
| **传输内存块大小** | **HCCS H2D** | **HCCS H2D BufferPool** | **RDMA H2D** | **RDMA H2D BufferPool** |
|     1M      |  17.426 GB/s | 15.807 GB/s  | 17.270 GB/s  | 6.131 GB/s  |
|     2M      |  17.443 GB/s | 16.200 GB/s  | 17.282 GB/s  | 7.951 GB/s  |
|     4M      |  17.451 GB/s | 16.316 GB/s  | 18.287 GB/s  | 8.290 GB/s  |
|     8M      |  17.439 GB/s | 15.451 GB/s  | 17.277 GB/s  | 4.419 GB/s  |
| **传输内存块大小** | **HCCS D2H** | **HCCS D2H BufferPool** | **RDMA D2H** | **RDMA D2H BufferPool** |
|     1M      |  19.676 GB/s | 16.718 GB/s  |  19.666 GB/s  | 9.239 GB/s  |
|     2M      |  19.701 GB/s | 16.680 GB/s  |  19.682 GB/s  | 10.357 GB/s  |
|     4M      |  19.620 GB/s | 16.454 GB/s  |  19.688 GB/s  | 9.523 GB/s  |
|     8M      |  19.713 GB/s | 16.200 GB/s  |  19.688 GB/s  | 9.073 GB/s  |
| **传输内存块大小** | **HCCS H2H** | **HCCS H2H BufferPool** | **RDMA H2H** | **RDMA H2H BufferPool** |
|     1M      |  17.344 GB/s | 16.534 GB/s  |  18.902 GB/s  | 6.931 GB/s  |
|     2M      |  17.327 GB/s | 16.526 GB/s  |  18.894 GB/s  | 7.508 GB/s  |
|     4M      |  17.347 GB/s | 16.583 GB/s  |  18.925 GB/s  | 7.820 GB/s  |
|     8M      |  17.359 GB/s | 16.251 GB/s  |  18.929 GB/s  | 7.818 GB/s  |

- READ

| **传输内存块大小** | **HCCS D2D** | **HCCS D2D BufferPool** | **RDMA D2D** | **RDMA D2D BufferPool** |
|:-----------:|:------------:|:------------:|:------------:|:------------:|
|     1M      |  22.518 GB/s | 22.377 GB/s  |  22.454 GB/s  | 22.393 GB/s  |
|     2M      |  22.531 GB/s | 22.545 GB/s  |  22.482 GB/s  | 22.458 GB/s  |
|     4M      |  22.543 GB/s | 22.486 GB/s  |  22.492 GB/s  | 22.490 GB/s  |
|     8M      |  22.547 GB/s | 22.518 GB/s  |  22.490 GB/s  | 22.510 GB/s  |
| **传输内存块大小** | **HCCS H2D** | **HCCS H2D BufferPool** | **RDMA H2D** | **RDMA H2D BufferPool** |
|     1M      |  19.543 GB/s | 16.144 GB/s  | 22.490 GB/s  | 6.075 GB/s  |
|     2M      |  19.556 GB/s | 16.474 GB/s  | 22.523 GB/s  | 7.274 GB/s  |
|     4M      |  19.574 GB/s | 16.523 GB/s  | 22.518 GB/s  | 8.249 GB/s  |
|     8M      |  19.574 GB/s | 15.528 GB/s  | 22.531 GB/s  | 4.530 GB/s  |
| **传输内存块大小** | **HCCS D2H** | **HCCS D2H BufferPool** | **RDMA D2H** | **RDMA D2H BufferPool** |
|     1M      |  17.875 GB/s | 17.878 GB/s  | 17.630 GB/s  | 10.288 GB/s  |
|     2M      |  17.751 GB/s | 17.939 GB/s  | 17.544 GB/s  | 9.607 GB/s  |
|     4M      |  17.880 GB/s | 17.939 GB/s  | 17.650 GB/s  | 10.568 GB/s  |
|     8M      |  17.741 GB/s | 17.819 GB/s  | 17.524 GB/s  | 9.066 GB/s  |
| **传输内存块大小** | **HCCS H2H** | **HCCS H2H BufferPool** | **RDMA H2H** | **RDMA H2H BufferPool** |
|     1M      |  17.855 GB/s | 17.187 GB/s  | 17.541 GB/s  | 8.918 GB/s  |
|     2M      |  17.743 GB/s | 17.053 GB/s  | 17.565 GB/s  | 9.921 GB/s  |
|     4M      |  17.855 GB/s | 16.995 GB/s  | 17.547 GB/s  | 9.225 GB/s  |
|     8M      |  17.761 GB/s | 17.119 GB/s  | 17.664 GB/s  | 8.678 GB/s  |

