# HIXL E2E Smoke Tests

端到端冒烟测试套件，验证 HIXL 单边通信库在不同部署模式下的传输功能和数据完整性。

## 前置条件

- **硬件**: 至少 2 张 NPU 卡（Atlas A2/A3）
- **驱动**: Ascend NPU Driver >= 25.5.0
- **CANN**: CANN >= 9.1.0
- **Python**: Python 3.10+
- **依赖包**:
  ```bash
  pip install pytest torch==2.13.0+cpu --index-url https://download.pytorch.org/whl/cpu
  pip install torch-npu==2.13.0rc1
  ```

## 测试场景

### 1. Real-Real 模式 (`test_real_real.py`)

模拟两个独立的 RealClient（类似 vLLM 的 Prefill 和 Decode 节点）。

**测试用例**:
- `test_normal_full_flow_d2rd_batch_read_write`: D2D 跨设备批量读写
- `test_normal_full_flow_d2rh_batch_read_write`: D2RH 跨设备批量读写

**架构**:
```
Client 进程 (devices [0,1,2,3])     Server 进程 (devices [1,2,3,0])
├─ engine[0] (dev 0) ──────────────> engine[0] (dev 1)
├─ engine[1] (dev 1) ──────────────> engine[1] (dev 2)
├─ engine[2] (dev 2) ──────────────> engine[2] (dev 3)
└─ engine[3] (dev 3) ──────────────> engine[3] (dev 0)
```

**数据流**:
1. Client 填充 `0xDD`，Server 填充 `0xCC`
2. Client WRITE `0xDD` → Server
3. Client 清零本地内存
4. Client READ ← Server（期望 `0xDD`）

### 2. Standalone 同主机模式 (`test_standalone_same_host.py`)

模拟 Store Service + App 同主机部署。

**测试用例**:
- `test_normal_full_flow_same_host_d2rh_write`: D2RH 同主机写入

**架构**:
```
App 进程 (devices [0,1,2,3])          Store 进程 (devices [1,2,3,0])
├─ engine[0] (dev 0) ──────────────> engine[0] (dev 1)
├─ engine[1] (dev 1) ──────────────> engine[1] (dev 2)
├─ engine[2] (dev 2) ──────────────> engine[2] (dev 3)
└─ engine[3] (dev 3) ──────────────> engine[3] (dev 0)
                                      └─ 共享 host memory (base_addr)
```

**数据流**:
1. App 填充 `0xFF`
2. App[i] WRITE `0xFF` → Store host memory offset `i * MEM_SIZE`
3. App 清零本地内存
4. App[i] READ ← Store host memory offset `i * MEM_SIZE`（期望 `0xFF`）

### 3. Dummy-Real 共享内存模式 (`test_dummy_real_shared_mem.py`)

模拟 Mooncake dummy-real 部署模式，验证跨进程 device memory 共享。

**测试用例**:
- `test_normal_full_flow_d2rd_d2rh_read`: D2RD + D2RH 完整流程

**架构**:
```
Local Dummy (devices [0,1,2,3])     Local Real (devices [0,1,2,3])
├─ 分配 device memory              ├─ 导入 IPC key
├─ 填充 0xAA                       ├─ 创建 engines
└─ 导出 IPC key                    └─ 注册 device + host memory
                                            │
                                            ▼
Remote Real (devices [1,2,3,0])     Remote Dummy (devices [1,2,3,0])
├─ 导入 IPC key                    ├─ 分配 device memory
├─ 创建 engines                    ├─ 填充 0xBB
├─ 注册 device + host memory       └─ 导出 IPC key
└─ 验证 D2RH WRITE 结果
```

**数据流**:
1. D2RD WRITE: Local device (0xAA) → Remote device
2. D2RD READ: Remote device → Local device（期望 0xAA）
3. D2RH WRITE: Local device (0xAA) → Remote host
4. D2RH READ: Remote host → Local device（期望 0xAA）

### 4. 大规模连接与重连 (`test_scale_reconnect.py`)

测试大规模多链路传输和设备抖动重连。

**测试用例**:
- `test_normal_flow_multi_links`: 多设备 × 250 链路/设备 批量传输
- `test_flapping_dev_reconnect`: 设备抖动场景下的重连能力

**架构**:
```
Client 进程 (devices [0,1,2,3])     Server 进程 (devices [1,2,3,0])
├─ engine[0] (dev 0) ──┐            ├─ engine[0] (dev 1)
│   ├─ link 0          │            │   ├─ link 0
│   ├─ link 1          │            │   ├─ link 1
│   └─ ...             │            │   └─ ...
│   └─ link 249        │            │   └─ link 249
├─ engine[1] (dev 1) ──┤            ├─ engine[1] (dev 2)
├─ engine[2] (dev 2) ──┤            ├─ engine[2] (dev 3)
└─ engine[3] (dev 3) ──┘            └─ engine[3] (dev 0)
```

## 运行测试

### 基本用法

```bash
# 设置环境变量
export ASCEND_RT_VISIBLE_DEVICES=0,1,2,3
source /usr/local/Ascend/cann/set_env.sh

# 运行所有测试
python3 -m pytest tests/e2e/ -v

# 运行单个测试文件
python3 -m pytest tests/e2e/test_real_real.py -v

# 显示详细输出（包括 print）
python3 -m pytest tests/e2e/test_real_real.py -v -s
```

### 环境变量

| 变量名 | 默认值 | 说明 |
|--------|--------|------|
| `ASCEND_RT_VISIBLE_DEVICES` | `0,1,2,3` | 可见的 NPU 设备列表 |
| `HIXL_E2E_MIN_NPU` | `2` | 最少需要的 NPU 数量 |
| `HIXL_E2E_LINKS_PER_DEV` | `250` | 每个设备的逻辑链路数（scale-reconnect） |
| `HIXL_E2E_TRANSFER_SIZE` | `4096` | 每个链路的传输大小（scale-reconnect） |
| `HIXL_E2E_REGISTER_SIZE` | `268435456` (256MB) | 注册的内存大小（scale-reconnect） |

### 示例

```bash
# 使用 4 张卡运行测试
export ASCEND_RT_VISIBLE_DEVICES=0,1,2,3
python3 -m pytest tests/e2e/ -v

# 自定义链路参数
export HIXL_E2E_LINKS_PER_DEV=500
export HIXL_E2E_TRANSFER_SIZE=8192
python3 -m pytest tests/e2e/test_scale_reconnect.py -v

# 跳过 NPU 数量检查（调试用）
export HIXL_E2E_MIN_NPU=0
python3 -m pytest tests/e2e/test_real_real.py -v
```

## 测试架构

### 设备分配策略

使用 `get_device_lists()` 实现错位设备分配：
- **Client/App**: 使用 `[0, 1, 2, 3]`
- **Server/Store**: 使用 `[1, 2, 3, 0]`（错位 +1）

确保 `client[i]` 和 `server[i]` 在不同的物理设备上，避免资源冲突。

### 端口分配

使用 `get_port()` 生成唯一的监听端口：
```
BASE_PORT = 39000
port = BASE_PORT + scenario_idx * 100 + role_offset + dev_id + engine_offset
```

**示例**（SCENARIO_IDX=3, dev_id=0）:
- `server` → 39300
- `client` → 39400
- `server_fabric` → 39500
- `client_fabric` → 39600

### 内存管理

- **Device Memory**: 使用 `torch.zeros(..., device="npu")` 分配
- **Host Memory**: 使用 `torch.empty(...).pin_memory()` 分配页对齐内存
- **IPC 共享**: 使用 `acl.rt.ipc_mem_get_export_key` / `ipc_mem_import_by_key` 跨进程共享

### 资源清理顺序

1. **Client 端**: `disconnect` → `deregister` → `finalize`
2. **Server 端**: `deregister` → `finalize`

## 调试技巧

### 查看详细日志

```bash
# 启用 console logging
python3 -m pytest tests/e2e/test_real_real.py -v -s
```

### 检查 HIXL 日志

```bash
# 设置日志级别
export ASCEND_GLOBAL_LOG_LEVEL=1  # 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR

# 设置日志路径
export ASCEND_PROCESS_LOG_PATH=/tmp/hixl_logs
mkdir -p /tmp/hixl_logs

# 运行测试
python3 -m pytest tests/e2e/test_real_real.py -v

# 查看日志
ls /tmp/hixl_logs/plog-*.log
```

### 常见问题

**Q: 测试跳过，提示 "Need >= 2 NPUs"**
```bash
# 检查 NPU 数量
npu-smi info -l | grep "NPU"

# 设置环境变量覆盖
export HIXL_E2E_MIN_NPU=0
```

**Q: 连接超时**
```bash
# 检查端口是否被占用
netstat -tlnp | grep 39

# 增加超时时间（修改代码中的 CONNECT_TIMEOUT_MS）
```

**Q: 数据校验失败**
```bash
# 检查日志中的 WRITE/READ 返回值
# 确认设备分配是否正确（client[i] 和 server[i] 应在不同物理设备）
```

## 文件结构

```
tests/e2e/
├── __init__.py                      # 包初始化文件
├── conftest.py                      # pytest 配置和 fixtures
├── utils.py                         # 公共工具函数
├── test_real_real.py                # Real-Real 模式测试
├── test_standalone_same_host.py     # Standalone 同主机模式测试
├── test_dummy_real_shared_mem.py    # Dummy-Real 共享内存模式测试
├── test_scale_reconnect.py          # 大规模连接与重连测试
└── README.md                        # 本文档

pytest_e2e.ini                       # pytest e2e 配置文件（仓库根目录）
```

## 贡献指南

1. 新增测试用例应遵循现有模式
2. 使用 `logger.info(...)` 输出关键步骤日志
3. 使用 `result_queue` 传递 worker 结果
4. 确保资源清理顺序正确：`disconnect` → `deregister` → `finalize`
5. 运行 `ruff check` 和 `ruff format` 检查代码风格
