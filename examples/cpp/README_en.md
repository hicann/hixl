## Contents

- [Sample](#sample)
- [Directory Structure](#directory-structure)
- [Environment Requirements](#sample-configuration)
- [Build Instructions](#build-instructions)
- [Sample Running](#sample-running)

## Sample

Function: Manages KV Cache in a separate deployment scenario through the LLM‑DataDist API. 

## Directory Structure

```
├── cpp
|   ├── prompt_pull_cache_and_blocks.cpp               // Prompt implementation of pull cache and pull blocks
|   ├── decoder_pull_cache_and_blocks.cpp              // Decoder implementation of pull cache and pull blocks
|   ├── prompt_push_cache_and_blocks.cpp               // Prompt implementation of push cache and push blocks
|   ├── decoder_push_cache_and_blocks.cpp              // Decoder implementation of push cache and push blocks
|   ├── prompt_switch_roles.cpp                        // Prompt implementation of switch roles
|   ├── decoder_switch_roles.cpp                       // Decoder implementation of switch roles
|   ├── client_server_h2d.cpp                          // HIXL client–server mode, H2D scenario sample
|   ├── server_server_d2d.cpp                          // HIXL server–server mode, D2D scenario sample
|   ├── fabric_mem_d2d.cpp                             // HIXL FabricMem mode, D2D scenario sample
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

## Build Instructions


1. Compile using the `bash build.sh --examples` command. See "Compile the Source Code" in [Build from Source Code](../../docs/en/build.md).

2. After compilation, multiple executables are generated under the `build/examples/cpp`` directory.

## Sample Running

### 1. prompt/decoder Samples
 - Note:
    - All samples must run in pairs. The execution interval between the prompt side and decoder side should not be too long. In these samples, the decoder side sets `WAIT_PROMPT_TIME` to 5 s, and the prompt side sets `WAIT_TIME` to 10 s. You may adjust these values according to your actual environment to ensure successful execution.
    - The following samples assume that the prompt and decoder run on the same machine. Set `local_ip` and `remote_ip` to the same value. 

 - Configure environment variables
    - If Ascend-CANN-Toolkit is installed in the runtime environment, set the environment variables as follows:

        ```
        source ${HOME}/Ascend/cann/set_env.sh
        ```

        Replace `${HOME}/Ascend` with the actual software installation path.

    -  If the `CANN‑XXX.run` package is installed in the runtime environment, set the environment variables as follows:

        ```
        source ${HOME}/Ascend/latest/bin/setenv.bash
        ```

        Replace `${HOME}/Ascend` with the actual software installation path.

 - Run the executable in the runtime environment.

    (1) Run `pull_cache_and_blocks`.

    This sample demonstrates the workflow where the decoder pulls cache and blocks from the prompt. The direction of link setup and pull operations is independent of role assignment and can be modified as needed. 

    - Run `prompt_pull_cache_and_blocks` with parameters `device_id` and `local_ip`. Here `device_id` is the device ID used by the prompt, and `local_ip` is the IP address of the host running the prompt. For example: 
        ```
        ./prompt_pull_cache_and_blocks 0 10.10.170.1
        ```

    - Run `decoder_pull_cache_and_blocks` with parameters `device_id`, `local_ip`, and `remote_ip`. Here `device_id` is the device ID used by the decoder, `local_ip` is the IP address of the host running the decoder, and `remote_ip` is the IP address of the host running the prompt. For example:
        ```
        ./decoder_pull_cache_and_blocks 2 10.170.10.1 10.170.10.1
        ```
    
    - If you are running this command in an A5 environment, add the `local_comm_res` parameter. For example:
        ```
        # prompt host
        HCCL_INTRA_ROCE_ENABLE=1 ./prompt_pull_cache_and_blocks 0 10.10.170.0 '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'

        # Decoder host
        HCCL_INTRA_ROCE_ENABLE=1 ./decoder_pull_cache_and_blocks 0 10.170.10.1 10.170.10.0 '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
        ```

    (2) Run `push_cache_and_blocks`.

    This sample demonstrates the workflow where the prompt pushes cache and blocks to the decoder. The direction of link setup and push operations is independent of role assignment and can be modified as needed.

    - Run `prompt_push_cache_and_blocks` with parameters `device_id`, `local_ip`, and `remote_ip`. Here `device_id` is the device ID used by the prompt, `local_ip` is the IP address of the host running the prompt, and `remote_ip` is the IP address of the host running the decoder. For example:
        ```
        ./prompt_push_cache_and_blocks 0 10.10.10.1 10.10.10.1
        ```

    - Run `decoder_push_cache_and_blocks` with parameters `device_id` and `local_ip`. Here `device_id` is the device ID used by the decoder, and `local_ip` is the IP address of the host running the decoder. For example:
        ```
        ./decoder_push_cache_and_blocks 4 10.10.10.1
        ```

    - If you are running this command in an A5 environment, add the `local_comm_res` parameter. For example:
        ```
        # prompt host
        HCCL_INTRA_ROCE_ENABLE=1 ./prompt_push_cache_and_blocks 0 10.10.10.0 10.10.10.1 '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'

        # Decoder host
        HCCL_INTRA_ROCE_ENABLE=1 ./decoder_push_cache_and_blocks 0 10.10.10.1 '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
        ```

    (3) Run `switch_roles`.

    This sample demonstrates prompt–decoder role switching combined with pull and push workflows.

    - Run `prompt_switch_roles` with parameters `device_id`, `local_ip`, and `remote_ip`. Here `device_id` is the device ID used by the prompt, `local_ip` is the IP address of the host running the prompt, and `remote_ip` is the IP address of the host running the decoder. For example:
        ```
        ./prompt_switch_roles 0 10.10.170.1 10.170.10.1
        ```

    - Run `decoder_switch_roles` with parameters `device_id`, `local_ip`, and `remote_ip`. Here `device_id` is the device ID used by the decoder, `local_ip` is the IP address of the host running the decoder, and `remote_ip` is the IP address of the host running the prompt. For example:
        ```
        ./decoder_switch_roles 2 10.170.10.1 10.170.10.1
        ```

### 2. HIXL Samples
  - Note:
    - All samples must run in pairs. The execution interval between the client and server should not be too long. In client–server samples, `WAIT_REG_TIME` is set to `5 s` and `WAIT_TRANS_TIME` to `20 s`. In server–server samples, `WAIT_TIME` is set to `5 s`. You may adjust these values according to your actual environment to ensure successful execution.
    - The following samples can run only on a single machine. The IP portion of `local_engine` and `remote_engine` must be identical. The server-side engine uses the `ip:port` format, while the client-side engine uses the `ip` format. Multi‑machine execution requires modifying the samples.

  - Configure environment variables
    - If Ascend-CANN-Toolkit is installed in the runtime environment, set the environment variables as follows:

        ```
        source ${HOME}/Ascend/cann/set_env.sh
        ```

        Replace `${HOME}/Ascend` with the actual software installation path.

    -  If the `CANN‑XXX.run` package is installed in the runtime environment, set the environment variables as follows:

        ```
        source ${HOME}/Ascend/latest/bin/setenv.bash
        ```

        Replace `${HOME}/Ascend` with the actual software installation path.

  - Run the executable in the runtime environment.

    (1) Run `client_server_h2d` in client-server mode in the H2D scenario.

    - Run `client client_server_h2d` with parameters `device_id`, `local engine`, and `remote engine`. Here, `device_id` is the device ID used by the client. For example:
        ```
        HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d 0 10.10.10.0 10.10.10.0:16000
        ```

    - Run `server client_server_h2d` with parameters `device_id` and `local engine`. Here, `device_id` is the device ID used by the server. For example:
        ```
        HCCL_INTRA_ROCE_ENABLE=1 ./client_server_h2d 1 10.10.10.0:16000
        ```

    (2) Run `server_server_d2d` in D2D mode, with both servers acting as servers.

    - Run `server1 server_server_d2d` with parameters `device_id`, `local engine`, and `remote engine`. Here, `device_id` is the device ID used by the current engine. For example:
        ```
        HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d 0 10.10.10.0:16000 10.10.10.0:16001
        ```

    - Run `server2 server_server_d2d` with parameters `device_id`, `local engine`, and `remote engine`. Here, `device_id` is the device ID used by the current engine. For example:
        ```
        HCCL_INTRA_ROCE_ENABLE=1 ./server_server_d2d 1 10.10.10.0:16001 10.10.10.0:16000
        ```
    In the command, `HCCL_INTRA_ROCE_ENABLE=1` indicates that RDMA is used for data transfer.

    (3) Run `fabric_mem_d2d` in FabricMem mode in the D2D scenario.

    **Note**: To use the FabricMem mode, upgrade the HDK to 26.0 or later.

      - Run `server1 fabric_mem_d2d` with parameters `device_id`, `local engine`, and `remote engine`. Here, `device_id` is the device ID used by the current engine. For example:
          ```
          ./fabric_mem_d2d 0 127.0.0.1:16000 127.0.0.1:16001
          ```

      - Run `server2 fabric_mem_d2d` with parameters `device_id`, `local engine`, and `remote engine`. Here, `device_id` is the device ID used by the current engine. For example:
          ```
          ./fabric_mem_d2d 1 127.0.0.1:16001 127.0.0.1:16000
          ```
