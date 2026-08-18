# hixl_tool 使用指南

[中文](./readme.md) | [English](./readme_en.md)

## 1. 概述

`hixl_tool` 是 HIXL 附带的命令行工具，用于辅助配置分布式 AI 场景下的网络通信资源。工具提供两个子命令：

| 子命令 | 功能 | 默认产物 |
|--------|------|----------|
| `host_route`       | 导出本机 Host/CPU 与 NPU 的 EID 清单（可选离线导出） | `host_route.json` |
| `local_comm_res`   | 为每个 NPU 生成 LocalCommRes 配置文件 | `local_comm_res_<device_id>_<phy_id>.json` |

```
hixl_tool <subcommand> [options]
```

两个子命令均基于引擎自动生成能力，`route_data` 通过 **DSMI + urma_admin + DCMI** 自动采集，**不再依赖传统 `route.conf` 或 procfs**。

---

## 2. 编译与产物

### 2.1 编译

前置条件：已安装 Ascend CANN Toolkit（>= 9.0.0）并已加载环境变量，完整构建步骤参考 [docs/zh/build.md](../../../docs/zh/build.md)。

```bash
# 加载 CANN 环境变量（未设置 ASCEND_HOME_PATH 时用默认路径）
source ${ASCEND_HOME_PATH}/set_env.sh

# 编译：--examples 会同时置位 ENABLE_HIXL_TOOL=ON，触发 hixl_tool 编译
bash build.sh --examples
```

- `hixl_tool` 源码位于 `scripts/tools/hixl_tool/`，由根目录 `CMakeLists.txt`（`ENABLE_HIXL_TOOL`，默认 OFF）与 `scripts/tools/hixl_tool/CMakeLists.txt` 构建。
- `build.sh --examples` 在编译 examples/benchmarks 的同时将 `ENABLE_HIXL_TOOL` 置为 `ON`；手工配置 CMake 时也可通过 `-D ENABLE_HIXL_TOOL=ON` 单独开启该工具编译。
- `hixl_tool` 仅生成编译产物，**不会**被打入 `cann-hixl_*.run` 安装包，请在构建目录直接使用或自行拷贝分发。

### 2.2 产物位置

| 产物 | 位置 |
|------|------|
| `hixl_tool` 可执行文件 | `build/scripts/tools/hixl_tool/hixl_tool` |
| 依赖库 `libcann_hixl.so` | `build/src/hixl/libcann_hixl.so` |

> 说明：以上均为仓库根目录下内部构建目录 `build/` 中的路径，而非安装目录 `build_out/`。

### 2.3 运行

`hixl_tool` 动态链接 `libcann_hixl.so`，运行前需确保该共享库可被加载，任选其一：

- 方式一（直接使用构建产物）：

  ```bash
  # 将 build/src/hixl 加入库搜索路径，避免运行时提示 libcann_hixl.so 找不到
  export LD_LIBRARY_PATH=${PWD}/build/src/hixl:${LD_LIBRARY_PATH}
  ./build/scripts/tools/hixl_tool/hixl_tool --help
  ```

- 方式二（已安装匹配版本的 HIXL run 包）：`libcann_hixl.so` 随包安装在 CANN 的 lib64 目录，加载 CANN 环境变量后即可直接运行构建出的 `hixl_tool` 二进制。

验证工具可用：

```bash
./hixl_tool --help
```

> 运行 `host_route` / `local_comm_res` 需要可访问 DAVINCI 设备；若以容器方式运行，还需保证拓扑目录可见（见 [5. 使用产物](#5-使用产物)）。

---

## 3. 工具一：host_route（EID 清单导出）

### 3.1 用途

导出本机全部 NPU 的 Host/CPU 侧 EID（2 口 PG）与 NPU 侧 EID，写入 `host_route.json`，作为**可选的离线 EID 清单**。

> 说明：`local_comm_res` 已不再依赖本产物；本子命令仅作离线巡检/备案用途。

### 3.2 用法

```
hixl_tool host_route [--output <dir>]
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--output <dir>` | 输出目录 | `/etc/` |

### 3.3 产物格式

输出文件：`{output}/host_route.json`

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

字段说明：

- `device_num`：设备总数
- `devices[].device_id`：NPU 设备 ID
- `devices[].local_eid`：Host/CPU 侧 EID（2 口 PG）
- `devices[].remote_eid`：NPU 侧 EID

### 3.4 执行流程

1. 枚举本机全部 NPU（`aclrtGetDeviceCount` + `aclrtGetPhyDevIdByUserDevId`）
2. 取首个 NPU 的 `mainboard_id` 判断 Server/PoD 产品形态
3. 对每组 NPU 调用 `GenerateRouteDataViaDsmi` 生成 route_data
4. 序列化并写 `host_route.json`（文件权限 0600）

### 3.5 示例

```bash
# 默认输出到 /etc/host_route.json
./hixl_tool host_route

# 指定输出目录
./hixl_tool host_route --output /home/hixl
```

---

## 4. 工具二：local_comm_res（LocalCommRes 生成）

### 4.1 用途

为每个目标 NPU 生成 `LocalCommRes`（本地通信资源）JSON 文件。生成的产物可直接作为 HIXL API 的 `LocalCommRes` 选项值，或作为 Mooncake 的通信资源配置，供后续建链与传输使用。

### 4.2 用法

```
hixl_tool local_comm_res [options]
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--topo_file_path <path>` | 硬件拓扑 JSON 文件路径（可选） | 缺省走默认 topo |
| `--device_id <id1,id2,...>` | 目标用户设备 ID，逗号分隔 | 自动识别全部设备 |
| `--protocol_desc <desc>` | 协议描述，`protocol:placement` 格式，逗号分隔 | ScaleOut 按 InterconType 自动选择 + `ub_ctp:device` |
| `--output <dir>` | 输出目录 | `/etc/` |
| `--file_name_prefix <prefix>` | 输出文件名前缀 | `local_comm_res` |

### 4.3 产物格式

每个 device 输出一个文件：`{output}/{prefix}_{device_id}_{phy_id}.json`

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

字段说明：

- `version`：产物格式版本号（`1.3`）
- `net_instance_id`：网络实例 ID
- `endpoint_list`：端点列表
  - `protocol`：协议类型（`ub_ctp` / `uboe` / `ub_rtp`）
  - `comm_id`：通信标识（EID 或 IP）
  - `placement`：位置（`device` / `host`）
  - `plane`：平面（`plane_pg_0` / `plane_pg_1`）
  - `net_instance_id`：所属网络实例

### 4.4 `--protocol_desc` 可取值

格式为 `protocol:placement`，多个用英文逗号 `,` 分隔：

| 取值 | 含义 |
|------|------|
| `ub_ctp:device` | 生成 device 侧 ub_ctp 端点 |
| `ub_ctp:host` | 生成 host 侧 ub_ctp 端点 |
| `uboe:device` | 生成 UBoE ScaleOut 端点 |
| `ub_rtp:device` | 生成 UB_RTP（UBG）ScaleOut 端点 |

约束：

- `uboe` 与 `ub_rtp` **互斥**，不能同时出现
- `uboe` / `ub_rtp` 仅支持 `device` placement
- 不传 `--protocol_desc` 时：ScaleOut 按 `InterconType` 自动选择 + 生成 `ub_ctp:device`
- 需要 host 侧 ub_ctp 边时，必须显式配置 `ub_ctp:host`

### 4.5 执行流程

1. 解析参数，确定目标设备列表
2. 对每个 device：`aclrtSetDevice` 占用设备 → 构造 `HixlOptions`（含 `protocol_desc`）→ 调用 `EndpointGenerator::AutoGenEndpointList`（按 SoC 类型分发，A5 走默认 topo/自定义 topo）→ 组装 LocalCommRes → 写 JSON → `aclrtResetDevice` 释放设备
3. `route_data` 在引擎内部通过 DSMI + urma_admin + DCMI 自动生成，**无需 `host_route.json`**

### 4.6 示例

```bash
# 全部设备，auto 模式（ScaleOut 按 InterconType + ub_ctp:device）
./hixl_tool local_comm_res --output /home/hixl

# 指定设备 + 显式协议：ub_rtp ScaleOut + device/host 侧 ub_ctp
./hixl_tool local_comm_res \
  --output /home/hixl \
  --device_id 0,1 \
  --protocol_desc ub_rtp:device,ub_ctp:device,ub_ctp:host

# 指定拓扑文件
./hixl_tool local_comm_res \
  --topo_file_path /usr/local/Ascend/driver/topo/950/atlas_950_1.json \
  --output /home/hixl \
  --protocol_desc uboe:device,ub_ctp:device
```

---

## 5. 使用产物

生成的 `LocalCommRes` JSON 文件可用于：

1. **HIXL API**：将 JSON 内容直接作为 `LocalCommRes` 选项值传入（`OPTION_LOCAL_COMM_RES`）
2. **Mooncake**：将生成的文件路径配置为 Mooncake 通信资源的 LocalCommRes 来源
3. **离线导出的 host_route.json**：作为 EID 清单备案参考

> 容器场景需保证拓扑目录映射：`/usr/local/Ascend/driver/topo/950/`
