## Contents

- [Contents](#contents)
- [Sample Introduction](#sample-introduction)
- [Directory Structure](#directory-structure)
- [Sample Configuration](#sample-configuration)
- [Program Build](#program-build)
- [Sample Running](#sample-running)
  - [1. LLM-DataDist Samples](#1-llm-datadist-samples)
  - [2. HIXL Samples](#2-hixl-samples)

## Sample Introduction

Function: Implements KV Cache transmission functionality in disaggregated deployment scenarios through LLM-DataDist and HIXL APIs.

## Directory Structure

```sh
├── cpp
|   ├── prompt_pull_cache_and_blocks.cpp               // Prompt side implementation for pull cache and pull blocks
|   ├── decoder_pull_cache_and_blocks.cpp              // Decoder side implementation for pull cache and pull blocks
|   ├── prompt_push_cache_and_blocks.cpp               // Prompt side implementation for push cache and push blocks
|   ├── decoder_push_cache_and_blocks.cpp              // Decoder side implementation for push cache and push blocks
|   ├── prompt_switch_roles.cpp                        // Prompt side implementation for switch_roles
|   ├── decoder_switch_roles.cpp                       // Decoder side implementation for switch_roles
|   ├── hixl_example_d2rd.cpp                          // HIXL D2rD single-process scenario sample
|   ├── hixl_example_d2rh.cpp                          // HIXL D2rH single-process scenario sample
|   ├── hixl_example_d2rd_multiproc.cpp                // HIXL D2rD multi-process scenario sample
|   ├── fabric_mem_d2d.cpp                             // HIXL fabric-mem mode d2d scenario sample
|   ├── CMakeLists.txt                                 // Build script
```

## Sample Configuration

Some samples support execution over RDMA links in the A5 environment and must run on two machines. These cases are explicitly noted in the corresponding sample descriptions. Before execution, manually configure `local_comm_res`. The configuration format reference is available at: [Communication Device Configuration](https://gitcode.com/cann/hixl/issues/37). You can obtain the host NIC IP information using the following commands:

```shell
# Query the mapping between RoCE devices and network ports, and identify the ports in Up state
ibdev2netdev

# Find the corresponding IP addresses using the network ports
ifconfig
```

## Program Build

1. Refer to the **Compile and Execute** section in [Build](../../docs/en/build.md) and use `bash build.sh --examples` to compile.

2. After compilation, multiple executable files are generated in the **build/examples/cpp** directory.

## Sample Running

### 1. LLM-DataDist Samples

  - Note:

    - All samples must run in pairs. The execution interval between the prompt and decoder sides should not be too long. In the samples, the decoder side sets `WAIT_PROMPT_TIME` to 5s, and the prompt side sets `WAIT_TIME` to 10s. You may adjust these values according to your actual environment to ensure successful execution.
    - The following samples assume that the prompt and decoder run on the same machine. Set `local_ip` and `remote_ip` to the same value.

  - Configure environment variables
    - If "Ascend-cann-toolkit" package is installed in the runtime environment, set the environment variables as follows:

        ```cpp
        source ${HOME}/Ascend/cann/set_env.sh
        ```

        Replace "${HOME}/Ascend" with the actual software installation path.

    - If "CANN-XXX.run" package is installed in the runtime environment, set the environment variables as follows:

        ```cpp
        source ${HOME}/Ascend/latest/bin/setenv.bash
        ```

        Replace "${HOME}/Ascend" with the actual software installation path.

  - Run the executable files in the runtime environment.

    (1) Run pull_cache_and_blocks

    This sample demonstrates the workflow where the decoder pulls cache and blocks from the prompt. The direction of link setup and pull operations is independent of role assignment and can be modified as needed.

    - Run prompt_pull_cache_and_blocks with parameters device_id and local_ip, where device_id is the device ID used by the prompt, and local_ip is the IP address of the host where the prompt runs. For example:

        ```cpp
        ./prompt_pull_cache_and_blocks 0 10.10.170.1
        ```

    - Run decoder_pull_cache_and_blocks with parameters device_id, local_ip and remote_ip, where device_id is the device ID used by the decoder, local_ip is the IP address of the host where the decoder runs, and remote_ip is the IP address of the host where the prompt runs. For example:

        ```cpp
        ./decoder_pull_cache_and_blocks 2 10.170.10.1 10.170.10.1
        ```

    - If running in an A5 environment, add the local_comm_res parameter. For example:

        ```cpp
        # Prompt host
        HCCL_INTRA_ROCE_ENABLE=1 ./prompt_pull_cache_and_blocks 0 10.10.170.0 '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'

        # Decoder host
        HCCL_INTRA_ROCE_ENABLE=1 ./decoder_pull_cache_and_blocks 0 10.170.10.1 10.170.10.0 '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
        ```

    (2) Run push_cache_and_blocks

    This sample demonstrates the workflow where the prompt pushes cache and blocks to the decoder. The direction of link setup and push operations is independent of role assignment and can be modified as needed.

    - Run prompt_push_cache_and_blocks with parameters device_id, local_ip and remote_ip, where device_id is the device ID used by the prompt, local_ip is the IP address of the host where the prompt runs, and remote_ip is the IP address of the host running the decoder. For example:

        ```cpp
        ./prompt_push_cache_and_blocks 0 10.10.10.1 10.10.10.1
        ```

    - Run decoder_push_cache_and_blocks with parameters device_id and local_ip, where device_id is the device ID used by the decoder, and local_ip is the IP address of the host where the decoder runs. For example:

        ```cpp
        ./decoder_push_cache_and_blocks 4 10.10.10.1
        ```

    - If running in an A5 environment, add the local_comm_res parameter. For example:

        ```cpp
        # Prompt host
        HCCL_INTRA_ROCE_ENABLE=1 ./prompt_push_cache_and_blocks 0 10.10.10.0 10.10.10.1 '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'

        # Decoder host
        HCCL_INTRA_ROCE_ENABLE=1 ./decoder_push_cache_and_blocks 0 10.10.10.1 '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
        ```

    (3) Run switch_roles

    This sample demonstrates prompt and decoder role switching combined with pull and push workflows.

    - Run prompt_switch_roles with parameters device_id, local_ip and remote_ip, where device_id is the device ID used by the prompt, local_ip is the IP address of the host where the prompt runs, and remote_ip is the IP address of the host where the decoder runs. For example:

        ```cpp
        ./prompt_switch_roles 0 10.10.170.1 10.170.10.1
        ```

    - Run decoder_switch_roles with parameters device_id, local_ip and remote_ip, where device_id is the device ID used by the decoder, local_ip is the IP address of the host where the decoder runs, and remote_ip is the IP address of the host where the prompt runs. For example:

        ```cpp
        ./decoder_switch_roles 2 10.170.10.1 10.170.10.1
        ```

### 2. HIXL Samples

  - Note:

    - Single-process samples (hixl_example_d2rd, hixl_example_d2rh) start two engines in one process, no need for separate terminals. Supported on CANN-9.1.0 or later.
    - Multi-process sample (hixl_example_d2rd_multiproc) requires starting server and client in two separate terminals. Server starts first. No CANN version requirement.
    - fabric_mem_d2d must run in pairs, started in two terminals.

  - HIXL Sample Process Parameter Description

    | Parameter | Applicable Samples | Required/Optional | Default Value | Description |
    | --- | --- | --- | --- | --- |
    | `--protocol=<type>[,...]` | hixl_example_d2rd, hixl_example_d2rh, hixl_example_d2rd_multiproc | Required | - | Communication protocol, supports comma-separated multiple protocols. hixl_example_d2rd supports `roce:device`, `uboe:device`, `ub_ctp:device`, `ub_tp:device`; hixl_example_d2rh supports `roce:device`, `uboe:device`, `ub_ctp:device`, `ub_tp:device`, `ub_ctp:host`, `ub_tp:host`; hixl_example_d2rd_multiproc supports `hccs:device`, `roce:device`, `uboe:device`, `ub_ctp:device`, `ub_tp:device`. Protocol hardware dependencies: `hccs:device`, `roce:device` only support Atlas A2 training products/Atlas A2 inference products, Atlas A3 training products/Atlas A3 inference products; `uboe:device`, `ub_ctp:device`, `ub_tp:device`, `ub_ctp:host`, `ub_tp:host` only support Ascend 950PR/Ascend 950DT. Protocol definition details refer to `comm_resource_config.protocol_desc` option description in [HIXL Interface](../../docs/zh/api/cpp/HIXL-interface.md). |
    | `--device=<id>` or `--device=id1,id2` | hixl_example_d2rd, hixl_example_d2rh, hixl_example_d2rd_multiproc | Optional | hixl_example_d2rd, hixl_example_d2rh default 0,2; hixl_example_d2rd_multiproc default client=0, server=2 | hixl_example_d2rd, hixl_example_d2rh use `--device=id1,id2` to specify two devices bound to each engine; hixl_example_d2rd_multiproc uses `--device=<id>` to specify the device bound to the current process. |
    | <code>--version=0&#124;1</code> | hixl_example_d2rd, hixl_example_d2rh, hixl_example_d2rd_multiproc | Optional | 1 | When set to 0, uses HCCL collective communication domain-based one-sided communication capability, only supports Atlas A2 training products/Atlas A2 inference products, Atlas A3 training products/Atlas A3 inference products; when set to 1, uses HIXL CS interface-based one-sided communication capability, decouples communication domain, recommended, supports Atlas A2 training products/Atlas A2 inference products, Atlas A3 training products/Atlas A3 inference products, Ascend 950PR/Ascend 950DT. |
    | <code>--role=client&#124;server</code> | hixl_example_d2rd_multiproc | Required | - | Specifies the current process as client or server. |
    | `--local-engine=<ip:port>` | hixl_example_d2rd_multiproc | Optional | client=127.0.0.1:16000, server=127.0.0.1:16001 | Specifies the engine address of the current process. |
    | `--remote-engine=<ip:port>` | hixl_example_d2rd_multiproc | Optional | client=127.0.0.1:16001, server=127.0.0.1:16000 | Specifies the engine address of the peer process. |

  - Configure environment variables
    - If "Ascend-cann-toolkit" package is installed in the runtime environment, set the environment variables as follows:

        ```cpp
        source ${HOME}/Ascend/cann/set_env.sh
        ```

        Replace "${HOME}/Ascend" with the actual software installation path.

    - If "CANN-XXX.run" package is installed in the runtime environment, set the environment variables as follows:

        ```cpp
        source ${HOME}/Ascend/latest/bin/setenv.bash
        ```

        Replace "${HOME}/Ascend" with the actual software installation path.

  - Run the executable files in the runtime environment.

    (1) Run hixl_example_d2rd, D2RD single-process scenario

      - Note:
        - Single-process starts two engines in one thread, each binding to a different device. Engine A initiates WRITE transfer to Engine B's device buffer.

      - Example:

          ```cpp
          # Use roce:device protocol
          ./hixl_example_d2rd --protocol=roce:device

          # Specify device
          ./hixl_example_d2rd --protocol=roce:device --device=0,2

          # Use version 0 mode (only supports roce:device)
          ./hixl_example_d2rd --protocol=roce:device --version=0
          ```

    (2) Run hixl_example_d2rh, D2RH single-process scenario

      - Note:
        - Single-process starts two engines in one thread, each binding to a different device. Both sides initiate WRITE transfer to each other's host buffer.

      - Example:

          ```cpp
          # Use roce:device protocol
          ./hixl_example_d2rh --protocol=roce:device

          # Use host-side protocol
          ./hixl_example_d2rh --protocol=ub_ctp:host,ub_ctp:device

          # Use version 0 mode
          ./hixl_example_d2rh --protocol=roce:device --version=0
          ```

    (3) Run hixl_example_d2rd_multiproc, D2RD multi-process scenario

      - Note:
        - Two independent processes start engines separately. After exchanging buffer addresses via socket, the client initiates READ transfer and verifies locally.

      - Example:

          ```cpp
          # Use roce protocol
          ./hixl_example_d2rd_multiproc --role=server --protocol=roce:device
          ./hixl_example_d2rd_multiproc --role=client --protocol=roce:device

          # Use hccs protocol
          ./hixl_example_d2rd_multiproc --role=server --protocol=hccs:device
          ./hixl_example_d2rd_multiproc --role=client --protocol=hccs:device

          # Use version 0 mode
          ./hixl_example_d2rd_multiproc --role=server --protocol=roce:device --version=0
          ./hixl_example_d2rd_multiproc --role=client --protocol=roce:device --version=0
          ```

    (4) Run fabric_mem_d2d, fabric mem mode D2D scenario

    **Note**: To use fabric mem mode, HDK must be upgraded to version 26.0 or later.

      - Run server1 fabric_mem_d2d with parameters device_id, local engine and remote engine, where device_id is the device ID used by the current engine. For example:

          ```cpp
          ./fabric_mem_d2d 0 127.0.0.1:16000 127.0.0.1:16001
          ```

      - Run server2 fabric_mem_d2d with parameters device_id, local engine and remote engine, where device_id is the device ID used by the current engine. For example:

          ```cpp
          ./fabric_mem_d2d 1 127.0.0.1:16001 127.0.0.1:16000
          ```
