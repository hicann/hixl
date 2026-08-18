# hixl_tool User Guide

[中文](./readme.md) | [English](./readme_en.md)

## 1. Overview

`hixl_tool` is a command-line utility shipped with HIXL. It helps configure network communication resources for distributed AI scenarios. The tool provides two subcommands:

| Subcommand | Purpose | Default output |
|------------|---------|----------------|
| `host_route`       | Export the host/CPU and NPU EID inventory of this machine (optional offline export) | `host_route.json` |
| `local_comm_res`   | Generate a LocalCommRes configuration file for each NPU | `local_comm_res_<device_id>_<phy_id>.json` |

```
hixl_tool <subcommand> [options]
```

Both subcommands rely on the engine auto-generation path. `route_data` is collected automatically through **DSMI + urma_admin + DCMI** and **no longer depends on the legacy `route.conf` or procfs**.

---

## 2. Build and Artifacts

### 2.1 Build

Prerequisites: Ascend CANN Toolkit (>= 9.0.0) is installed and the environment variables are loaded. For the full build procedure, see [docs/en/build.md](../../../docs/en/build.md).

```bash
# Load CANN environment variables (use the default path when ASCEND_HOME_PATH is unset)
source ${ASCEND_HOME_PATH}/set_env.sh

# Build: --examples also sets ENABLE_HIXL_TOOL=ON and compiles hixl_tool
bash build.sh --examples
```

- Source code lives in `scripts/tools/hixl_tool/`. It is built by the root `CMakeLists.txt` (`ENABLE_HIXL_TOOL`, default OFF) and `scripts/tools/hixl_tool/CMakeLists.txt`.
- `build.sh --examples` compiles examples/benchmarks and turns `ENABLE_HIXL_TOOL` on. You can also enable the tool alone with `-D ENABLE_HIXL_TOOL=ON` when configuring CMake by hand.
- `hixl_tool` is a build artifact only. It is **not** packed into the `cann-hixl_*.run` installer. Use it from the build directory or copy it yourself for distribution.

### 2.2 Artifact locations

| Artifact | Path |
|----------|------|
| `hixl_tool` executable | `build/scripts/tools/hixl_tool/hixl_tool` |
| Dependency `libcann_hixl.so` | `build/src/hixl/libcann_hixl.so` |

> These paths are under the in-tree build directory `build/` at the repository root, not the install directory `build_out/`.

### 2.3 Run

`hixl_tool` dynamically links `libcann_hixl.so`. Make sure the shared library can be loaded before you run the tool. Use either of the following:

- Option 1 (use the build artifacts directly):

  ```bash
  # Add build/src/hixl to the library search path so libcann_hixl.so can be found
  export LD_LIBRARY_PATH=${PWD}/build/src/hixl:${LD_LIBRARY_PATH}
  ./build/scripts/tools/hixl_tool/hixl_tool --help
  ```

- Option 2 (a matching HIXL run package is already installed): `libcann_hixl.so` is installed under the CANN lib64 directory. After you load the CANN environment, you can run the built `hixl_tool` binary directly.

Verify that the tool works:

```bash
./hixl_tool --help
```

> `host_route` and `local_comm_res` need access to DAVINCI devices. When you run in a container, the topology directory must also be visible (see [5. Using the artifacts](#5-using-the-artifacts)).

---

## 3. Tool 1: host_route (EID inventory export)

### 3.1 Purpose

Export the host/CPU-side EID (2-port PG) and NPU-side EID of every local NPU into `host_route.json`. This file is an **optional offline EID inventory**.

> `local_comm_res` no longer depends on this artifact. Use this subcommand only for offline inspection or record keeping.

### 3.2 Usage

```
hixl_tool host_route [--output <dir>]
```

| Option | Description | Default |
|--------|-------------|---------|
| `--output <dir>` | Output directory | `/etc/` |

### 3.3 Output format

Output file: `{output}/host_route.json`

```json
{
  "device_num": 8,
  "devices": [
    {
      "device_id": 0,
      "local_eid": "00000000000000000000000000mm00pg",
      "remote_eid": "00000000000000000000000000mm00np"
    }
  ]
}
```

Field description:

- `device_num`: total number of devices
- `devices[].device_id`: NPU device ID
- `devices[].local_eid`: host/CPU-side EID (2-port PG)
- `devices[].remote_eid`: NPU-side EID

### 3.4 Execution flow

1. Enumerate every local NPU (`aclrtGetDeviceCount` + `aclrtGetPhyDevIdByUserDevId`)
2. Read `mainboard_id` from the first NPU to determine the Server/PoD product form
3. Call `GenerateRouteDataViaDsmi` for each NPU group to generate `route_data`
4. Serialize and write `host_route.json` (file mode 0600)

### 3.5 Examples

```bash
# Default output: /etc/host_route.json
./hixl_tool host_route

# Custom output directory
./hixl_tool host_route --output /home/hixl
```

---

## 4. Tool 2: local_comm_res (LocalCommRes generation)

### 4.1 Purpose

Generate a `LocalCommRes` (local communication resource) JSON file for each target NPU. The generated files can be passed directly as the HIXL API `LocalCommRes` option, or used as Mooncake communication-resource configuration for later connection setup and transfer.

### 4.2 Usage

```
hixl_tool local_comm_res [options]
```

| Option | Description | Default |
|--------|-------------|---------|
| `--topo_file_path <path>` | Hardware topology JSON path (optional) | Default topology when omitted |
| `--device_id <id1,id2,...>` | Target user device IDs, comma-separated | Auto-detect all devices |
| `--protocol_desc <desc>` | Protocol description in `protocol:placement` form, comma-separated | ScaleOut selected by InterconType + `ub_ctp:device` |
| `--output <dir>` | Output directory | `/etc/` |
| `--file_name_prefix <prefix>` | Output file-name prefix | `local_comm_res` |

### 4.3 Output format

One file per device: `{output}/{prefix}_{device_id}_{phy_id}.json`

```json
{
  "version": "1.3",
  "net_instance_id": "superpod_1",
  "endpoint_list": [
    {
      "protocol": "uboe",
      "comm_id": "192.168.100.205",
      "placement": "device",
      "plane": "plane_pg_0",
      "net_instance_id": "superpod_1"
    },
    {
      "protocol": "ub_ctp",
      "comm_id": "00000000000000000000000000mm00e0",
      "placement": "device",
      "plane": "plane_pg_0",
      "net_instance_id": "superpod_1"
    },
    {
      "protocol": "ub_ctp",
      "comm_id": "00000000000000000000000000mm00e1",
      "placement": "host",
      "plane": "plane_pg_0",
      "net_instance_id": "superpod_1"
    }
  ]
}
```

Field description:

- `version`: artifact format version (`1.3`)
- `net_instance_id`: network instance ID
- `endpoint_list`: endpoint list
  - `protocol`: protocol type (`ub_ctp` / `uboe` / `ub_rtp`)
  - `comm_id`: communication identifier (EID or IP)
  - `placement`: location (`device` / `host`)
  - `plane`: plane (`plane_pg_0` / `plane_pg_1`)
  - `net_instance_id`: owning network instance

### 4.4 `--protocol_desc` values

The format is `protocol:placement`. Separate multiple entries with a comma `,`:

| Value | Meaning |
|-------|---------|
| `ub_ctp:device` | Generate device-side ub_ctp endpoints |
| `ub_ctp:host` | Generate host-side ub_ctp endpoints |
| `uboe:device` | Generate UBoE ScaleOut endpoints |
| `ub_rtp:device` | Generate UB_RTP (UBG) ScaleOut endpoints |

Constraints:

- `uboe` and `ub_rtp` are **mutually exclusive**
- `uboe` / `ub_rtp` support only the `device` placement
- When `--protocol_desc` is omitted: ScaleOut is selected by `InterconType`, and `ub_ctp:device` is generated
- Host-side ub_ctp edges require an explicit `ub_ctp:host` entry

### 4.5 Execution flow

1. Parse arguments and determine the target device list
2. For each device: `aclrtSetDevice` to occupy the device → build `HixlOptions` (including `protocol_desc`) → call `EndpointGenerator::AutoGenEndpointList` (dispatch by SoC type; A5 uses the default or custom topology) → assemble LocalCommRes → write JSON → `aclrtResetDevice` to release the device
3. The engine generates `route_data` internally through DSMI + urma_admin + DCMI. **`host_route.json` is not required**

### 4.6 Examples

```bash
# All devices, auto mode (ScaleOut by InterconType + ub_ctp:device)
./hixl_tool local_comm_res --output /home/hixl

# Selected devices + explicit protocols: ub_rtp ScaleOut + device/host ub_ctp
./hixl_tool local_comm_res \
  --output /home/hixl \
  --device_id 0,1 \
  --protocol_desc ub_rtp:device,ub_ctp:device,ub_ctp:host

# Custom topology file
./hixl_tool local_comm_res \
  --topo_file_path /usr/local/Ascend/driver/topo/950/atlas_950_1.json \
  --output /home/hixl \
  --protocol_desc uboe:device,ub_ctp:device
```

---

## 5. Using the artifacts

The generated `LocalCommRes` JSON files can be used as follows:

1. **HIXL API**: pass the JSON content directly as the `LocalCommRes` option (`OPTION_LOCAL_COMM_RES`)
2. **Mooncake**: configure the generated file path as the LocalCommRes source for Mooncake communication resources
3. **Offline `host_route.json`**: keep it as an EID inventory reference

> In a container, map the topology directory: `/usr/local/Ascend/driver/topo/950/`
