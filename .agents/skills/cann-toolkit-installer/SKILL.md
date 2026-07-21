---
name: cann-toolkit-installer
description: >-
  自动下载并安装华为 CANN Toolkit 与 ops 开发套件。当用户需要安装、更新或重新安装 CANN
  Toolkit/ops 时使用此 skill，适用于：新环境初始化、版本升级。自动获取最新构建版本，
  按 npu-smi 选择 ops 包，静默安装到用户指定目录并输出详细摘要。用户可能的指令是更新
  toolkit、更新 run 包、安装 CANN 基础环境等。
license: CANN Open Software License Agreement Version 2.0
---

# CANN Toolkit / ops 自动安装

自动从华为镜像站下载并安装社区版 CANN Toolkit 与对应芯片 ops 包，包含完整的下载、验证、安装和配置流程。

## 快速开始

提示大约需要 10 分钟，消耗 1～2K tokens

```bash
# 自动检测架构并安装（推荐）；install-path 必填
/cann-toolkit-installer <install-path>

# 指定架构（覆盖自动检测）
/cann-toolkit-installer <install-path> aarch64

# 完整参数
/cann-toolkit-installer <install-path> <arch> <version>
```

## 安装流程

### 1. 参数解析与架构检测

位置参数约定（与「快速开始」一致）：

1. `<install-path>`（必填）
2. `<arch>`（可选）：`x86_64` 或 `aarch64`；省略则自动检测
3. `<version>`（可选）：CANN 版本号；省略则默认 `9.1.0`

- **install-path**：toolkit 与 ops 必须安装到同一路径
- **架构自动检测**（未传 `<arch>` 时）：用 `uname -m`
  - `x86_64|i686|i386` → `x86_64`
  - `aarch64|armv8*|armv7l` → `aarch64`
- 用户传入 `<arch>` 时覆盖自动检测；非法值则停止并询问

### 2. 通过 npu-smi 选择 ops 包

安装 ops 前必须执行：

```bash
npu-smi info
```

从输出中识别 NPU 芯片型号（常见字段如 `Name` / `Chip Name`），再映射为 `${soc_name}`：

| 芯片型号 | `${soc_name}` | ops 包示例 |
| :--- | :--- | :--- |
| Ascend910 | `A3` | `Ascend-cann-A3-ops_${cann_version}_linux-${arch}.run` |
| Ascend910B | `910b` | `Ascend-cann-910b-ops_${cann_version}_linux-${arch}.run` |
| Ascend950 | `950` | `Ascend-cann-950-ops_${cann_version}_linux-${arch}.run` |

匹配规则：

- 输出含 `910B` / `Ascend910B` → `910b`
- 输出含 `950` / `Ascend950` → `950`
- 输出含 `Ascend910`（且不是 910B）→ `A3`

若无法识别芯片型号，停止安装并询问用户指定 `soc_name`，不要猜测。

### 3. 获取最新构建

**获取最新构建日期**：使用 curl 解析镜像站目录获取最新的构建日期（构建日期会动态变化）

```bash
# 获取最新的构建日期目录
latest_date=$(curl -sL "https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master/" | grep -oE 'href="[0-9]+' | sed 's/href="//' | sort -r | head -1)
```

目录结构：

- `master/{build_date}/Ascend-cann-toolkit_{version}_linux-{arch}.run`
- `master/{build_date}/Ascend-cann-{soc_name}-ops_{version}_linux-{arch}.run`

**注意**：

- 构建日期（如 `20260401000324385`）会动态变化，需要实时获取
- 完整 URL 格式：`https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master/{build_date}/`

### 4. 下载 Toolkit 与 ops 包

```bash
base_url="https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master/${latest_date}"

# Toolkit（约 1.2 GB）
wget -O "Ascend-cann-toolkit_${version}_linux-${arch}.run" \
  "${base_url}/Ascend-cann-toolkit_${version}_linux-${arch}.run" \
  --progress=bar:force

# ops（按 soc_name）
wget -O "Ascend-cann-${soc_name}-ops_${version}_linux-${arch}.run" \
  "${base_url}/Ascend-cann-${soc_name}-ops_${version}_linux-${arch}.run" \
  --progress=bar:force
```

- 下载位置：当前工作目录或安装目录

### 5. 下载结果检查（非官方校验和比对）

下载后必须检查，失败则停止安装，不要继续执行 `.run`：

```bash
# 文件必须存在且非空（典型 toolkit ~1.2GB；明显过小视为失败）
for f in \
  "Ascend-cann-toolkit_${version}_linux-${arch}.run" \
  "Ascend-cann-${soc_name}-ops_${version}_linux-${arch}.run"
do
  [[ -f "$f" ]] || { echo "missing: $f"; exit 1; }
  [[ -s "$f" ]] || { echo "empty file: $f"; exit 1; }
  # 可选：打印类型便于排查 HTML 错误页
  file "$f"
done
```

说明：此处**不做**与镜像站公布 sha256 的比对；仅保证下载产物存在且非空。若 `file` 显示为 HTML/text，视为下载失败，停止安装。

### 6. 静默安装

先装 toolkit，再装 ops；两者使用相同 `--install-path`。

```bash
chmod +x Ascend-cann-toolkit_${version}_linux-${arch}.run
chmod +x Ascend-cann-${soc_name}-ops_${version}_linux-${arch}.run

# 1) Toolkit
bash Ascend-cann-toolkit_${version}_linux-${arch}.run \
  --install --force -q --install-path="${install_path}" 2>&1 | tee toolkit_install.log

# 2) ops
bash Ascend-cann-${soc_name}-ops_${version}_linux-${arch}.run \
  --install --force -q --install-path="${install_path}" 2>&1 | tee ops_install.log
```

参数说明：

- `--install`: 安装模式
- `--force`: 强制更新，使用上一次的目录
- `-q`: 静默模式（减少输出）
- `--install-path`: 用户指定安装目录
- `2>&1 | tee *.log`: 同时输出到终端和日志文件

### 7. 结果验证

- 检查本次安装命令输出 / `toolkit_install.log` / `ops_install.log` 中的 ERROR 信息
- 提取安装摘要，也就是安装包最后的 Summary 信息
- 检查环境变量配置状态（提示 `source ${install_path}/cann/set_env.sh` 或实际生成路径）

## 故障排查

### 安装失败

**网络问题：**

```bash
# 测试镜像站连接
curl -I https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master/
```

**权限问题：**

- 检查安装路径写权限
- 某些操作可能需要 sudo

**磁盘空间：**

```bash
# 检查可用空间（至少需要 2GB）
df -h
```

**依赖缺失：**

- bash >= 5.1.16
- python3 >= 3.9.x
- cmake >= 3.16.0

**npu-smi 失败：**

- 确认驱动已安装且 `npu-smi` 在 PATH 中
- 无法识别芯片时，请用户手动指定 `soc_name`（`A3` / `910b` / `950`）

验证时仅以本次 tee 输出的 `toolkit_install.log` 和 `ops_install.log` 为准，不要读取 `${HOME}/var/log/ascend_seclog/ascend_toolkit_install.log`（该文件包含历史累积日志，会干扰判断）

## 注意事项

1. **网络要求**：需要访问华为镜像站（中国境内网络）
2. **安装时间**：5-10 分钟，取决于网络速度
3. **残留文件**：如果安装目录存在旧文件，会显示警告但仍继续
4. 安装目录如果是一个日期格式的，不表示上一个安装版本也是个日期的，因为可能是一个更新的版本安装在了以前的旧目录中
5. toolkit 与 ops 必须安装到同一 `install-path`
6. 安装顺序固定：先 toolkit，后 ops
