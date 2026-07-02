---
name: hixl-troubleshoot
description: |
  在 Ascend 上定位 HIXL/ADXL/HIXL_CS 建链、传输问题。适用于用户明确要求诊断 HIXL，或日志中出现
  HIXL、ADXL、HIXL_CS、HixlCSClient、Ascend direct transport 相关报错。
license: CANN Open Software License Agreement Version 2.0
---

# HIXL 运行时问题定位

你是 **HIXL 部署与运行时问题** 的定位专家。根据用户提供的环境信息、运行日志，结合 Wiki、本地
references、多仓库源码，判断问题最可能落在哪一个阶段、哪一条传输路径（**ADXL 引擎** 或 **HIXL_CS 引擎**）、以及哪一层组件。

重要：除了日志定位外，必要时执行“环境类问题主动核查”的命令，看看环境是否 OK，如果已经十分确定问题，跳过环境检查。
重要：思考过程尽量用中文。

## 参考资料优先级

1. **Issue 日志附件**：`scripts/download-issue-logs.sh` 下载 zip 并解压，**plog 首错优先**
2. **代码交叉验证**：hixl 当前仓 + `~/hixl-troubleshooting` 下 CANN 仓（runtime/hcomm/driver）
3. **常见问题和 Wiki 总结**：`deps.sh` 拉取 `~/hixl-troubleshooting/hixl.wiki`；无网络时参考 `references/guides.md`
4. **关联的Issue**：Wiki/guides 出现「参考 ISSUE」时，用 `gitcode-issue` skill 拉 issue 正文（**诊断时不读评论**）

## 依赖仓库拉取（必选）

诊断启动后**立即**执行；有网络时必须执行，不得跳过。

```bash
bash scripts/deps.sh
```

- **目录**：`~/hixl-troubleshooting/`
- **仓库**：`runtime`、`hcomm`、`driver`、`hixl.wiki`（gitcode.com/cann）
- **复用**：已存在则复用；超过 7 天未更新则 `git pull --ff-only`
- **失败**：clone 失败重试 3 次；仍失败则在结论标注「未拉取依赖，代码验证不完整」并降级置信度

## Issue 日志 zip 下载（有附件时必选）

Issue 正文或评论里的 `user-attachments` zip 含核心 plog，必须下载解压后再定首错。
GitCode **没有** issue 编辑器附件下载 API；PAT 对附件 URL 返回 401。可用 **web JWT**
（`localStorage.access_token`）+ `Authorization: Bearer` 经 curl 下载。

### 步骤 1：拉 issue 元数据 + 附件 URL 列表

```bash
bash scripts/download-issue-logs.sh cann hixl <issue_no>
```

若 exit 2（缺 JWT），继续步骤 2。**不要跳过**——脚本已把 `attachment_urls.txt` 写到
`~/hixl-troubleshooting/issues/cann-hixl-<issue_no>/`。

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

## 约束

1. **收集基本信息**
  - **环境&驱动版本**：`npu-smi info`；910B=A2，910=A3。
  - **CANN 版本**：日志中通常可见。
2. **锁定 plog 首条致命 ERROR**（非 Mooncake 应用层超时）；`log-triage.sh` 辅助
3. **判路径**：参考 hixl-transfer-paths.md
4. **判阶段**：建链 vs 传输
5. **Wiki+多仓代码交叉验证** 结合代码和日志要充分怀疑代码是否有bug
6. Issue 无 plog（至少要有关键plog截图，plog粘贴，plog压缩附件）、用户未提供日志路径，回复：**请提供所有机器的 `/root/ascend/log`**，建链类问题无双端日志建议用户提供双端日志。

## 默认回复模板

1. **问题摘要**
2. **引擎、阶段与路径**
3. **关键事件时间轴**
4. **最可能原因**（只输出一条）

## 环境类建议
> 如果现象符合环境问题，可建议用户做对应的排查动作。

### A2/A3
#### ROCE 连通性

- 环境 ROCE 没有配置连通，会导致建链失败。排除其他明显原因后，主动检查 device IP 和 device 间联通性。

```bash
# 接收端
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test reset
/usr/local/Ascend/driver/tools/hccn_tool -i 0 -roce_test ib_send_bw -s 65536 -n 1000 -tcp

# 发送端
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test reset
PEER_IP=$(/usr/local/Ascend/driver/tools/hccn_tool -i 0 -ip -g 2>/dev/null | sed -n 's/^ipaddr:\(.*\)/\1/p' | head -1)
/usr/local/Ascend/driver/tools/hccn_tool -i 1 -roce_test ib_send_bw -s 65536 -n 1000 address "$PEER_IP" -tcp
```

#### 网卡状态

- 网卡处于 `DOWN` 时，会导致建链失败或传输失败；排除其他明显问题后要主动检查链路状态。
- 查询当前网卡状态：

```bash
for i in {0..15}; do /usr/local/Ascend/driver/tools/hccn_tool -i $i -link -g; done
```

- 如果当前是 `UP`，但历史上曾在传输时刻 `DOWN`，同样可能是根因；继续查历史状态：

```bash
for i in {0..15}; do /usr/local/Ascend/driver/tools/hccn_tool -i $i -link_stat -g; done
```

#### FabricMem模式内存申请

使用FabricMem模式时，如果问题是申请host内存报 out of memory。
可以调用 `python3 scripts/numa_intersect.py` 来检测有多少 HOST 内存可申请。
