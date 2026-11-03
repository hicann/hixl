# HIXL E2E Smoke Tests

End-to-end smoke test suite for validating HIXL (Huawei Xfer Library) transport functionality and data integrity across different deployment modes.

## Prerequisites

- **Hardware**: At least 2 visible NPU cards. Product support depends on the selected protocol and test scenario; Atlas A2/A3 scenarios and the VMM scenario on Ascend 950PR/Ascend 950DT must meet their respective runtime requirements
- **Driver**: Ascend NPU Driver >= 25.5.0
- **CANN**: CANN >= 9.1.0
- **Python**: Python 3.10+
- **Dependencies**:

  The following commands only demonstrate dependency installation with Python 3.11 and CANN 9.1.0. Select compatible PyTorch and TorchNPU versions for your Python version, CANN version, and CPU architecture using the [TorchNPU Compatibility Guide](https://gitcode.com/Ascend/pytorch/blob/master/COMPATIBILITY.en.md), and replace the version numbers in the example commands accordingly.

  ```bash
  pip install pytest
  pip3 install torch==2.10.0 --index-url https://download.pytorch.org/whl/cpu
  pip3 install torch-npu==2.10.0.post4
  ```

## Test Scenarios

### 1. Real-Real Mode (`test_real_real.py`)

Simulates two independent RealClients (similar to vLLM Prefill and Decode nodes).

**Test Cases**:
- `test_normal_full_flow_d2rd_batch_read_write`: D2D cross-device batch read/write
- `test_normal_full_flow_d2rh_batch_read_write`: D2RH cross-device batch read/write

**Architecture**:
```
Client Process (devices [0,1,2,3])     Server Process (devices [1,2,3,0])
├─ engine[0] (dev 0) ──────────────> engine[0] (dev 1)
├─ engine[1] (dev 1) ──────────────> engine[1] (dev 2)
├─ engine[2] (dev 2) ──────────────> engine[2] (dev 3)
└─ engine[3] (dev 3) ──────────────> engine[3] (dev 0)
```

**Data Flow**:
1. Client fills `0xDD`, Server fills `0xCC`
2. Client WRITE `0xDD` → Server
3. Client zeroes local memory
4. Client READ ← Server (expects `0xDD`)

### 2. Standalone Same-Host Mode (`test_standalone_same_host.py`)

Simulates Store Service + App co-located on the same host.

**Test Cases**:
- `test_normal_full_flow_same_host_d2rh_write`: D2RH same-host write

**Architecture**:
```
App Process (devices [0,1,2,3])          Store Process (devices [1,2,3,0])
├─ engine[0] (dev 0) ──────────────> engine[0] (dev 1)
├─ engine[1] (dev 1) ──────────────> engine[1] (dev 2)
├─ engine[2] (dev 2) ──────────────> engine[2] (dev 3)
└─ engine[3] (dev 3) ──────────────> engine[3] (dev 0)
                                       └─ shared host memory (base_addr)
```

**Data Flow**:
1. App fills `0xFF`
2. App[i] WRITE `0xFF` → Store host memory offset `i * MEM_SIZE`
3. App zeroes local memory
4. App[i] READ ← Store host memory offset `i * MEM_SIZE` (expects `0xFF`)

### 3. Dummy-Real Shared Memory Mode (`test_dummy_real_shared_mem.py`)

Simulates Mooncake dummy-real deployment mode, validating cross-process device memory sharing.

**Test Cases**:
- `test_normal_full_flow_d2rd_d2rh_read`: D2RD + D2RH full flow

**Architecture**:
```
Local Dummy (devices [0,1,2,3])     Local Real (devices [0,1,2,3])
├─ allocate device memory           ├─ import IPC key
├─ fill 0xAA                        ├─ create engines
└─ export IPC key                   └─ register device + host memory
                                              │
                                              ▼
Remote Real (devices [1,2,3,0])     Remote Dummy (devices [1,2,3,0])
├─ import IPC key                   ├─ allocate device memory
├─ create engines                   ├─ fill 0xBB
├─ register device + host memory    └─ export IPC key
└─ verify D2RH WRITE result
```

**Data Flow**:
1. D2RD WRITE: Local device (0xAA) → Remote device
2. D2RD READ: Remote device → Local device (expects 0xAA)
3. D2RH WRITE: Local device (0xAA) → Remote host
4. D2RH READ: Remote host → Local device (expects 0xAA)

### 4. Dummy-Real VMM Shared Memory Mode (`test_dummy_real_shared_mem_vmm.py`)

The original IPC test is retained. This separate scenario uses VMM interfaces to share memory between processes on the same card and then perform transfers across cards.
It requires an importable pyACL module (`import acl`) and CANN/driver support for VMM allocation, shared handle export/import, mapping, and access permissions.
The devices must also support the selected communication protocol; the general version requirements do not imply support for this scenario on every product.

**Test Case**:

- `test_normal_full_flow_d2rd_d2rh_read`: Runs D2RD WRITE, D2RD READ, D2RH WRITE, and D2RH READ in one test function, so pytest reports `1 passed` when the entire flow succeeds.

**Roles of the Four Processes**:

- Local Dummy: Allocates VMM device memory, fills it with `0xAA`, exports shared handles, and verifies both READ results.
- Local Real: Imports and maps Local Dummy's allocations in another process on the same card, registers memory, connects, and initiates transfers.
- Remote Dummy: Allocates VMM device memory, fills it with `0xBB`, exports shared handles, and verifies the D2RD WRITE result.
- Remote Real: Imports and maps Remote Dummy's allocations in another process on the same card, registers device and host memory, and verifies the D2RH WRITE result.

All processes use the same visible card set. With `ASCEND_RT_VISIBLE_DEVICES=6,7`, logical devices `0,1` map to physical cards `6,7`; the two connections are Local Real card 6 → Remote Real card 7 and card 7 → card 6.
Shared handles are imported between processes on the same physical card; HIXL transfers data across cards.

**Data Flow and Verification**:

1. D2RD WRITE: Writes `0xAA` from Local device memory to Remote device memory; Remote Dummy verifies the result.
2. Local Real synchronously zeroes its local mapping and verifies the zero values, then performs D2RD READ; Local Dummy verifies that `0xAA` was read back.
3. D2RH WRITE: Writes `0xAA` from Local device memory to Remote host memory; Remote Real verifies the result.
4. Local Real synchronously zeroes the mapping and verifies the zero values again, then performs D2RH READ; Local Dummy verifies that `0xAA` was read back.

The parent receives each stage's verification result and acknowledges success before allowing the next stage to proceed. It also checks the exit status and cleanup results of all four processes; verification failures, VMM release failures, or missing results fail the test.

### 5. Scale Reconnect (`test_scale_reconnect.py`)

Tests large-scale multi-link transport and device flapping reconnection.

**Test Cases**:
- `test_normal_flow_multi_links`: Multi-device × 250 links/device batch transfer
- `test_flapping_dev_reconnect`: Reconnection capability under device flapping

**Architecture**:
```
Client Process (devices [0,1,2,3])     Server Process (devices [1,2,3,0])
├─ engine[0] (dev 0) ──┐               ├─ engine[0] (dev 1)
│   ├─ link 0          │               │   ├─ link 0
│   ├─ link 1          │               │   ├─ link 1
│   └─ ...             │               │   └─ ...
│   └─ link 249        │               │   └─ link 249
├─ engine[1] (dev 1) ──┤               ├─ engine[1] (dev 2)
├─ engine[2] (dev 2) ──┤               ├─ engine[2] (dev 3)
└─ engine[3] (dev 3) ──┘               └─ engine[3] (dev 0)
```

## Running Tests

### Basic Usage

```bash
# Set environment variables
export ASCEND_RT_VISIBLE_DEVICES=0,1,2,3
source /usr/local/Ascend/cann/set_env.sh

# Run all tests
python3 -m pytest tests/e2e/ -v

# Run a single test file
python3 -m pytest tests/e2e/test_real_real.py -v

# Show detailed output (including logs)
python3 -m pytest tests/e2e/test_real_real.py -v -s
```

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `ASCEND_RT_VISIBLE_DEVICES` | `0,1,2,3` | Visible NPU device list |
| `ASCEND_GLOBAL_RESOURCE_CONFIG` | Unconfigured; use underlying defaults | Complete JSON configuration string. For a nonempty value, the shared `create_engine` helper adds the global resource configuration option and passes the value unchanged without merging protocols. Example: `'{"comm_resource_config.protocol_desc":["ub_ctp:device"]}'` |
| `HIXL_E2E_MIN_NPU` | `2` | Minimum required NPU count |
| `HIXL_E2E_LINKS_PER_DEV` | `250` | Logical links per device (scale-reconnect) |
| `HIXL_E2E_TRANSFER_SIZE` | `4096` | Transfer size per link (scale-reconnect) |
| `HIXL_E2E_REGISTER_SIZE` | `268435456` (256MB) | Registered memory size (scale-reconnect) |

When the variable is unset or an empty string, the shared E2E helper `create_engine` omits the global resource configuration option. The underlying library handles default communication resources according to the device platform, topology, and capabilities; the test helper does not specify a default protocol list. Leaving the protocol unspecified therefore does not guarantee a particular communication path. To validate VMM + Device UB, explicitly configure `ub_ctp:device` as shown below.

Tests that explicitly pass `options` use those supplied options; the environment variable does not override them.

### Examples

```bash
# Run tests with 4 cards
export ASCEND_RT_VISIBLE_DEVICES=0,1,2,3
python3 -m pytest tests/e2e/ -v

# Run the complete VMM test on two cards with VMM and Device UB support
ASCEND_RT_VISIBLE_DEVICES=6,7 \
ASCEND_GLOBAL_RESOURCE_CONFIG='{"comm_resource_config.protocol_desc":["ub_ctp:device"]}' \
python3 -m pytest tests/e2e/test_dummy_real_shared_mem_vmm.py -v -s -rs

# Custom link parameters
export HIXL_E2E_LINKS_PER_DEV=500
export HIXL_E2E_TRANSFER_SIZE=8192
python3 -m pytest tests/e2e/test_scale_reconnect.py -v

# Skip NPU count check (for debugging)
export HIXL_E2E_MIN_NPU=0
python3 -m pytest tests/e2e/test_real_real.py -v
```

## Test Architecture

### Device Allocation Strategy

Uses `get_device_lists()` for staggered device assignment:
- **Client/App**: Uses `[0, 1, 2, 3]`
- **Server/Store**: Uses `[1, 2, 3, 0]` (offset +1)

Ensures `client[i]` and `server[i]` are on different physical devices, avoiding resource conflicts.

### Port Allocation

Uses `get_port()` to generate unique listening ports:
```
BASE_PORT = 39000
port = BASE_PORT + scenario_idx * 100 + role_offset + dev_id + engine_offset
```

**Example** (SCENARIO_IDX=3, dev_id=0):
- `server` → 39300
- `client` → 39400
- `server_fabric` → 39500
- `client_fabric` → 39600

### Memory Management

- **Device Memory**: Allocated via `torch.zeros(..., device="npu")`
- **Host Memory**: Allocated via `torch.empty(...).pin_memory()` for page-aligned memory
- **IPC Sharing**: Cross-process sharing via `acl.rt.ipc_mem_get_export_key` / `ipc_mem_import_by_key`
- **VMM Sharing**: The VMM test aligns physical allocation sizes to the allocation granularity, reserves VA, maps memory, and sets access permissions. Other processes import shared handles on the same card; the valid data size used for registration and transfer remains 64 KiB per buffer

### Resource Cleanup Order

1. **Client side**: `disconnect` → `deregister` → `finalize`
2. **Server side**: `deregister` → `finalize`

The VMM test stops Local Real first, then Remote Real, and finally signals both Dummies to release their resources.
Each Real releases its imported VMM mappings after deregistering memory and finalizing engines, then the Dummies release the original allocations. Each VMM allocation is cleaned up in this order: unmap → release VA → free the physical memory handle.

## Debugging Tips

### Viewing Detailed Logs

```bash
# Enable console logging
python3 -m pytest tests/e2e/test_real_real.py -v -s
```

### Checking HIXL Logs

```bash
# Set log level
export ASCEND_GLOBAL_LOG_LEVEL=1  # 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR

# Set log path
export ASCEND_PROCESS_LOG_PATH=/tmp/hixl_logs
mkdir -p /tmp/hixl_logs

# Run tests
python3 -m pytest tests/e2e/test_real_real.py -v

# View logs
ls /tmp/hixl_logs/plog-*.log
```

### Common Issues

**Q: Tests skipped with "Need >= 2 NPUs"**
```bash
# Check NPU count
npu-smi info -l | grep "NPU"

# Override environment variable
export HIXL_E2E_MIN_NPU=0
```

**Q: Connection timeout**
```bash
# Check if ports are in use
netstat -tlnp | grep 39

# Increase timeout (modify CONNECT_TIMEOUT_MS in code)
```

**Q: Data verification failure**
```bash
# Check WRITE/READ return values in logs
# Verify device allocation is correct (client[i] and server[i] should be on different physical devices)
```

## File Structure

```
tests/e2e/
├── __init__.py                      # package init file
├── conftest.py                      # pytest configuration and fixtures
├── utils.py                         # shared utility functions
├── test_real_real.py                # Real-Real mode tests
├── test_standalone_same_host.py     # Standalone same-host mode tests
├── test_dummy_real_shared_mem.py    # Dummy-Real shared memory mode tests
├── test_dummy_real_shared_mem_vmm.py # Dummy-Real VMM shared memory mode tests
├── test_scale_reconnect.py          # Scale reconnect tests
├── README.md                        # Chinese documentation
└── README_en.md                     # This document

pytest_e2e.ini                       # pytest e2e config (repository root)
```

## Contributing

1. New test cases should follow existing patterns
2. Use `logger.info(...)` for key step logging
3. Use `result_queue` to pass worker results
4. Ensure correct resource cleanup order: `disconnect` → `deregister` → `finalize`
5. Run `ruff check` and `ruff format` to verify code style
