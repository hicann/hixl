## Contents

- [Contents](#contents)
- [Sample](#sample)
- [Directory Structure](#directory-structure)
- [Environment Setup](#environment-setup)
- [Sample Configuration](#sample-configuration)
- [Sample Running](#sample-running)


## Sample

Function: Manages KV Cache in separated deployment through the `LLM-DataDist` API.

## Directory Structure

```
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

Install the dependencies in the `requirements.txt` file under the root directory. (Skip this step if the dependencies have been installed.)

```bash
pip3 install -r requirements.txt
```
Install the corresponding `torch` and `torch_npu` packages according to your environment (version 2.1.0 or later is recommended). [Obtain installation packages](https://gitcode.com/Ascend/pytorch)

## Sample Configuration

All sample executions require correct configuration of Ascend environment variables. For dual-host examples, try to execute them synchronously on both hosts.
```
source ${HOME}/Ascend/cann/set_env.sh
```
Replace `${HOME}/Ascend` with the actual software installation path.

- Modify the device information in the sample:
  - Change `device_ip` in `PROMPT_IP_LIST` to the device IP addresses of the Prompt host.
  - Change `PROMPT_HOST_IP` to the host IP address of the Prompt host.
  - Change `device_ip` in `DECODER_IP_LIST` to device IP addresses of the Decoder host.
  - Change `DECODER_HOST_IP` to the host IP address of the Decoder host.
  - Ensure that the scripts are identical on both hosts.

- Some samples support RDMA links in A5 environments and must be executed on dual hosts. These samples will be noted explicitly in the corresponding sample descriptions. Before execution, manually configure `local_comm_res`. The configuration format reference is available at: [Communication Device Configuration](https://gitcode.com/cann/hixl/issues/37). You can obtain the host NIC IP information using the following commands: 
  ```shell
  # Query the mapping between RoCE devices and network ports, and identify the ports in Up state
  ibdev2netdev

  # Find the corresponding IP addresses using the network ports
  ifconfig
  ```

## Sample Running
- Run the `pull_cache` sample. This sample demonstrates how to allocate cache memory, establish bidirectional connections, and pull cache from a remote node in a configured memory pool scenario.
  - Note:
    This example requires two hosts. See [Sample Configuration](#Sample Configuration).

  Run the sample program on both the Prompt and Decoder hosts. In the command, `device_id` specifies the device ID to be used, and `cluster_id` specifies the cluster ID that must be unique across all nodes involved in link establishment.
    ```
    # Prompt host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_cache_sample.py --device_id 0 --cluster_id 1
    # Decoder host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_cache_sample.py --device_id 0 --cluster_id 2
    ```
- Run the `pull_blocks` sample. This sample uses Torch to allocate memory, establishes bidirectional connections, and pulls cache from a remote node.
  - Note:
    This example requires two hosts. See [Sample Configuration](#Sample Configuration).

  Run the sample program on both the Prompt and Decoder hosts. In the command, `device_id` specifies the device ID to be used, and `cluster_id` specifies the cluster ID that must be unique across all nodes involved in link establishment.
    ```
    # Prompt host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_sample.py --device_id 0 --cluster_id 1
    # Decoder host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_sample.py --device_id 0 --cluster_id 2
    ```
- Run the `pull_from_cache_to_blocks` sample:
  - Note:
    This example requires two hosts. See [Sample Configuration](#Sample Configuration).

  Run the sample program on both the Prompt and Decoder hosts. In the command, `device_id` specifies the device ID to be used, and `cluster_id` specifies the cluster ID that must be unique across all nodes involved in link establishment.
    ```
    # Prompt host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_from_cache_to_blocks.py --device_id 0 --cluster_id 1
    # Decoder host:
    HCCL_INTRA_ROCE_ENABLE=1 python pull_from_cache_to_blocks.py --device_id 0 --cluster_id 2
    ```
- Run the `push_blocks` sample. This sample uses a single-side connection method to allocate memory, register blocks, and have the decoder initiate the connection to push blocks.
  Run the sample program on both the Prompt and Decoder hosts. In the command, `device_id` specifies the device ID to be used, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host.
    ```
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
  If you are running this command in an A5 environment, add the `local_comm_res` parameter. For example:
    ```
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_blocks_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
    ```
- Run the `push_cache` sample. This sample uses a single-side connection method to allocate memory, register cache, and have the decoder initiate the connection to push cache.
  Run the sample program on both the Prompt and Decoder hosts. In the command, `device_id` specifies the device ID to be used, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host.
    ```
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
  If you are running this command in an A5 environment, add the `local_comm_res` parameter. For example:
    ```
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python push_cache_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
    ```
- Run the `switch_role` sample. This sample uses a single-side connection method. First, the decoder uses Torch to allocate memory, registers blocks, and initiates the connection to pull blocks. Then, both sides switch roles. The Prompt initiates the connection, and the decoder pushes blocks.
  Run the sample program on both the Prompt and Decoder hosts. In the command, `device_id` specifies the device ID to be used, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host.
    ```
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python switch_role_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python switch_role_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
- Run the `pull_blocks_xpyd` sample. This sample supports the xPyD test scenario and uses a single-side connection method. Each process allocates memory and registers blocks. Each decoder initiates connections to all Prompts and pulls blocks locally. `local_ip_port` specifies the local host IP address and port.
  - Note:
    This sample can run on any number of hosts. Regardless of how many processes are started on the Prompt side, `prompt_ip_list` on the Decoder side consists of all `${local_ip:port}` entries from the Prompt side.
  ```  
  #Prompt side:  
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port ${local_ip_0:port_0}  
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 1 --role p --local_ip_port ${local_ip_1:port_1}
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n --role p --local_ip_port ${local_ip_n:port_n}
  #Decoder side: 
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n + 1 --role d --local_ip_port ${local_ip_n+1:port_n+1} --remote_ip_port ${prompt_ip_list}  
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id n + 2 --role d --local_ip_port ${local_ip_n+2:port_n+2} --remote_ip_port ${prompt_ip_list}  
  ```

  Here, `${prompt_ip_list}` consists of **all `${local_ip:port}` entries from the Prompt side**, separated by **;**. Run the sample program on both the Prompt and Decoder hosts. `device_id` specifies the device ID to be used, `role` specifies the cluster role, `local_ip_port` specifies the local host IP and port, and `remote_ip_port` specifies the peer host IP and port:
  ```
  # Any number of Prompt hosts:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 0 --role p --local_ip_port 10.170.10.0:26000
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 1 --role p --local_ip_port 10.170.10.0:26001
  # Any number of Decoder hosts:
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 2 --role d --local_ip_port 10.170.10.0:26002 --remote_ip_port '10.170.10.0:26000;10.170.10.0:26001'
  GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python pull_blocks_xpyd_sample.py --device_id 3 --role d --local_ip_port 10.170.10.0:26003 --remote_ip_port '10.170.10.0:26000;10.170.10.0:26001'
  ```
- Run the `transfer_cache_async` sample. This sample uses a single-side connection method to allocate memory, register cache, and have the Prompt side initiate the connection to asynchronously transfer cache in layers.

  Run the sample program on both the Prompt and Decoder hosts. In the command, `device_id` specifies the device ID to be used, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host.
    ```
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
  If you are running this command in an A5 environment, add the `local_comm_res` parameter. For example:
    ```
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.1","placement":"host"}],"version":"1.3"}'
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python transfer_cache_async_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0 --local_comm_res '{"net_instance_id":"superpod1_1","endpoint_list":[{"protocol":"roce","comm_id":"1.0.0.2","placement":"host"}],"version":"1.3"}'
    ```
- Run the `hixl_transfer_backend` sample. This sample uses HIXL as the transport backend for LLM-DataDist, performing memory registration, connection establishment, and data transfer. The sample allocates memory and registers blocks. The decoder initiates a connection and pushes blocks. The prompt initiates a connection and pulls blocks.
  Run the sample program on both the Prompt and Decoder hosts. In the command, `device_id` specifies the device ID to be used, `role` specifies the cluster role, `local_host_ip` specifies the IP address of the local host, and `remote_host_ip` specifies the IP address of the peer host.
    ```
    # Prompt host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python hixl_transfer_backend_sample.py --device_id 0 --role p --local_host_ip 10.170.10.0 --remote_host_ip 10.170.10.1
    # Decoder host:
    GLOO_SOCKET_IFNAME=enp67s0f5 HCCL_INTRA_ROCE_ENABLE=1 python hixl_transfer_backend_sample.py --device_id 1 --role d --local_host_ip 10.170.10.1 --remote_host_ip 10.170.10.0
    ```
**Note:** **GLOO_SOCKET_IFNAME** is the local NIC name and can be queried using `ifconfig`; **HCCL_INTRA_ROCE_ENABLE=1** indicates that RoCE is used for communication.
