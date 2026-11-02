## Table of Contents

- [ci_test](#ci_test)
- [Directory Structure](#directory-structure)
- [Environment Requirements](#environment-requirements)
- [Detailed Usage](#detailed-usage)

## ci_test

This directory provides the secondary smoke-test script, which supports building, testing, installing and running samples.

## Directory Structure

```sh
├── ci_test
|   ├── hixl.sh                                         // The secondary smoke-test script
```

## Environment Requirements

- Operating system and architecture: Euleros x86 systems and Euleros aarch64 systems
- Compiler: g++
- Chip: Atlas A3 training/inference series, Atlas 800I A2 inference product / A200I A2 Box heterogeneous components
- The Ascend AI software stack has been deployed on the runtime environment

## Detailed Usage

- Go to the `scripts/ci_test` directory:

```cpp
$ cd ./scripts/ci_test
```

- Run `bash hixl.sh --help` to list all available commands.

```sh
Usage:
  bash hixl.sh [<device_id_1> <device_id_2>]          Build, test, install hixl and run samples with default or specified devices
  bash hixl.sh hixl_build [--Build options]           Build code
  bash hixl.sh hixl_test [--Test options]             Test
  bash hixl.sh hixl_install                           Install hixl
  bash hixl.sh hixl_samples [<device_id_1> <device_id_2>]
                                                      Run samples
  bash hixl.sh hixl_build [--Build options]  hixl_samples [<device_id_1> <device_id_2>]
                                                      Run multiple stages together
  bash hixl.sh [-h | --help]                          Print usage

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
    --asan         Enable AddressSanitizer, default is OFF. when cov is set, asan is set too.
```

- Parameter description

  - `hixl_build`: run the build
  - `hixl_test`: run UTST
  - `hixl_install`: install the hixl package produced by the build
  - `hixl_samples`: run the samples of the hixl repository
  - `[--Build options]`: options accepted by the `build.sh` script
  - `[--Test options]`: options accepted by the `run_test.sh` script
  - `[<device_id_1> <device_id_2>]`: the two device ids used by the samples

- You can run every stage in one go:

  ```bash
  # Use the default device id
  bash hixl.sh

  # Use the specified device id
  bash hixl.sh 0 2
  ```

- You can combine `hixl_build`, `hixl_test`, `hixl_install` and `hixl_samples` as needed, for example:

  ```bash
  # Build only
  bash hixl.sh hixl_build --examples --output-path=./build_out

  # Build + UTST
  bash hixl.sh hixl_build --examples hixl_test -t=cpp

  # Build + run samples
  bash hixl.sh hixl_build --examples hixl_samples 0 2
  ```

- You can specify the network interface name. If it is not specified, interfaces whose names start with `eth` or `enp` are used by default.

  ```sh
  # Use the specified network interface name
  export SOCKET_IFNAME=enp67s0f5
  bash hixl.sh 0 2
  ```
