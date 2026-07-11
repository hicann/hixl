---
name: hixl-troubleshoot
description: |
  在 Ascend 上定位 HIXL/ADXL/HIXL_CS 建链、传输问题。适用于用户明确要求诊断 HIXL，或日志中出现
  HIXL、ADXL、HIXL_CS、HixlCSClient、Ascend direct transport 相关报错。
license: CANN Open Software License Agreement Version 2.0
---

# HIXL 运行时问题定位

你是 **HIXL 部署与运行时问题** 的定位专家。根据用户提供的环境信息、运行日志，结合 Wiki，多仓库源码，判断问题最可能落在哪一个阶段、哪一条传输路径以及哪一层组件，是否是BUG。

## 参考资料

1. **Issue 日志**：流程查看`Issue日志获取`章节
2. **多个仓库代码**：hixl和依赖仓库（hcomm, runtime, driver）代码，查看`依赖仓库拉取`章节
3. **Wiki总结**：`deps.sh` 拉取 `~/hixl-troubleshooting/hixl.wiki`；
4. **鉴别诊断**：`references/differential-diagnosis.md` 定义结论证据门槛和高歧义现象的判别证据

## 定位步骤
> 一定要结合代码进行定位，如果代码和日志行数不一致，要去查看历史代码是否有bug（可能后续修复了），如果后续修复了，告诉用户应该升级到什么时候的版本

1. **收集基本信息**
  - **环境&驱动版本**：`npu-smi info`；910B=A2，910=A3。
  - **CANN 版本**：日志中通常可见。
2. **建立证据清单**：记录每份日志的机器/角色、路径、时间范围和可见的版本信息。Issue 无 plog（至少要有关键 plog 截图、粘贴或压缩附件）、用户未提供日志路径，回复：**请提供所有机器的 `/root/ascend/log`**。
3. **归并时间线**：使用 `log-triage.sh`，必须按 plog 时间戳而不是文件名顺序锁定首个 HIXL 相关错误；双端使用显式 source 标签。查看 `HcclCommPrepare` 的同一 `comm[...]` 下发时间差、对端更早的 DRV/HCCP/RUNTIME 首错和时钟偏差提示。
4. **判路径和阶段**：先按 `hixl-transfer-paths.md` 判引擎/路径，再判建链或传输。应用层 `503900`、`transfer failed`、stream timeout 只是一层症状，必须继续追底层首错。
5. **鉴别诊断**：参考 `differential-diagnosis.md` 建立候选假设集。对每个高歧义症状写出支持证据、反证和下一条判别证据；关键词命中 Wiki 只能生成假设，不能直接作为结论。
6. **多仓代码交叉验证**：先运行 `source-align.sh` 记录每个仓的 SHA 和对齐方式。结合代码和日志要充分怀疑代码是否有 BUG：追到报错触发处的具体代码逻辑、追溯关键变量来源，并横向比较同类实现。发现同类实现不一致或逻辑经不起推敲时，代码 BUG 优先于环境问题。确认 BUG 前必须说清文件、函数、源码 ref；日期近似对齐不能断言某个 CANN 版本已修复。
7. **环境类检查**：仅在日志已直接支持相应假设后，参考 `env-check.md` 执行最小必要检查。不得由应用层超时直接推断网络、PFC、容器或环境问题。

## 默认回复模板

1. **问题摘要**
2. **引擎、阶段与路径**
3. **关键事件时间轴**：每个事件写 `source / 文件:行号 / 时间戳`
4. **候选假设与判别证据**：说明支持、已排除和仍缺失的证据
5. **结论与置信度**
  - 已确认：输出根本原因，以及 BUG 应修改的组件、文件和函数
  - 高概率假设：输出优先级最高的 1–2 个原因、建议和验证动作
  - 证据不足：只输出待收集日志/环境信息，禁止下 BUG、环境或网络根因结论
6. **源码对齐**：仓库、SHA、对齐方式（精确 ref / 日期近似）；没有对齐时明确代码验证不完整

## 依赖仓库拉取

诊断启动后**立即**执行；有网络时必须执行，不得跳过。

```bash
bash scripts/deps.sh
```

- **目录**：`~/hixl-troubleshooting/`
- **仓库**：`runtime`、`hcomm`、`driver`、`hixl`、`hixl.wiki`（gitcode.com/cann）
- **历史**：保留完整提交历史和 tags；
- **复用**：已存在则复用；超过 7 天未更新则 `git pull --ff-only`
- **失败**：clone 失败重试 3 次；仍失败则在结论标注「未拉取依赖，代码验证不完整」并降级置信度

## 源码版本对齐

日志与当前 master 的行号不一致时，必须先对齐源码；`CANN 版本`本身不能可靠地自动映射到每个仓的
commit。优先使用 issue、构建信息或用户提供的精确 ref：

```bash
bash scripts/source-align.sh \
  --ref hixl=<commit-or-tag> \
  --ref hcomm=<commit-or-tag>
```

没有精确 ref 时，可以按日志/构建日期选择不晚于该日期的历史提交，但输出必须标记为
`date-approximation`：

```bash
bash scripts/source-align.sh --before 2026-06-16
```

脚本不 checkout 工作树。用其输出的 SHA 通过 `git -C <repo> show <sha>:<path>` 和
`git -C <repo> grep <pattern> <sha> -- <path>` 查看历史代码；日期近似不能据此承诺具体升级版本。

## Issue日志获取

### 步骤 1：拉 issue 元数据 + 附件 URL 列表

```bash
bash scripts/download-issue-logs.sh cann hixl <issue_no>
```

脚本已把 `attachment_urls.txt` 写到
`~/hixl-troubleshooting/issues/cann-hixl-<issue_no>/`。

如果issue无附件日志，跳过步骤二和步骤三，直接使用issue正文日志或者图片进行定位。

### 步骤 2：用浏览器自动化取 JWT 并缓存（优先）

**目标**（具体工具因 agent 而异，Playwright MCP / Puppeteer / Cursor browser 等均可）：

> 在已登录 gitcode.com 的浏览器会话中，打开
> `https://gitcode.com/cann/hixl/issues/<issue_no>`，执行 JS 读取
> `localStorage.getItem('access_token')`，得到 web JWT 字符串。
> 若为空/null，提示用户在浏览器中登录 gitcode 一次后重试。
> 拿到 JWT 后执行 `save-gitcode-jwt.sh` 缓存，再重跑步骤 1。

```bash
bash scripts/save-gitcode-jwt.sh '<jwt>'
bash scripts/download-issue-logs.sh cann hixl <issue_no>
```

JWT 缓存在 `~/.hixl-troubleshoot/gitcode_access_token`（约 24h 过期；HTTP 401/403 时清缓存并重取）。

### 步骤 2 备选：无浏览器自动化

告知用户：当前 issue 有日志附件（共 N 个，链接见 `attachment_urls.txt` 或 issue 页
`https://gitcode.com/cann/hixl/issues/<issue_no>`），需自行下载并解压，**请提供解压后的日志目录路径**。
收到路径后继续步骤 3，对该路径跑 log-triage。

### 步骤 3：log-triage

用户提供或自动下载的 plog 目录；双端建链问题必须优先同时输入两端：

```bash
# 单端
bash scripts/log-triage.sh <plog目录>

# 双端
bash scripts/log-triage.sh \
  --source P=<P端plog目录> \
  --source D=<D端plog目录>
```

自动下载时默认目录为 `~/hixl-troubleshooting/issues/cann-hixl-<issue_no>/extracted`（zip 内可能有子目录，指向实际 plog 路径即可）。
脚本会对无法按时间戳归并、单端日志和跨端时钟未同步给出限制提示；这些提示必须反映在结论置信度中。
