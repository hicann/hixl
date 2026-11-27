# Quick Start

<!-- md-trans-meta sourceCommit=df3b14504e1aa05d42a2e7708d83c859f02821d5 translatedAt=2026-11-19T01:01:42.861Z pushedAt=2026-11-19T01:04:51.894Z nl=2 -->

## Overall Development Process

**Overview**

For the following descriptions, inference frameworks are abstracted into these modules:

- Resource initialization module.
- KV Cache management module, including memory creation, allocation (PA scenarios), and destruction.
- Model inference module.
- Resource release module.

This section describes how to enable LLM-DataDist in an inference framework.

**Development Process**

1. Locate the framework's resource initialization module and call LLM-DataDist initialization and connection APIs during this phase.
2. Locate the inference framework's KV Cache management module and call LLM-DataDist KV Cache registration APIs to register user-allocated KV Cache memory.
3. The inference framework must separate the Prefill and Decode phases and deploy their inference scripts on different cluster nodes. Before Decode, receive Prefill output as input and use LLM-DataDist KV Cache transfer APIs to pull or push the KV Cache stored on Prefill.
4. Run the Prefill and Decode inference scripts separately.
5. Release LLM-DataDist resources in the framework's resource release module.

## Preparing the Environment

The following products are supported:

<!-- npu="910b" id1 -->
- Atlas A2 products: Only Atlas 800I A2 inference servers and A200I A2 Box heterogeneous components are supported. With HCCS within the Server, only D2D is supported.
<!-- end id1 -->
<!-- npu="A3" id2 -->
- Atlas A3 products: Host memory is unsupported as a remote Cache when HCCS is used.
<!-- end id2 -->
<!-- npu="950" id3 -->
- Ascend 950PR&950DT products use UB within a supernode and RoCE between supernodes.
<!-- end id3 -->

Install the driver, firmware, and CANN following [CANN Software Installation](https://www.hiascend.com/document/detail/en/CANNCommunityEdition/latest/softwareinst/instg/instg_0000.html?OS=openEuler&InstallType=netyum).

In heterogeneous scenarios, the receiver and sender must use the same MTU (maximum transmission unit) configuration. For details on querying and configuring the MTU, see [HCCN Tool API Reference](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference).

Use `hccn_tool` to query Device IP addresses and test inter-device connectivity. Devices across clusters must have RDMA connections to enable LLM-DataDist. For details, see [HCCN Tool API Reference](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference). Common commands are listed below.

|Command|Scenarios|
|--|--|
|hccn_tool [-i %d] -link -g|Queries the specified Device network port Link status. `-i` specifies the Device. Example: `hccn_tool -i 0 -link -g`.|
|hccn_tool [-i %d] -ip -g|Queries the IP address and subnet mask. `-i` specifies the Device. Example: `hccn_tool -i 0 -ip -g`.|
|hccn_tool [-i %d] -ip -inet6 -g|Queries the IPv6 address and subnet mask. `-i` specifies the Device. Example: `hccn_tool -i 0 -ip -inet6 -g`.|
|hccn_tool [-i %d] -ping -g [address %s ]|Queries ping results from a Device to a destination. `-i` specifies a Device on the current server, and `address` specifies the destination. Example: `hccn_tool -i 0 -ping -g address 192.168.2.1`.|

The following environment variables are also used. See [HCCL Environment Variables](https://gitcode.com/cann/hccl/blob/9.3.0-beta.2/docs/en/user_guide/hccl_env/README.md).

|Name|Scenarios|
|--|--|
|HCCL_RDMA_TC, HCCL_RDMA_SL|If the customer has planned the parameter-plane network and specified traffic types and priorities for various services, use these two variables to set the traffic class and priority of parameter-plane collective communication to match that plan.|
|HCCL_RDMA_RETRY_CNT, HCCL_RDMA_TIMEOUT|These specify RDMA hardware retry count and retry timeout. Excessive values make the system insensitive to network anomalies and unable to detect network faults. If the values are too small, NIC hardware may fail to mask transient network interruptions, directly disrupting the application. Choose appropriate values based on your network conditions. For example, configure these values based on the duration range of most transient interruptions. To reduce the impact of network jitter, we recommend configuring HCCL_RDMA_TIMEOUT using the following formula: HCCL_RDMA_TIMEOUT=log2(pull KV timeout * 10^6 / (HCCL_RDMA_RETRY_CNT + 1) / 4.096), rounded up. When both the pull KV timeout and HCCL_RDMA_RETRY_CNT have their default values, a value of 15 is recommended for HCCL_RDMA_TIMEOUT.|
|HCCL_INTRA_ROCE_ENABLE|Controls whether devices within a Server communicate over RoCE loopback.|
|AUTO_USE_UC_MEMORY|Controls whether operators may move data without passing through L2 Cache. If unset before using LLM-DataDist, LLM-DataDist sets it to 0, requiring all operator data movement to pass through L2 Cache.|

### Supporting Multiple Python Versions (On-Demand Compilation)

The `hixl` Python package is built around a pybind11 extension module. The precompiled extension can be loaded directly only with the Python version used to build it. For other Python versions from 3.9 onward, the first `import hixl` automatically compiles the bundled source on demand and caches the result. Initial compilation takes approximately 10~20 seconds (measured at about 13 seconds on x86 and 16 seconds on arm). A second import using the cache takes about 0.2 seconds; subsequent imports reuse the cache directly.

On-demand compilation requires the following dependencies (needed only on the first import with a Python version other than the precompiled version):

|Dependency|Description|
|--|--|
|System cmake|Install with `apt install cmake` or `yum install cmake` (cmake installed through pip does not work).|
|C++ compiler and make|Install with `apt install build-essential` or `yum install gcc-c++ make`. If missing, the cmake configure/build stage reports an error; install the dependencies indicated by the error message.|
|Python development headers|Install with `apt install python3-dev` or `yum install python3-devel`.|
|pybind11 (pip package, version 2.13.6 or later)|Install with `python -m pip install "pybind11>=2.13.6"`.|

The HIXL software package must also be installed with the corresponding CANN environment, which provides the public `hixl` headers and runtime libraries.

Compiled output cache location: If the package directory is writable, output is written back to `site-packages/hixl/` and automatically invalidated and rebuilt when the package is upgraded. With `--pylocal y` installation, the upgrade/uninstall scripts automatically remove the output written back, metadata, and files left by the old package. For user-managed installations such as the default `pip --user`, files may remain after uninstalling, as is customary in the Python ecosystem (like `__pycache__`); you can manually delete the `hixl` directory. If the package directory is read-only, output is cached in `~/.cache/cann/hixl/`. This cache is automatically removed when the run package is upgraded or uninstalled. You can also manually delete the entire cache directory; the next import automatically recompiles the extension.

Concurrent first imports by multiple processes are automatically serialized: only one process compiles at a time, while the others wait and then reuse the compiled result.

## Complete Example

Using the transformers LLAMA model, this example highlights script changes before and after PD disaggregation, showing how to adapt a colocated script to a PD-disaggregated script. Visit [Gitee](https://gitee.com/ascend/torchair/tree/master#version_match), download the sample package matching the version compatibility table, and obtain examples from `npu_tuned_model/llm/llama/benchmark/pd_separate`. The examples separate full and incremental models and run them on different cluster nodes.

The service layer schedules the disaggregated scripts through the inference process as follows.

1. When a user request arrives, the service layer schedules it on the Prompt cluster for full inference, then sends information needed by the incremental script to the Decode cluster node. The Prompt cluster node can then accept another request.
2. The Decode cluster receives the corresponding request information, pulls the corresponding request's KV Cache already computed on the Prompt cluster nodes, batches requests according to the incremental model's batch size, and runs incremental inference.
3. When a request on the Decode cluster completes and frees a batch slot, receive a new request from the Prompt cluster and repeat steps 2 and 3.
4. The Prompt cluster repeats step 1 and the Decode cluster repeats steps 2 and 3 until the application finishes, then both clusters exit.

For more examples, visit [GitCode](https://gitcode.com/cann/hixl), select the matching version, and obtain examples from `examples/python`.
