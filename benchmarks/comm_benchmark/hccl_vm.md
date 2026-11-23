# 在 HCCL-VM 中运行 HIXL communication benchmark

简体中文 | [English](./hccl_vm_en.md)

返回 [communication benchmark](./README.md)。本教程提供无真实 NPU 的功能验证步骤，使用真实 HIXL benchmark、Host HCOMM，以及 UB 类协议的真实 ARM HIXL kernel/Device HCOMM。HCCL-VM 在 Level 2 模拟底层接口，Runner 执行共享内存搬运并即时比较，Checker 核对任务证据。RoCE 数据面走真实 Host HCOMM 的 WR/MR/CQ 调用与 VM 下层模拟。

**VM 内的耗时和带宽不是物理设备性能，不能用于性能比较。** 物理网络连通性测试仍使用主 README 中的真实设备流程。

## 1. 适用版本和范围

HCCL-VM 是外部工具，本文采用 [CheckerL2 integration/hixl-a5](https://gitcode.com/zhupc158/CheckerL2/tree/integration/hixl-a5) 的完整四协议功能，固定提交 `fc3f46e580c9a60b0ab4f8ad6c2db7c73cde5dd0`（[PR 969](https://gitcode.com/zhupc158/CheckerL2/pull/969) 已合入该集成分支）。需要该仓库的读取权限；不要用仅 CTP 的分批版本或假定普通 main 具有完全相同的功能。

协议为 `ub`（UB_CTP）、`uboe`、`ub_rtp`、`roce`。每个协议运行下列四种内存组合，共 16 项，每项分别执行一次 write 和 read：

| initiator:target | write | read |
|---|---|---|
| device:device | D2rD | rD2D |
| host:device | H2rD | rD2H |
| device:host | D2rH | rH2D |
| host:host | H2rH | rH2H |

`r` 表示远端。每个阶段独立启动 initiator 和 target，不保证 read 回读上一阶段 write 的同一块 buffer。业务 read 和用于完成同步的 8 字节标志 read 是不同任务。本教程不新增 HCCS、FabricMem 或 A2/A3 场景。

## 2. 环境准备

使用 Linux，准备匹配的 CANN toolkit、HCOMM/HCCL 头文件源码和 AICPU 包。首次构建还需要网络或预先准备第三方缓存。按 [HIXL 构建指南](../../docs/zh/build.md) 和 [VM README](https://gitcode.com/zhupc158/CheckerL2/blob/fc3f46e580c9a60b0ab4f8ad6c2db7c73cde5dd0/README.md) 安装编译依赖；缺少 `sqlite3.h`、`zlib.h`、`infiniband/verbs.h` 时分别补充 SQLite、zlib、ibverbs 开发包。x86_64 运行 ARM Device 进程还需要 `qemu-aarch64-static`，原生 aarch64 不需要 QEMU。

任一步骤返回非零时，应停止并查看该步骤日志，修复后再继续。所有命令在同一个 Bash 会话按顺序执行。先替换下列四个绝对路径；选择可写的独立 CANN 安装用于部署 JSON，避免修改共享环境：

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

获取指定 VM 版本（目标目录应不存在）：

```bash
git clone --branch integration/hixl-a5 \
  https://gitcode.com/zhupc158/CheckerL2.git "$VM_SRC"
git -C "$VM_SRC" checkout --detach fc3f46e580c9a60b0ab4f8ad6c2db7c73cde5dd0
```

## 3. 构建 HIXL benchmark 和 VM

### 3.1 构建 HIXL

必须包含 Device 构建，**不要加 `--host`**。请使用当前文档所在的 HIXL 源码目录，生成匹配的 Host 库、Device kernel 和 JSON：

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

脚本从 benchmark 所在 build 目录定位 Host HIXL 动态库。不要只移动 benchmark 二进制，也不要用其他版本的 Device kernel 混搭。

### 3.2 构建 VM Host/ARM 并部署 CANN Device 库

设置源码路径并完整构建；`build_pkg.sh --deploy all` 仅从已选 CANN 部署 HCCL/HCOMM AICPU 包，不编译或重装 HCOMM/HCCL：

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

`build_pkg.sh` 对所选 CANN 的包布局和解包工具有要求；如部署失败，应先查看 `vm-deploy.log` 和固定版本的脚本帮助，不要忽略失败后继续运行。这里复制的是刚构建的真实 ARM HIXL kernel，不是 Host 模拟 kernel。

### 3.3 部署匹配的 kernel JSON

固定 VM 脚本从当前 CANN 目录读取 JSON。以下命令保存已有 HIXL JSON，再部署本次生成的版本；CANN 的 `ccl_kernel.json` 应由匹配的 HCOMM 包提供：

```bash
export kernel_config="$ASCEND_HOME_PATH/opp/built-in/op_impl/aicpu/config"
if test -f "$kernel_config/libcann_hixl_kernel.json"; then
  cp "$kernel_config/libcann_hixl_kernel.json" "$TUTORIAL_WORK/libcann_hixl_kernel.json.backup"
fi
cp "$hixl_device_dir/libcann_hixl_kernel.json" "$kernel_config/"
cmp "$hixl_device_dir/libcann_hixl_kernel.json" "$kernel_config/libcann_hixl_kernel.json"
```

### 3.4 检查运行产物

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

如自行部署 QEMU，直接把 `HCCL_VM_QEMU_PATH` 设置为其绝对路径即可。检查通过仅代表所列前提满足；依赖动态库或 ABI 不匹配仍会在执行时失败。

## 4. 为当前 benchmark 显式开启 read 精度校验

当前 benchmark 默认 `--data_verify=false`，输出 `consistency=not_checked`；固定 VM 冒烟脚本要求 `consistency=pass`，但原脚本没有传该参数。因此必须在 **target 和 initiator 两端**开启校验。

下面从安装脚本生成教程专用副本，只追加两处参数，不修改 VM 源码、安装脚本或 benchmark 默认行为。它对固定脚本格式做检查，版本不符时停止，不能直接跳过检查：

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

后面通过 `bash "$smoke"` 运行副本，不要求执行权限。write 阶段仍执行 write；benchmark 的数据精度检查只用于 read。不要换回原安装脚本或原矩阵入口，否则会重新缺少显式校验参数。

## 5. 先运行 UB_CTP D2D


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

- `run-hixl` 是专用子命令，不是普通 `run`；脚本前不要插入单独的 `--`。
- `--level 2` 保留真实 HCOMM，在下层提供模拟；不加载 HIXL Level 1 替代实现。
- `--topo-meta 112` 是已有通信域拓扑元数据配置名，本例选择两个 rank；不是芯片型号或网卡 IP。双方 EID/IP 与 LocalCommRes 由固定脚本配置。
- `--validate-tasks` 启用最终 Checker 验收；Runner 即使不指定它也负责执行任务，但没有最终验收不能算本教程通过。
- 当前 smoke 每阶段搬运 1 MiB、block size 64 KiB、loops=1。它不是任意规模压力测试。

## 6. 完整 16 项矩阵

确认单项通过后运行下面循环。UBOE 使用专用拓扑，其余协议使用普通 A5 拓扑；串行执行，避免固定端口冲突：

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

最终要求 16 项 PASS，`TOTAL_PASS=16 TOTAL_FAIL=0`。详细输出均重定向到日志，终端只显示摘要。每个 FAIL 都给出日志路径，不能因其他项通过就忽略。

## 7. 怎样判断成功

一次场景需要命令退出码为 0，并同时出现：

```text
HiXL HCCL-VM smoke passed
HCOMM Checker PASS, level=2
PASS HCOMM Runner/Checker task validation
```

三者依次表示脚本检查通过、Checker 任务检查通过、VM 命令完成验证及退出处理。read 输出 JSON 中还必须有 `"consistency":"pass"`；关闭校验得到 `not_checked` 不满足该条件。

Runner 执行真实字节复制并即时比较源/目的共享 backing；Checker 校验实际协议、读写类型、Host/Device 类型、业务范围覆盖、任务终态及完成同步证据，不再重新遍历 buffer 做一轮 memcmp。只看其中任意一个 PASS 或仅有 benchmark 输出都不够。

使用 `--keep-logs` 时，场景日志会打印 `HiXL smoke logs retained at:`，指向 target/initiator 和输出文件目录。汇总中的 rc 也可能来自成功标记检查失败，而不只是 workload 退出码。

## 8. 常见问题

| 现象 | 检查方法 |
|---|---|
| 找不到 `run-hixl` 或参数不识别 | 核对 VM 固定提交及 `VM_INSTALL`，重新构建安装完整版本 |
| 找不到 `device`、ARM 库或 QEMU | 完成 Device 构建、CANN 包部署与 HIXL kernel 复制，核对实际架构 |
| 缺少 JSON 或 kernel 加载失败 | 确认 source 的 CANN 与部署路径一致，JSON 和真实 Device 库与本次构建匹配 |
| `read completed without consistency=pass` | 确认使用教程副本且两端 `--data_verify=true`；再查看 target 日志和输出 JSON |
| `HiXL did not select protocol ...` | 检查实际选路及 EndpointMatcher 日志是否输出；缺日志不能直接视为协议正确 |
| 超时或连接失败 | 确认没有并发矩阵/端口占用，查看两端日志和 Runner 任务终态；超时也算失败 |
| Runner 或 Checker 失败 | 检查失败任务、地址范围和协议绑定；不要去掉 validate-tasks 或精度检查来获得 PASS |
| 旧版本跑过、新源码仍报旧错误 | 检查实际 benchmark、Host 库、Device kernel、JSON 和 VM 二进制是否更新到同一套版本 |

## 9. 已验证环境与边界

以下是本教程的参考组合，不表示其他版本自动兼容：

| 组件 | 本轮版本 |
|---|---|
| Host | Ubuntu 24.04 x86_64 |
| QEMU | 8.2.2（x86_64 主机） |
| CANN | 9.3.0，包含真实 Host HCOMM 与匹配的 AICPU 包 |
| HIXL 实现 | `2384c335`（本 PR 仅增加文档） |
| HCOMM 源码头文件 | `a38c0e1b1` |
| HCCL 源码头文件 | `c5e32b95` |
| CheckerL2 | `fc3f46e580c9a60b0ab4f8ad6c2db7c73cde5dd0` |

更换 CANN、源码或 VM 提交后，应重新构建匹配库并执行矩阵。这里只验证单机上两个模拟 rank 的固定小数据场景，不代表所有拓扑、数据量、并发和真实设备都已验证。

本轮已完成 HIXL 完整构建、VM Host/ARM 组件构建、CANN 包部署检查、UB_CTP D2D 单项和 16 项矩阵；结果为 `TOTAL_PASS=16 TOTAL_FAIL=0`，每项均检查三条成功标记。
