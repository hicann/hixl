# Run the HIXL communication benchmark with HCCL-VM

[简体中文](./hccl_vm.md) | English

Return to the [communication benchmark](./README_en.md). This tutorial runs functional checks without a physical NPU. It retains the real HIXL benchmark, Host HCOMM and, for UB-family transports, the ARM HIXL kernel and Device HCOMM. HCCL-VM simulates lower interfaces at Level 2; Runner copies shared backing and compares data immediately, and Checker validates task evidence. RoCE uses real Host HCOMM WR/MR/CQ calls with the VM lower layer.

**VM timing and bandwidth do not represent physical device performance.** Use the real-device procedure in the main README for physical network connectivity and performance tests.

## 1. Versions and scope

HCCL-VM is an external dependency. Use the full four-transport implementation on [CheckerL2 integration/hixl-a5](https://gitcode.com/zhupc158/CheckerL2/tree/integration/hixl-a5), pinned to `fc3f46e580c9a60b0ab4f8ad6c2db7c73cde5dd0` ([PR 969](https://gitcode.com/zhupc158/CheckerL2/pull/969) was merged into this integration branch). Read access to that repository is required. Do not substitute the CTP-only batch or assume ordinary main has identical functionality.

Transports are `ub` (UB_CTP), `uboe`, `ub_rtp` and `roce`. Each runs these four memory pairs, for 16 cases, with a separate write and read phase per case:

| initiator:target | write | read |
|---|---|---|
| device:device | D2rD | rD2D |
| host:device | H2rD | rD2H |
| device:host | D2rH | rH2D |
| host:host | H2rH | rH2H |

`r` means remote. Each phase starts new initiator and target processes; read is not guaranteed to read the same buffer written by the previous phase. Payload reads and internal 8-byte completion-flag reads are distinct. This tutorial adds no HCCS, FabricMem or A2/A3 cases.

## 2. Prepare the environment

Use Linux with matching CANN toolkit, HCOMM/HCCL source headers and AICPU packages. Initial builds need network access or prepared third-party caches. Follow the [HIXL build guide](../../docs/en/build.md#environment-setup) and [VM README](https://gitcode.com/zhupc158/CheckerL2/blob/fc3f46e580c9a60b0ab4f8ad6c2db7c73cde5dd0/README.md) for build dependencies. Missing `sqlite3.h`, `zlib.h` or `infiniband/verbs.h` requires the SQLite, zlib or ibverbs development package, respectively. On x86_64, ARM Device execution also needs `qemu-aarch64-static`; native aarch64 does not need QEMU.

Stop on any nonzero step result and inspect its log before continuing. Run the commands in order in the same Bash session. Replace the following four absolute paths first. Select a writable, separate CANN installation for JSON deployment rather than modifying a shared installation:

```bash
export WORKSPACE="$HOME/hixl-vm-work"
export HIXL_SRC=/absolute/path/to/hixl
export HCOMM_SRC=/absolute/path/to/hcomm
export HCCL_SRC=/absolute/path/to/hccl
export CANN_ROOT=/absolute/path/to/cann
source "$CANN_ROOT/set_env.sh"
export VM_SRC="$WORKSPACE/CheckerL2"
export VM_INSTALL="$VM_SRC/hccl_vm_install"
export TUTORIAL_WORK="$WORKSPACE/tutorial"
mkdir -p "$TUTORIAL_WORK"
```

Obtain the pinned VM version (the destination directory must not exist):

```bash
git clone --branch integration/hixl-a5 \
  https://gitcode.com/zhupc158/CheckerL2.git "$VM_SRC"
git -C "$VM_SRC" checkout --detach fc3f46e580c9a60b0ab4f8ad6c2db7c73cde5dd0
```

## 3. Build HIXL and the VM

### 3.1 Build HIXL

Include the Device build: **do not add `--host`**. Use the HIXL source containing this document to generate matching Host libraries, Device kernel and JSON:

```bash
cd "$HIXL_SRC"
bash build.sh --examples -j4 >"$TUTORIAL_WORK/hixl-build.log" 2>&1 || {
  tail -n 80 "$TUTORIAL_WORK/hixl-build.log"; exit 1;
}
export benchmark="$HIXL_SRC/build/benchmarks/comm_benchmark/hixl_comm_bench"
export hixl_device_dir="$HIXL_SRC/build/device_build/src/ops/hixl_kernel"
test -x "$benchmark"
test -f "$hixl_device_dir/libcann_hixl_kernel.so"
test -f "$hixl_device_dir/libcann_hixl_kernel.json"
```

The smoke script locates the Host HIXL library relative to the benchmark build directory. Do not move only the executable or mix in a Device kernel from another version.

### 3.2 Build VM Host/ARM components and deploy CANN Device libraries

Set source paths and build the full VM. `build_pkg.sh --deploy all` only deploys installed HCCL/HCOMM AICPU packages from the selected CANN; it does not build or reinstall HCOMM/HCCL:

```bash
cd "$VM_SRC"
export HCOMM_CODE_HOME="$HCOMM_SRC"
export HCCL_CODE_HOME="$HCCL_SRC"
HVM_MAKE_JOBS=4 bash build.sh --full \
  --package-path "$ASCEND_HOME_PATH" \
  --hcomm-path "$HCOMM_SRC" --hccl-path "$HCCL_SRC" \
  >"$TUTORIAL_WORK/vm-build.log" 2>&1 || {
    tail -n 80 "$TUTORIAL_WORK/vm-build.log"; exit 1;
  }
bash build_pkg.sh --deploy all --tool_path "$VM_SRC" \
  >"$TUTORIAL_WORK/vm-deploy.log" 2>&1 || {
    tail -n 80 "$TUTORIAL_WORK/vm-deploy.log"; exit 1;
  }
cp "$hixl_device_dir/libcann_hixl_kernel.so" "$VM_INSTALL/lib/aarch64/"
```

Deployment depends on the selected CANN package layout and extraction tools. If it fails, inspect `vm-deploy.log` and the pinned script help before continuing. The copied HIXL library is the newly built real ARM kernel, not a Host kernel substitute.

### 3.3 Deploy matching kernel JSON

The pinned VM script reads JSON from the current CANN installation. Save the existing HIXL JSON, then deploy the generated version. The matching HCOMM package must supply `ccl_kernel.json`:

```bash
export kernel_config="$ASCEND_HOME_PATH/opp/built-in/op_impl/aicpu/config"
if test -f "$kernel_config/libcann_hixl_kernel.json"; then
  cp "$kernel_config/libcann_hixl_kernel.json" "$TUTORIAL_WORK/libcann_hixl_kernel.json.backup"
fi
cp "$hixl_device_dir/libcann_hixl_kernel.json" "$kernel_config/"
cmp "$hixl_device_dir/libcann_hixl_kernel.json" "$kernel_config/libcann_hixl_kernel.json"
```

### 3.4 Check runtime artifacts

```bash
test -x "$VM_INSTALL/bin/hccl-vm"
test -x "$VM_INSTALL/bin/device"
test -f "$ASCEND_HOME_PATH/lib64/libhcomm.so"
test -f "$kernel_config/ccl_kernel.json"
for lib in libccl_kernel.so libccl_kernel_plf.so libcann_hixl_kernel.so; do
  test -f "$VM_INSTALL/lib/aarch64/$lib" || { echo "Missing: $lib"; exit 1; }
done
if test "$(uname -m)" != aarch64; then
  export HCCL_VM_QEMU_PATH="${HCCL_VM_QEMU_PATH:-$(command -v qemu-aarch64-static || true)}"
  test -x "$HCCL_VM_QEMU_PATH" || { echo 'Install qemu-user-static or set HCCL_VM_QEMU_PATH'; exit 1; }
fi
```

For a custom QEMU deployment, set `HCCL_VM_QEMU_PATH` directly to its absolute path. These checks establish only the listed prerequisites; incompatible shared libraries or ABIs can still fail at runtime.

## 4. Explicitly enable read verification

The current benchmark defaults to `--data_verify=false` and reports `consistency=not_checked`. The pinned VM smoke script requires `consistency=pass` but does not pass this option. Enable verification on **both target and initiator**.

Generate a tutorial-specific copy that adds exactly those two arguments, without changing VM source, the installed script or benchmark defaults. Format checks stop on an unexpected script version; do not bypass them:

```bash
export smoke="$TUTORIAL_WORK/run_hixl_smoke_verify.sh"
python3 - <<'PYCODE' || exit 1
import os
from pathlib import Path
source = Path(os.environ["VM_INSTALL"]) / "script/run_hixl_smoke.sh"
destination = Path(os.environ["smoke"])
text = source.read_text()
for role in ("target", "initiator"):
    old = f"--role={role} --device_id="
    if text.count(old) != 1:
        raise SystemExit("Unexpected VM script version; check the pinned commit")
    text = text.replace(old, f"--role={role} --data_verify=true --device_id=")
if text.count("--data_verify=true") != 2:
    raise SystemExit("Read verification must be enabled on both processes")
destination.write_text(text)
PYCODE
bash -n "$smoke"
```

Run the copy with `bash "$smoke"`; executable permission is unnecessary. The write phase remains a write, and benchmark data verification applies only to reads. Do not switch back to the original installed smoke or matrix entry point, which lacks the explicit verification options.

## 5. Start with UB_CTP D2D


```bash
cd "$VM_INSTALL/bin"
single_log="$TUTORIAL_WORK/ub_device_device.log"
if timeout --signal=TERM --kill-after=5s 90s \
  ./hccl-vm run-hixl ascend950_cluster_32_server_normal \
  --level 2 --topo-meta 112 --validate-tasks \
  bash "$smoke" --transport=ub \
  --initiator-memory=device --target-memory=device --keep-logs \
  "$benchmark" >"$single_log" 2>&1; then
  echo "Command completed; check all three markers: $single_log"
else
  rc=$?
  echo "FAIL ub device:device rc=$rc log=$single_log"
fi
grep -E 'HiXL HCCL-VM smoke passed|HCOMM Checker PASS|PASS HCOMM Runner/Checker task validation' "$single_log"
```

- `run-hixl` is the dedicated command, not ordinary `run`. Do not insert a standalone `--` before the script.
- `--level 2` retains real HCOMM with lower-interface simulation; it does not load the HIXL Level 1 replacement.
- `--topo-meta 112` names an existing communication-domain topology configuration selecting two ranks here; it is not a chip type or NIC IP. The pinned script configures endpoint EIDs/IPs and LocalCommRes.
- `--validate-tasks` enables final Checker validation. Runner still executes tasks without it, but that omits the final acceptance required here.
- Each smoke phase transfers 1 MiB, using a 64 KiB block size and loops=1. This is not an arbitrary-scale stress test.

## 6. Run the full 16-case matrix

After the single case succeeds, run this loop. UBOE needs its dedicated topology; other transports use the normal A5 topology. Run serially to avoid fixed-port conflicts:

```bash
cd "$VM_INSTALL/bin"
result_dir=$(mktemp -d "$TUTORIAL_WORK/matrix.XXXXXX")
summary_file="$result_dir/summary.log"
passed=0
failed=0
: >"$summary_file"
echo "RESULT_DIR=$result_dir"
for transport in ub uboe ub_rtp roce; do
  topology=ascend950_cluster_32_server_normal
  if test "$transport" = uboe; then
    topology=ascend950_cluster_4_server_normal_uboe
  fi
  for memory_pair in device:device host:device device:host host:host; do
    IFS=: read -r initiator_memory target_memory <<< "$memory_pair"
    log_file="$result_dir/${transport}_${initiator_memory}_${target_memory}.log"
    if timeout --signal=TERM --kill-after=5s 90s \
      ./hccl-vm run-hixl "$topology" \
      --level 2 --topo-meta 112 --validate-tasks \
      bash "$smoke" --transport="$transport" \
      --initiator-memory="$initiator_memory" --target-memory="$target_memory" \
      --keep-logs "$benchmark" >"$log_file" 2>&1 &&
      grep -q 'HiXL HCCL-VM smoke passed' "$log_file" &&
      grep -q 'HCOMM Checker PASS, level=2' "$log_file" &&
      grep -q 'PASS HCOMM Runner/Checker task validation' "$log_file"; then
      passed=$((passed + 1))
      echo "PASS $transport $memory_pair" | tee -a "$summary_file"
    else
      rc=$?
      failed=$((failed + 1))
      echo "FAIL $transport $memory_pair rc=$rc log=$log_file" | tee -a "$summary_file"
    fi
  done
done
echo "TOTAL_PASS=$passed TOTAL_FAIL=$failed" | tee -a "$summary_file"
echo "SUMMARY=$summary_file"
test "$passed" -eq 16 && test "$failed" -eq 0
```

Require 16 PASS lines and `TOTAL_PASS=16 TOTAL_FAIL=0`. Detailed output goes to per-case logs, keeping the terminal summary concise. Every FAIL prints its log path; other successful cases do not excuse a failure.

## 7. Recognize success

Each case must exit with code 0 and include all three markers:

```text
HiXL HCCL-VM smoke passed
HCOMM Checker PASS, level=2
PASS HCOMM Runner/Checker task validation
```

These indicate script acceptance, Checker task acceptance, and final VM validation and exit processing. Read output JSON must also contain `"consistency":"pass"`; `not_checked` does not satisfy this requirement.

Runner copies actual bytes and immediately compares source/destination shared backing. Checker validates actual protocol, operation, Host/Device types, payload coverage, task states and completion evidence; it does not traverse buffers for another memcmp pass. Any single marker or benchmark output alone is insufficient.

With `--keep-logs`, the case log prints `HiXL smoke logs retained at:` with the directory containing target/initiator logs and output files. A summary rc can also come from a missing-marker check, not just a workload exit status.

## 8. Troubleshooting

| Symptom | What to check |
|---|---|
| Missing run-hixl or unknown options | Check the pinned VM commit and VM_INSTALL; rebuild and install the full version |
| Missing device, ARM libraries or QEMU | Complete the Device build, CANN package deployment and HIXL kernel copy; check the actual architecture |
| Missing JSON or kernel loading failure | Source the same CANN installation used for deployment; match JSON and Device libraries to this build |
| read completed without consistency=pass | Use the tutorial copy with data_verify=true on both sides; inspect target logs and output JSON |
| HiXL did not select protocol ... | Check actual link selection and EndpointMatcher logging; missing logs do not prove correct protocol selection |
| Timeout or connection failure | Check for concurrent matrices and occupied ports, both process logs and Runner task states; timeouts are failures |
| Runner or Checker failure | Inspect failed tasks, address ranges and protocol bindings; do not remove validation to obtain PASS |
| Old behavior after rebuilding | Check that the actual benchmark, Host library, Device kernel, JSON and VM binaries belong to the intended versions |

## 9. Validated environment and boundaries

This is the reference combination, not a guarantee that other versions are compatible:

| Component | Version used for this run |
|---|---|
| Host | Ubuntu 24.04 x86_64 |
| QEMU | 8.2.2 (x86_64 host) |
| CANN | 9.3.0, with real Host HCOMM and matching AICPU packages |
| HIXL implementation | `2384c335` (this PR adds documentation only) |
| HCOMM source headers | `a38c0e1b1` |
| HCCL source headers | `c5e32b95` |
| CheckerL2 | `fc3f46e580c9a60b0ab4f8ad6c2db7c73cde5dd0` |

After changing CANN, source or VM commits, rebuild matching libraries and rerun the matrix. The scope is two simulated ranks on one host with fixed small payloads; this does not validate every topology, payload size, concurrency pattern or physical device.

This run completed the full HIXL build, VM Host/ARM component builds, CANN package deployment checks, the UB_CTP D2D case and the full matrix: `TOTAL_PASS=16 TOTAL_FAIL=0`. All three acceptance markers were checked for every case.
