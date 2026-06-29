## Contents

- [Contents](#contents)
- [Sample Introduction](#sample-introduction)
- [Directory Structure](#directory-structure)
- [Environment Setup](#environment-setup)
- [Sample Configuration](#sample-configuration)
- [Sample Running](#sample-running)

## Sample Introduction

Function: Implements KV Cache management functionality in disaggregated deployment scenarios through the LLM-DataDist API.

## Directory Structure

```sh
├── python
|   ├── pull_blocks_sample.py
|   ├── pull_blocks_xpyd_sample.py
|   ├── pull_cache_sample.py
|   ├── pull_from_cache_to_blocks.py
|   ├── push_blocks_sample.py
|   ├── push_cache_sample.py
|   ├── switch_role_sample.py
|   ├── transfer_cache_async_sample.py
|   ├── hixl_transfer_backend_sample.py
```

## Environment Setup

- Install dependencies from `requirements.txt` in the root directory (skip if already installed):

  ```bash
  pip3 install -r requirements.txt
  ```

- Install **pytorch** and **torch_npu** packages

  According to the Python version you use, refer to [Version Compatibility Guide](https://gitcode.com/Ascend/pytorch) to download and install the corresponding versions of `pytorch` and `torch_npu`. For installation methods, please refer to [Software Installation Guide](https://gitcode.com/Ascend/pytorch/blob/v2.7.1/docs/zh/installation_guide/menu_installation_guide.md). Below are the compatible versions and installation commands for Python 3.13. For other versions, refer to the version compatibility guide and installation instructions.

  | PyTorch Version | torch_npu Plugin Version | Python Version | System Architecture | CANN Version | Installation Method | Installation Command |
  | --------------- | ------------------------ | -------------- | ------------------- | ------------ | ------------------- | -------------------- |
  | 2.7.1           | 26.0.0                   | Python 3.13    | AArch64             | 9.0.0        | Offline (Whl)       | # Download and install PyTorch framework <br/> `wget https://download.pytorch.org/whl/cpu/torch-2.7.1%2Bcpu-cp313-cp313-manylinux_2_28_aarch64.whl `<br>`pip3 install torch-2.7.1+cpu-cp313-cp313-manylinux_2_28_aarch64.whl ` <br/><br/> # Download and install torch_npu plugin <br> `wget https://gitcode.com/Ascend/pytorch/releases/download/v26.0.0-pytorch2.7.1/torch_npu-2.7.1.post4-cp313-cp313-manylinux_2_28_aarch64.whl`<br> ` pip3 install torch_npu-2.7.1.post4-cp313-cp313-manylinux_2_28_aarch64.whl` |
  | 2.7.1           | 26.0.0                   | Python 3.13    | X86_64              | 9.0.0        | Offline (Whl)       | # Download and install PyTorch framework  <br/> ` wget https://download.pytorch.org/whl/cpu/torch-2.7.1%2Bcpu-cp313-cp313-manylinux_2_28_x86_64.whl`<br> `pip3 install torch-2.7.1+cpu-cp313-cp313-manylinux_2_28_x86_64.whl ` <br/><br/> # Download and install torch_npu plugin <br> ` wget https://gitcode.com/Ascend/pytorch/releases/download/v26.0.0-pytorch2.7.1/torch_npu-2.7.1.post4-cp313-cp313-manylinux_2_28_x86_64.whl` <br>  `pip3 install torch_npu-2.7.1.post4-cp313-cp313-manylinux_2_28_x86_64.whl` |

## Sample Configuration

All sample executions require correct configuration of Ascend environment variables. For dual-host examples, try to execute them synchronously on both hosts.

```sh
source ${HOME}/Ascend/cann/set_env.sh
```

Replace `${HOME}/Ascend` with the actual software installation path.

- Modify the device information in the sample:
  - Change `device_ip` in `PROMPT_IP_LIST` to the device IP addresses of the Prompt host.
  - Change `PROMPT_HOST_IP` to the host IP address of the Prompt host.
  - Change `device_ip` in `DECODER_IP_LIST` to the device IP addresses of the Decoder host.
  - Change `DECODER_HOST_IP` to the host IP address of the Decoder host.
  - Ensure that the scripts are identical on both hosts.

- Some samples support RDMA links in A5 environments and must be executed on dual hosts. These samples will be noted explicitly in the corresponding sample descriptions. Before execution, manually configure `local_comm_res`. The configuration format reference is available at: [Communication Device Configuration](https://gitcode.com/cann/hixl/issues/37). You can obtain the host NIC IP information using the following commands:

  ```sh
  # Query the mapping between RoCE devices and network ports, and identify the ports in Up state
  ibdev2netdev

  # Find the corresponding IP addresses using the network ports
  ifconfig
  ```

## Sample Running

- Run the `pull_cache` sample. This sample demonstrates how to allocate cache memory, establish bidirectional connections, and pull cache from a remote node in a configured memory pool scenario.
  - Note:
    This example requires two hosts. See [Sample Configuration](#sample-configuration).

  Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, and `cluster_id` specifies the cluster ID which must be unique across all nodes involved in connection establishment:

    ```sh
    # Prompt host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_cache_sample.py --device_id 0 --cluster_id 1
    # Decoder host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_cache_sample.py --device_id 0 --cluster_id 2
    ```

- Run the `pull_blocks` sample. This sample uses Torch to allocate memory, establishes bidirectional connections, and pulls cache from a remote node.
  - Note:
    This example requires two hosts. See [Sample Configuration](#sample-configuration).

  Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, and `cluster_id` specifies the cluster ID which must be unique across all nodes involved in connection establishment:

    ```sh
    # Prompt host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_sample.py --device_id 0 --cluster_id 1
    # Decoder host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_sample.py --device_id 0 --cluster_id 2
    ```

- Run the `pull_from_cache_to_blocks` sample:
  - Note:
    This example requires two hosts. See [Sample Configuration](#sample-configuration).

  Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, and `cluster_id` specifies the cluster ID which must be unique across all nodes involved in connection establishment:

    ```sh
    # Prompt host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_from_cache_to_blocks.py --device_id 0 --cluster_id 1
    # Decoder host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_from_cache_to_blocks.py --device_id 0 --cluster_id 2
    ```

- Run the `push_blocks` sample. This sample uses a single-side connection method to allocate memory, register blocks, and have the decoder initiate the connection to push blocks.
  Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host:

    ```sh
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```

  If running in an A5 environment, add the `local_comm_res` parameter. For example:

    ```sh
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
    ```

- Run the `push_cache` sample. This sample uses a single-side connection method to allocate memory, register cache, and have the decoder initiate the connection to push cache.
  Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host:

    ```sh
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```

  If running in an A5 environment, add the `local_comm_res` parameter. For example:

    ```sh
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
    ```

- Run the `switch_role` sample. This sample uses a single-side connection method. First, Torch allocates memory and registers blocks. The decoder initiates the connection to pull blocks. Then both sides switch roles, with the prompt initiating the connection and the decoder pushing blocks.
  Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host:

    ```sh
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python switch_role_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python switch_role_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```

- Run the `pull_blocks_xpyd` sample. This sample supports xPyD test scenarios and uses a single-side connection method. Each process allocates memory and registers blocks. Each decoder initiates connections to all prompts and pulls blocks locally. `local_ip_port` specifies the local host IP and port.
  - Note:
    This sample can run on any number of hosts. Regardless of how many processes are started on the Prompt side, `prompt_ip_list` on the Decoder side consists of all `${local_ip:port}` entries from the Prompt side.

  ```sh
  # Prompt side:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port ${local_ip_0:port_0}
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 1 --role p --local_ip_port ${local_ip_1:port_1}
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n --role p --local_ip_port ${local_ip_n:port_n}
  # Decoder side:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n + 1 --role d --local_ip_port ${local_ip_n+1:port_n+1} --remote_ip_port ${prompt_ip_list}
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n + 2 --role d --local_ip_port ${local_ip_n+2:port_n+2} --remote_ip_port ${prompt_ip_list}
  ```

  Where `${prompt_ip_list}` consists of **all `${local_ip:port}` entries from the Prompt side**, separated by **;**. Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, `role` specifies the cluster role, `local_ip_port` specifies the local host IP and port, and `remote_ip_port` specifies the peer host IP and port:

  ```sh
  # Any number of Prompt hosts:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port 10.10.10.0:26000
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 1 --role p --local_ip_port 10.10.10.0:26001
  # Any number of Decoder hosts:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 2 --role d --local_ip_port 10.10.10.0:26002 --remote_ip_port '10.10.10.0:26000;10.10.10.0:26001'
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 3 --role d --local_ip_port 10.10.10.0:26003 --remote_ip_port '10.10.10.0:26000;10.10.10.0:26001'
  ```

- Run the `transfer_cache_async` sample. This sample uses a single-side connection method to allocate memory, register cache, and have the prompt side initiate the connection to asynchronously transfer cache layer by layer.

  Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host:

    ```sh
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```

  If running in an A5 environment, add the `local_comm_res` parameter. For example:

    ```sh
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
    ```

- Run the `hixl_transfer_backend` sample. This sample uses HIXL as the transport backend for LLM-DataDist, performing memory registration, connection establishment, and data transfer. The sample allocates memory and registers blocks. The decoder initiates a connection and pushes blocks. The prompt initiates a connection and pulls blocks.
  Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to use, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host:

    ```sh
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python hixl_transfer_backend_sample.py --device_id 0 --role p --local_host_ip 10.10.10.0 --remote_host_ip 10.10.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python hixl_transfer_backend_sample.py --device_id 1 --role d --local_host_ip 10.10.10.1 --remote_host_ip 10.10.10.0
    ```

**Note**: **GLOO_SOCKET_IFNAME** is the local NIC name, which can be queried via ifconfig; **HCCL_INTRA_ROCE_ENABLE=1** indicates using RoCE for communication.
