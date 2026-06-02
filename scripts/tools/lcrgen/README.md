# LocalCommRes Generator Tool

本工具用于生成本地通信资源（LocalCommRes）信息，并以 JSON 格式输出到文件。

## 功能说明

该工具通过用户输入的npu逻辑id，自动生成以下信息：
- 版本号（version）
- 网络实例 ID（net_instance_id）
- 端点列表（endpoint_list），包含所有通信边的配置信息

生成的 JSON 文件将保存在当前执行目录下，文件名为 `local_comm_res_{device_id}.json`。

## 编译

在项目根目录下执行：

```bash
bash build.sh --examples
```

## 运行
进入build目录下，找工具的同名目录，其中有编译生成的可执行文件。
```bash
./lcrgen <npu_id>
```

### 参数说明

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| npu_id | int | 是 | NPU 逻辑设备 ID（从 0 开始）|

### 示例

```bash
# 查看帮助信息
./lcrgen

# 生成 device 0 的 LocalCommRes 信息
./lcrgen 0

# 生成 device 1 的 LocalCommRes 信息
./lcrgen 1
```

## 输出说明

### 控制台输出

工具会在控制台打印生成的 LocalCommRes 信息，包括：
- 版本号
- 网络实例 ID
- 端点列表大小
- 每个端点的详细信息

### JSON 文件输出

在当前执行目录下生成 JSON 文件，文件名格式：`local_comm_res_{npu_id}.json`

**JSON 文件格式：**

```json
{
  "version": "1.3",
  "net_instance_id": "superpod_xxx",
  "endpoint_list": [
    {
      "protocol": "ub_ctp",
      "comm_id": "000000000000004000100000dfdf1672",
      "placement": "host",
      "plane": "plane_pg_0"
    },
    {
      "protocol": "ub_ctp",
      "comm_id": "000000000000004000100000dfdf1672",
      "placement": "host",
      "dst_eid": "0x000000000000008000100000dfdf1b01"
    }
  ]
}
```

**字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| version | string | 版本号，默认 "1.3" |
| net_instance_id | string | 网络实例 ID |
| endpoint_list | array | 端点列表 |
| protocol | string | 通信协议，如 "ub_ctp" |
| comm_id | string | 通信标识符 |
| placement | string | 位置信息："host" 或 "device" |
| plane | string | 平面标识（如 plane_pg_0, plane_pg_1），可选 |
| dst_eid | string | 目标 EID，可选 |

## 依赖

- ACL（Ascend Computing Language）运行时库
- CANN（Compute Architecture for Neural Networks）软件栈
- NPU 驱动和固件

## 注意事项

1. 运行前请确保 NPU 设备可用且已正确配置
2. 需要有足够的权限访问 NPU 设备, 
3. JSON 文件会覆盖同名的已有文件
4. 端点列表可能为空（当拓扑信息不完整时）
5. 需要有root权限执行可执行文件
