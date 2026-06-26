## Introduction

This project provides C++ and Python usage examples. Developers can refer to the corresponding implementations according to their specific requirements.  

## Directory Structure
```
├── examples
│   ├── cpp                # C++ samples
│   ├── python             # Python samples
│   ├── third_parties      # Integration samples with third‑party libraries
│   ├── README.md          # Sample documentation
```

## Development Samples

|  **Sample** |  **Description** |  **Language** |
|---|---|---|
| [cppSamples](cpp) | C++ samples| C++ |
| [pythonSamples](python) | Python samples| Python |
| [thirdPartiesSamples](third_parties) | HIXL integration samples with other communities| C++/Python |


## Environment Requirements

### 1. Hardware and Software Preparation
-   Chips: Atlas A3 training/inference products, Atlas 800I A2 inference products/A200I A2 Box heterogeneous components, Ascend 950PR/Ascend 950DT
-   Deploy the Ascend AI stack in the runtime environment by referring to [Environment Setup](../docs/en/build.md#environment-setup).

### 2. Device Connectivity Check
Before running the samples, use the [hccn_tool](https://support.huawei.com/enterprise/en/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference) provided in the driver package to verify **connectivity between two devices**. The following uses the A2 scenario as an example:
> In a container environment, if the `hccn_tool` command cannot be found, the possible cause is that the soft link `-v /usr/bin/hccn_tool:/usr/bin/hccn_tool` is not specified when the container is created. You can solve the problem using the following methods:
>
> Search for the `hccn_tool` executable file in the CANN driver package installation directory (`/usr/local/Ascend/driver/tools/hccn_tool` by default) and run `ln -s /usr/local/Ascend/driver/tools/hccn_tool /usr/bin/hccn_tool` to manually create a soft link.

- Step 1: Query the IP addresses of the required devices. The following uses an 8-device setup as an example:
```shell
for i in {0..7}; do hccn_tool -i $i -ip -g; done
```
- Step 2: Check the connectivity between two devices (such as device A and device B).
```shell
# Check whether device A can ping device B
hccn_tool -i ${device_id_a} -ping -g address ${ip_address_b}
# Check whether device B can ping device A
hccn_tool -i ${device_id_b} -ping -g address ${ip_address_a}
```
Here, `device_id` can be queried via `npu-smi info`, and `ip_address` is the the device IP address obtained in the previous step. For example:
```shell
hccn_tool -i 0 -ping -g address 10.10.10.1
hccn_tool -i 1 -ping -g address 10.10.10.0
```

If the output contains `recv time out seq`, the two devices are not connected. Try other devices.

> **Note**: The A3 environment uses a single-device dual-die architecture, where both dies share one OS. For example, `dev-os-0` contains `device-0` and `device-1`.
>
> Devices within the same single‑device dual‑die configuration are not interconnected. For instance, `device‑0` and `device‑1` cannot communicate, nor can `device‑2` and `device‑3`, and so on. When running samples in an A3 environment, ensure that the provided device IDs meet the connectivity requirements. 

- Step 3: Check the consistency of TLS certificate configurations across devices.
```shell
# Check the TLS status of each device
for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch
```
In the output, `tls switch[0](0:disable, 1:enable)` controls whether the TLS certificate is enabled. Ensure that all devices requiring connectivity have consistent TLS configurations.

Devices with TLS enabled cannot establish connections with devices where TLS is disabled. You are advised to disable TLS uniformly using the following command:
```shell
# Disabling the TLS certificate
for i in {0..7}; do hccn_tool -i $i -tls -s enable 0; done
```
**Note**: If you encounter `hccn_tool is busy, please try again`, check that no other processes are executing the command and try again.
