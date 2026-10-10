# vllm_hixl_connector

> **Experimental module**: This module lives under `src/experimental/` and is excluded from the build by default; it is packaged into the hixl wheel only when built with `build.sh --experimental`. Its API and behavior may change or be removed at any time, and it is not recommended for production use.

## Overview

`vllm_hixl_connector` is a vLLM V1 KV cache connector for Ascend NPUs. In PD-disaggregated (Prefill/Decode Disaggregation) inference, it leverages the one-sided communication capability of the HIXL engine (HCCS/RDMA peer-to-peer D2D transfer) to move the KV caches computed by the Prefill instance to the Decode instance at high speed, so the Decode side does not have to re-prefill the prompt.

Key capabilities:

- **Separated control plane and data plane**: the control plane runs over ZMQ (metadata handshake, transfer-completion notification, block freeing), while the data plane lets the Decode side pull directly from the Prefill side's device memory via one-sided HIXL READs — the Prefill side never ships blocks itself.
- **Flexible parallel layouts**: different TP/DP sizes on the P and D sides, PP on the Prefill side (with custom layer partitioning), PCP/DCP context parallelism, and cross-node deployment.
- **Broad model coverage**: GQA, MLA (DeepSeek family), sparse attention (including the SFA DCP replicated indexer), compressed KV (`compress_ratios`), hybrid architectures (Mamba-style linear layers + attention layers, HMA), SWA window clipping, and MTP/EAGLE3 speculative decoding.
- **Asymmetric prefix caching**: when P/D hit rates differ, the set of blocks that actually need pulling is trimmed automatically.
- **Post-pull reformatting**: GQA multi-segment merge, NZ format conversion, and hybrid linear-layer transpose, preferably accelerated with fused Ascend operators.

## How It Works

The connector holds different responsibilities on the scheduler side and the worker side of vLLM. The overall transfer sequence:

1. At worker startup a HIXL engine instance is created; once the KV caches are allocated, vLLM calls back `register_kv_caches` to register the device-memory regions (already 2M-aligned by the allocator) with the engine (`register_physical_regions`).
2. The Prefill side (`kv_producer`) starts `KVCacheSendingThread`, listening as a ZMQ ROUTER on each rank's handshake port; the Decode side (`kv_consumer`) starts `KVCacheRecvingThread`.
3. When the Decode-side scheduler finds a request that needs remote prefill, the worker computes the transfer shards for it (remote ports, remote/local block-id mappings) and fetches `HIXLAgentMetadata` from the Prefill side over ZMQ (remote KV base addresses, listen port, group layout).
4. The Decode side organizes source/destination addresses and lengths into batched descriptors and calls `Hixl.transfer_async` (READ) to issue the one-sided pull, polling until completion.
5. After the pull, the KV layout is reformatted as needed, then a `DONE_RECVING_MSG` is sent to the Prefill side over ZMQ; once the Prefill side has confirmed all shards, it delays freeing the KV blocks (unconfirmed blocks are force-freed past a timeout, with an error logged).

Port layout (all derived from `kv_transfer_config.kv_port`):

| Port | Formula | Purpose |
|------|---------|---------|
| Handshake port | `kv_port + DP offset + device_index` | ZMQ control plane (metadata, completion signals) |
| Engine listen port | `kv_port + 10000 + DP offset + device_index` | HIXL engine data-plane endpoint |

where `DP offset = dp_rank × tp_size × pp_size × pcp_size` and `device_index = (pp_rank × pcp_size + pcp_rank) × tp_size + tp_rank`, so every rank gets a unique port.

## Module Layout

| File | Description |
|------|-------------|
| `connector.py` | Entry point of `HIXLConnector`, implementing the vLLM V1 connector interface; scheduler-side logic (request matching, delayed block freeing, metadata building, multi-node handshake mapping) |
| `worker.py` | Worker-side logic: KV memory registration, TP/PP/PCP/DCP transfer sharding and block-id mapping, pull strategies for GQA/Mamba/MLA and other cases |
| `transfer_threads.py` | Background transfer threads: the P-side ZMQ ROUTER service thread; the D-side request queue, per-peer concurrent pulls and KV reformatting |
| `hixl_wrapper.py` | Local wrapper around the native `hixl` pybind: connection reuse, async transfers polled into blocking reads, soft/hard timeouts with orphan-handle reaping, peer invalidation and reconnect |
| `engine_options.py` | Parsing of the `hixl_engine` extra config: backend selection (`hixl_cs`/`comm`), option normalization (`LocalCommRes`/`GlobalResourceConfig`), listen port and timeouts |
| `metadata.py` | Wire-protocol and per-step data structures: `HIXLAgentMetadata`, `ReqMeta`, `GroupPull`, `HIXLConnectorMetadata`, etc. |
| `task_tracker.py` | Request-completion and delayed-free tracking shared by the P and D sides (force-free on timeout) |
| `utils.py` | ZMQ send/receive retries, contiguous block grouping, request hashing, PP layer-partition parsing and other helpers |
| `constants.py` | Message types, backend names, default timeouts and other constants |

## Requirements and Installation

- Ascend environment: CANN toolkit (build >= 9.0.0, runtime >= 8.5), with its environment variables sourced.
- Software dependencies: vllm (V1 connector API), vllm-ascend, torch/torch_npu, numpy, msgspec, pyzmq.
- Build and install the hixl wheel that contains this module:

```bash
bash build.sh --experimental
# After installing the hixl wheel from build_out/, vllm_hixl_connector is available as a top-level Python package
```

## Usage

### Registering the Connector

There are two ways to plug the module into vLLM: **use the source tree directly without building**, or **build a wheel and install it**. The vLLM launch command is identical for both; the only difference is where the `vllm_hixl_connector` package in the Python environment comes from.

#### Option 1: Use directly in vLLM, without building

Suitable when the environment already has a wheel with the native `hixl` library and you just want to try out or iterate on the pure-Python connector code. The source package shadows the old copy in `site-packages` via `PYTHONPATH`, in three steps:

1. Place the source package into the runtime environment (copy or mount); only this one directory is needed (`PYTHONPATH` must point to its parent directory `src/experimental/python`):

   ```
   /path/to/hixl/src/experimental/python/vllm_hixl_connector/   # 9 modules + __init__.py
   ```

2. Make the source package take precedence over the old copy from the wheel, and start vLLM from the same shell (the server process must inherit the variable):

   ```bash
   export PYTHONPATH=/path/to/hixl/src/experimental/python:$PYTHONPATH
   ```

3. Start vLLM. Prefill side shown (for the decode side, switch `kv_role` to `kv_consumer`):

   ```bash
   vllm serve <model> --tensor-parallel-size 8 \
     --kv-transfer-config '{
       "kv_connector": "HIXLConnector",
       "kv_connector_module_path": "vllm_hixl_connector.connector",
       "kv_role": "kv_producer",
       "kv_port": 23000,
       "extra_config": {
         "prefill": {"tp_size": 8, "dp_size": 1},
         "decode":  {"tp_size": 4, "dp_size": 1}
       }
     }'
   ```

Notes for Option 1:

- `PYTHONPATH` shadows the whole package: once the source path wins in `sys.path`, the old copy from the wheel becomes entirely unavailable, so old and new code are never mixed.
- Both the P and D sides must set the same `PYTHONPATH`. Otherwise the two sides load two different `HIXLConnector` implementations: not only are the class objects distinct, the ZMQ messages and the `HIXLAgentMetadata` wire protocol may also be incompatible. Failures then surface as anomalies during handshake or transfer rather than clear import errors.

#### Option 2: Build, then use in vLLM

The formal deployment path — the connector is installed into the Python environment together with the hixl wheel:

```bash
bash build.sh --experimental
pip install <the hixl wheel under build_out>
```

No environment variables are needed after installation. Decode side shown:

```bash
vllm serve <model> --tensor-parallel-size 4 \
  --kv-transfer-config '{
    "kv_connector": "HIXLConnector",
    "kv_connector_module_path": "vllm_hixl_connector",
    "kv_role": "kv_consumer",
    "kv_port": 23000,
    "extra_config": {
      "prefill": {"tp_size": 8, "dp_size": 1},
      "decode":  {"tp_size": 4, "dp_size": 1}
    }
  }'
```

Points common to both paths:

- The P and D sides differ only in `kv_role`; every other setting (including `extra_config`) is identical;
- Both parallelism sections of `extra_config` must be filled in on both sides: the module reads the `prefill` and `decode` sections on each side to derive peer ranks, ports and block mappings;
- When P and D are co-located on one machine, the two sides must use different `kv_port` values (see "KVTransferConfig").

#### Installation self-check

Verify in two steps before starting vLLM; Option 1 additionally requires confirming that the source copy, not the old one, is loaded:

```bash
# Step 1: the package can be imported (lightweight, does not import vllm/torch, runs without NPUs)
python -c "import vllm_hixl_connector; print(vllm_hixl_connector.__file__)"
# Under Option 2 this prints vllm_hixl_connector/__init__.py inside the wheel install path;
# under Option 1 it must point into the PYTHONPATH source directory — a site-packages/...
# path means the variable is not in effect (or vLLM was started by another user/service
# that did not inherit it).

# Step 2: the connector class can be fully loaded (requires vllm, vllm-ascend, torch_npu installed)
python -c "from vllm_hixl_connector import HIXLConnector; print(HIXLConnector)"
# Expected: <class 'vllm_hixl_connector.connector.HIXLConnector'>
```

A `ModuleNotFoundError` in step 1 means the wheel is not installed, or was built without `--experimental`; import failures in step 2 mean the runtime dependencies are missing (see "Requirements and Installation").

### How vLLM Loads the Connector

`HIXLConnector` is not a vLLM built-in. When starting a PD-disaggregated instance, vLLM imports it at runtime based on two fields of `--kv-transfer-config`:

- `kv_connector`: the connector **class name**;
- `kv_connector_module_path` (optional): the **Python module path** holding the class.

Resolution order (`KVConnectorFactory.create_connector`):

1. If `kv_connector_module_path` is set → `importlib.import_module(that path)`, then `getattr(module, kv_connector)`;
2. Otherwise, if `kv_connector` contains a dot → split at the last dot into "module path + class name" and go back to step 1;
3. Otherwise → only the vLLM built-in connectors are searched, and `HIXLConnector` will not be found.

Hence you must tell vLLM where the class lives via `kv_connector_module_path` (or the equivalent fully-dotted form `"kv_connector": "vllm_hixl_connector.connector.HIXLConnector"`, which depends on vLLM-version support for that parsing) — which is exactly why both fields appear in the examples above.

The package layout keeps this loading cheap:

```
vllm_hixl_connector/
├── __init__.py      # lazy export only; importing does not pull in vllm/torch
├── connector.py     # the HIXLConnector class
└── ...              # the other 8 modules
```

`__init__.py` exports `HIXLConnector` through a lazy `__getattr__`: `import vllm_hixl_connector` is very lightweight, and only actually accessing the `HIXLConnector` attribute imports `connector.py` and all of its dependencies (vllm, torch_npu, etc.). The Option 2 example (module path pointing at the package `vllm_hixl_connector`) takes advantage of this; the Option 1 example and the fully-dotted form import `connector.py` directly.

### KVTransferConfig

`--kv-transfer-config` takes a JSON object (corresponding to vLLM's `KVTransferConfig` class) with the following top-level fields:

| Field | Required | Description |
|-------|----------|-------------|
| `kv_role` | Yes | Instance role: `kv_producer` on the Prefill side (computes KV and waits for it to be pulled), `kv_consumer` on the Decode side (actively pulls remote KV) |
| `kv_connector` | Yes | Connector class name; always `HIXLConnector` |
| `kv_connector_module_path` | Recommended | Module path; use `vllm_hixl_connector` (see "Registering the Connector") |
| `engine_id` | No (explicit recommended) | Unique identifier of this instance; must differ between P and D (checked by the D side when fetching metadata). If left unset, newer vLLM versions auto-generate a random uuid in `KVTransferConfig.__post_init__`, so the two sides naturally differ; setting it explicitly makes instances easier to identify in logs and debugging; on older vLLM versions without the auto-generation it must be set explicitly |
| `kv_port` | Yes | Port base value: handshake ports count up from `kv_port` and engine listen ports from `kv_port + 10000`, each occupying a contiguous run of per-rank ports (exact formulas under "How It Works"); **when P and D are co-located, the two sides must use different `kv_port` values** |
| `extra_config` | Yes | Extension config of this module; see the next section |

### Configuration Reference

`extra_config` fields:

| Field | Sub-item | Description | Default |
|-------|----------|-------------|---------|
| `prefill` | `tp_size` / `dp_size` | Prefill-side parallel layout; required on both sides | none (required) |
| | `pp_size` | Prefill-side pipeline-parallel degree | 1 |
| | `pp_layer_partition` | Layer count per PP rank, comma-separated (e.g. `"16,16"`); defaults to vLLM's split | none |
| `decode` | `tp_size` / `dp_size` | Decode-side parallel layout; required on both sides | none (required) |
| | `pp_size` | Decode-side pipeline-parallel degree; currently must be 1 | 1 |
| `hixl_engine` | `backend` | HIXL backend: `hixl_cs` or `comm` | `hixl_cs` |
| | `options` | Options dict passed through to the engine (e.g. `GlobalResourceConfig`); `hixl_cs` auto-fills `LocalCommRes` 1.3 when no protocol descriptor is configured explicitly, while `comm` strips CS selectors | `{}` |
| | `listen_port_base` | Base value for the engine listen port | `kv_port + 10000` |
| | `link_timeout_ms` | Link establishment timeout | 5000 |
| | `transfer_timeout_ms` | Soft timeout per transfer (past it, up to 30 more seconds are spent draining before the transfer is abandoned with a warning) | 60000 |

After each transfer completes, the connector logs per-request elapsed time, byte count and bandwidth (INFO), useful for performance diagnosis.

### Troubleshooting

| Symptom | Cause and fix |
|---------|---------------|
| `ModuleNotFoundError: No module named 'vllm_hixl_connector'` | The wheel is not installed, or was built without `--experimental`; confirm with step 1 of the installation self-check |
| `AttributeError: module 'vllm_hixl_connector' has no attribute '...'` | Misspelled `kv_connector` class name; the `__getattr__` recognizes only `HIXLConnector` |
| Step 2 of the self-check fails to import vllm / vllm_ascend / torch_npu | The lazy-loading design imports these dependencies only when the class is actually accessed; install the runtime dependencies per "Requirements and Installation" |
| Startup fails with `prefill_tp_size ... must be greater than or equal to the decode_tp_size` | `extra_config.prefill.tp_size` is smaller than `decode.tp_size`; adjust the parallel layout |
| Startup fails with parallel-layout errors such as `pp_size(x) and pcp_size(y) cannot both be greater than 1` | The parallel layout violates a constraint; check each item under "Constraints and Notes" |
| P/D port conflicts when co-located; processes fail to start | Handshake/listen ports are both derived from `kv_port` (listen offset +10000); give the two co-located sides different `kv_port` values and keep the range from `kv_port` to `kv_port + 10000 + total ranks` free |
| One side starts normally while the other stalls or times out on handshake | Check network connectivity and firewalls between the sides (both handshake and engine listen ports must be open); confirm both sides loaded the same implementation (see the notes under "Registering the Connector" > Option 1) |

## Constraints and Notes

- `prefill.tp_size` must be greater than or equal to `decode.tp_size`; hybrid/Mamba models additionally require the former to be divisible by the latter.
- PP is not supported on the Decode side (`decode.pp_size` must be 1); PP and PCP cannot both be greater than 1.
- The `block_size` values of the P and D sides must divide each other; with context parallelism on the D side (PCP/DCP greater than 1), a P-side block size larger than the D side is not supported.
- Context-parallel deployments require the remote CP size to be an integer multiple of the local CP size.
- This module is experimental; its interfaces and behavior may change between versions. For issues, start by observing the handshake and shard-mapping flow with DEBUG logs.
- A pull that is still WAITING past `hixl_engine.transfer_timeout_ms` (default 60 s) plus a 30 s grace period is abandoned (with a correctness-hazard log): the abandoned READ may still write into recycled KV blocks when it lands late. Raise the timeout to match your network and load, and treat such warnings as a prompt to investigate the network first.
- After receiving the "done receiving" signal from the Decode side, the Prefill side delays freeing the KV blocks. The maximum wait reuses the vLLM environment variable `VLLM_MOONCAKE_ABORT_REQUEST_TIMEOUT` (in seconds, vLLM default 600; the connector does not introduce a separate variable). Past the timeout the blocks are force-freed, with a `Force freed expired request` ERROR log, to avoid memory leaks.
- The ZMQ control plane has no authentication or encryption: handshake and engine listen ports must only be exposed on a trusted network (internal or dedicated cluster network), never to the public internet.
