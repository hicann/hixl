## 目录

- [ci_test](#ci_test)
- [目录结构](#目录结构)
- [环境要求](#环境要求)
- [详细用法](#程序编译)

## ci_test

该目录提供了支持编译、测试、安装以及用例执行功能的二级冒烟脚本。

## 目录结构

```
├── ci_test
|   ├── hixl.sh                                         // 二级冒烟脚本
```

## 环境要求

-   操作系统及架构：Euleros x86系统、Euleros aarch64系统
-   编译器：g++
-   芯片：Atlas A3 训练/推理系列产品、Atlas 800I A2 推理产品/A200I A2 Box 异构组件
-   已完成昇腾AI软件栈在运行环境上的部署

## 详细用法
- 进入到scripts/ci_test目录：
```
$ cd ./scripts/ci_test
```
- 运行 `bash hixl.sh --help` 即可查看到所有命令集合。
```
Usage:
  sh hixl.sh [<device_id_1> <device_id_2>]            Build, test, install hixl and run samples with default or specified devices
  sh hixl.sh hixl_build [--Build options]             Build code
  sh hixl.sh hixl_test [--Test options]               Test
  sh hixl.sh hixl_install                             Install hixl
  sh hixl.sh hixl_samples [<device_id_1> <device_id_2>]
                                                      Run samples
  sh hixl.sh hixl_build [--Build options]  hixl_samples [<device_id_1> <device_id_2>]
                                                      Run multiple stages together
  sh hixl.sh [-h | --help]                            Print usage

Build options:
    -h, --help        Print usage
    -v, --verbose     Display build command
    -j<N>             Set the number of threads used for building HIXL, default is 8
    --build_type=<Release|Debug> |--build-type=<Release|Debug>
                      Set build type, default Release
    --cann_3rd_lib_path=<PATH> | --cann_3rd_lib_path=<PATH>
                      Set ascend third_party package install path, default ./third_party
    --output_path=<PATH> | --output-path=<PATH>
                      Set output path, default ./build_out
    --pkg             Build run package, reserved parameter
    --examples        Build with examples and benchmarks, default is OFF
    --asan            Enable AddressSanitizer, default is OFF
    --cov             Enable Coverage, default is OFF

Test options:
    -h, --help     Print usage
    -t, --test     Build all test
        =cpp               Build all cpp test
        =py                Build all py test
    -c, --cov      Build test with coverage tag
                   Please ensure that the environment has correctly installed lcov, gcov, and genhtml.
                   and the version matched gcc/g++, default is OFF.
    -v, --verbose  Display build command
    -j<N>          Set the number of threads used for building Parser, default 8
        --cann_3rd_lib_path=<PATH> | --cann-3rd-lib-path=<PATH>
                   Set ascend third_party package install path, default ./third_party
    --asan         Enable AddressSanitizer, default is OFF. when cov is setted, asan is setted too.
```
- 参数详细解释

  - `hixl_build`：执行编译
  - `hixl_test`：执行UTST
  - `hixl_install`：安装编译出来的hixl
  - `hixl_samples`：执行hixl仓中的samples
  - `[--Build options]`：build.sh脚本的参数
  - `[--Test options]`：run_test.sh脚本的参数
  - `[<device_id_1> <device_id_2>]`：执行samples使用的两个device_id

- 用户可以直接执行所有功能：
  ```
  # 使用默认的device_id
  bash hixl.sh

  # 使用指定的device_id
  bash hixl.sh 0 2
  ```
- 用户可以根据自己的需要将`hixl_build`、`hixl_test`、`hixl_install` 和 `hixl_samples` 进行组合，例如：
  ```
  # 仅编译
  bash hixl.sh hixl_build --examples --output-path=./build_out

  # 编译＋UTST
  bash hixl.sh hixl_build --examples hixl_test -t=cpp

  # 编译 + 执行samples
  bash hixl.sh hixl_build --examples hixl_samples 0 2
  ```