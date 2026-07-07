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
3. **Wiki总结**：`deps.sh` 拉取 `~/hixl-troubleshooting/hixl.wiki`；无网络时参考 `references/guides.md`

## 定位步骤

1. **收集基本信息**
  - **环境&驱动版本**：`npu-smi info`；910B=A2，910=A3。
  - **CANN 版本**：日志中通常可见。
2. **锁定 plog 首条致命 ERROR**（非 Mooncake 应用层超时）；`log-triage.sh` 辅助
3. **判路径**：参考 hixl-transfer-paths.md
4. **判阶段**：建链 vs 传输
5. **多仓代码交叉验证** 结合代码和日志要充分怀疑代码是否有bug：不能看到报错关键词命中已有场景、或现象像"容器/环境类问题"就直接下结论。只要有源码可查，必须先追到报错触发处的具体代码逻辑、追溯关键变量的来源，并在代码库中找同类实现横向比对是否一致；发现同类场景实现不一致、或代码逻辑本身经不起推敲时，代码 bug 应优先于环境问题作为结论。确认结论前，要能说清楚"具体是哪个文件、哪个函数"，说不清楚就说明代码没看够，不能下结论。
6. Issue 无 plog（至少要有关键plog截图，plog粘贴，plog压缩附件）、用户未提供日志路径，回复：**请提供所有机器的 `/root/ascend/log`**，建链类问题无双端日志建议用户提供双端日志。
7. **环境类检查** 参考env-check.md

## 默认回复模板

1. **问题摘要**
2. **引擎、阶段与路径**
3. **关键事件时间轴**
4. **原因**
  - 如果是BUG，输出根本原因，应该哪个组件哪里修改
  - 如果是使用问题，输出原因和建议

## 依赖仓库拉取

诊断启动后**立即**执行；有网络时必须执行，不得跳过。

```bash
bash scripts/deps.sh
```

- **目录**：`~/hixl-troubleshooting/`
- **仓库**：`runtime`、`hcomm`、`driver`、`hixl.wiki`（gitcode.com/cann）
- **复用**：已存在则复用；超过 7 天未更新则 `git pull --ff-only`
- **失败**：clone 失败重试 3 次；仍失败则在结论标注「未拉取依赖，代码验证不完整」并降级置信度

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

用户提供或自动下载的 plog 目录：

```bash
bash scripts/log-triage.sh <plog目录>
```

自动下载时默认目录为 `~/hixl-troubleshooting/issues/cann-hixl-<issue_no>/extracted`（zip 内可能有子目录，指向实际 plog 路径即可）。
