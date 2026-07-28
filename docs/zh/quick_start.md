# 快速开始

本文介绍如何执行一个HIXL C++样例，包括server/client双进程启动、READ传输与数据校验流程，帮助用户在完成构建后快速验证HIXL在HCCS链路下的基础传输能力。

> **说明**：`hixl_example_quickstart` 为快速开始用例，仅用于验证功能是否可用。样例在失败路径上会直接退出进程，未做内存解注册、断链、引擎销毁等资源清理；正式业务请参考其他样例完善异常处理与资源释放。

## 前提条件

- 已完成CANN环境变量配置。默认安装路径下可执行：

  ```bash
  source /usr/local/Ascend/cann/set_env.sh
  ```

- 已编译样例：

  ```bash
  bash build.sh --examples
  ```

- 已选择两张互通的device。若执行失败，请先参考[样例执行](../../examples/README.md)检查device连通性和TLS配置。
- 当前用例仅支持Atlas A2 训练系列产品/Atlas A2 推理系列产品、Atlas A3 训练系列产品/Atlas A3 推理系列产品。

## 执行样例

进入C++样例可执行文件目录：

```bash
cd build/examples/cpp
```

分别在两个终端执行如下命令。需要先启动server，再启动client。

```bash
# 终端1：server
./hixl_example_quickstart --role=server

# 终端2：client
./hixl_example_quickstart --role=client
```

client终端出现如下日志时，表示READ传输和数据校验成功：

```text
[INFO] TransferSync READ completed
[INFO] Verify success
```

## 验证功能

该用例对应源码为[examples/cpp/hixl_example_quickstart.cpp](../../examples/cpp/hixl_example_quickstart.cpp)，以最短代码演示HIXL核心流程，验证HIXL在`hccs:device`链路下的基础传输能力。该用例仅用于功能验证：成功路径会断链并释放资源，失败路径直接 `exit`，不做资源清理。

- 启动server/client两个独立进程。
- 双端各自初始化Hixl引擎并注册Device内存。
- server先完成本端RegisterMem，再通过socket交换远端buffer地址，保证client拿到的地址已可传输。
- 由client发起READ传输，读取server端数据。
- 在client本地校验读取到的数据，验证Device侧数据传输和数据一致性。
- 成功时传输完成后断链、解注册内存并销毁引擎；失败时直接退出，不做上述清理。

## 默认参数

上述命令未显式指定device和engine地址，将使用样例默认值：

- client默认使用`device 0`，本端engine地址为`127.0.0.1:16000`。
- server默认使用`device 2`，本端engine地址为`127.0.0.1:16001`。

更多参数含义和HIXL样例说明，可参考[examples/cpp HIXL样例](../../examples/cpp/README.md#2-hixl样例)。
