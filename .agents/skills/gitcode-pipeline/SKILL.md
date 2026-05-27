---
name: gitcode-pipeline
description: 触发 GitCode PR 流水线，并循环查询流水线状态直到完成。当用户提到触发流水线、查看流水线状态、等待流水线结果、流水线失败、盯一下流水线、盯ci、看一下pr 12306的ci时自动使用此 skill。
---

# GitCode Pipeline Skill

## 执行原则

1. **先列计划**：执行前必须先向用户展示执行计划，然后再执行，计划制定后 MUST 直接执行，DO NOT 等待用户确认后再开始执行（展示计划≠请示批准）
2. **逐步反馈**：每完成一个步骤，向用户输出简要进度信息，不要闷头执行。
3. **日志落盘**：所有失败任务的日志必须保存到 `pipeline_logs` 目录，文件名包含 PR 编号、任务名和时间戳。

## 前置准备

### 提取仓库信息

```bash
repo_url=$(git remote get-url origin)
if [[ $repo_url == git@* ]]; then
  owner=$(echo $repo_url | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\1|')
  repo=$(echo $repo_url | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\2|')
else
  owner=$(echo $repo_url | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\1|')
  repo=$(echo $repo_url | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\2|')
fi
encoded_repo=$(printf '%s' "${owner}/${repo}" | jq -sRr @uri)
```

### 验证环境变量

检查 `GITCODE_API_TOKEN` 是否已设置，未设置则提示用户。

## 执行计划模板

每次执行前，需要先列出计划然后执行

```
## 执行计划

| 步骤 | 操作 | 说明 |
|------|------|------|
| 1 | 检查 PR Label | 查询 PR labels，判断 ci-pipeline-passed 是否存在 |
| 2 | 查询流水线状态 | 仅当无 ci-pipeline-passed label 时执行，检查流水线状态 |
| 3 | 触发流水线（如需要） | 优先 API retry，不适用时评论触发。见步骤 3 触发策略 |
| 4 | 循环查询状态 | 每 60 秒查询一次，直到完成 |
| 5 | 处理结果 | 成功则报告；失败则获取日志并分析 |
| 6 | 修复 | 如果是编译失败，dt用例失败或覆盖率不足，需要尝试修改代码，push代码后重新触发ci
```

**每完成一步，向用户反馈**，例如：
- `✅ 步骤 1 完成：PR labels 中无 ci-pipeline-passed，需要检查流水线`
- `✅ 步骤 2 完成：发现已有流水线 #505783，状态: RUNNING`
- `✅ 步骤 3 完成：已通过 API retry 重触流水线，等待启动...`

## 工作流程

加载 `references/gitcode_pipeline_api.md` 了解接口细节。**执行时必须使用 `scripts/` 目录下的封装脚本，不要直接调用 curl**，封装脚本会在服务端用 jq 提取关键字段，大幅减少返回体积和 token 消耗。

### 脚本一览

| 脚本 | 用途 | 原始返回 → 提取后 | 超时要求 |
|------|------|------------------|----------|
| `scripts/gp-list.sh` | 查流水线列表 | ~2KB → ~200B/条 | **20分钟** |
| `scripts/gp-detail.sh` | 查流水线详情(阶段/Job) | ~50KB → ~500B | 默认 |
| `scripts/gp-sub-output.sh` | 查子流水线步骤输出 | ~300B → ~100B | 默认 |
| `scripts/gp-log.sh` | 查日志(末尾错误摘要) | ~10MB → ~1KB | 默认 |
| `scripts/gp-log-full.sh` | 循环翻页获取全量日志 | 全量日志 → 本地文件 | 默认 |
| `scripts/gp-cov.sh` | 获取覆盖率报告并解压 | 全量日志 → 解压后的覆盖率目录 | 默认 |
| `scripts/gp-trigger.sh` | 评论触发流水线 | ~1KB → ~3B | 默认 |
| `scripts/gp-api-retry.sh` | API retry 重跑指定流水线（需传 content id） | ~1KB → ~3B | 默认 |
| `scripts/gp-analyze-failure.sh` | 一键分析失败：自动穿透子流水线获取失败Job和日志 | 多次API调用 → 失败摘要 | 默认 |
| `scripts/gp-retry.sh` | 评论触发CI + 自动轮询直到完成 | 多次轮询 → 最终结果 | 默认 |
| `scripts/gp-wait.sh` | 循环轮询流水线状态直到完成 | 每 60 秒输出状态 | **60 分钟** |

所有脚本兼容 Windows/Linux/Mac（依赖 bash + jq + curl）。**注意**：`gp-wait.sh` 用于轮询流水线状态时需设置 60 分钟超时。

### 步骤 1：检查 PR Label（CI 通过状态的权威判断）

通过 PR Label 判断 CI 是否真正通过。**流水线的 `status=success` 可能是旧 SHA 的结果，只有 `ci-pipeline-passed` label 才能证明最新代码已通过 CI。**

```bash
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/${PR_NUMBER}?access_token=${TOKEN}" \
  | jq -r '.labels[]?.name'
```

**判断逻辑**：
- 有 `ci-pipeline-passed` label → **CI 已通过最新代码**，直接报告完成，无需后续步骤
- 无 `ci-pipeline-passed` label → 进入步骤 2 查询流水线状态

**为什么必须检查 Label 而非仅看流水线 status**：
当 PR 推送新代码后，旧的流水线 `status` 仍为 `success`（不会自动变为 failed），但该结果是旧 SHA 的。流水线通过后系统会自动添加 `ci-pipeline-passed` label，新代码推送后该 label 会被移除。因此 label 是 CI 状态的唯一可靠来源。

### 步骤 2：查询流水线状态（仅当无 ci-pipeline-passed 时执行）

```bash
bash scripts/gp-list.sh <PR_NUMBER>
```

**返回格式**（key=value，每行一条流水线）：
```
id=526718 status=success sha=659baaa82e1a ref=fix/add-missing-semicolon-in-file-constant created=2026-04-30T16:08:16 pipeline_id=c85338dd... pipeline_run_id=159d8739... pipeline_detail={"hook_id":"42205",...}
```

**判断逻辑**：
- 无输出或 `total=0` → 无流水线，进入步骤 3 触发
- `status=running` → 进入步骤 4 轮询
- `status=failed` 或 `status=canceled` → 进入步骤 5 分析失败
- `status=success` 但无 `ci-pipeline-passed` label → 流水线结果是旧 SHA 的，需要对比 SHA 确认：
  ```bash
  # 获取 PR 当前 HEAD SHA
  HEAD_SHA=$(curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/${PR_NUMBER}?access_token=${TOKEN}" \
    | jq -r '.head.sha')
  # 对比流水线 SHA（从 gp-list.sh 输出的 sha 字段取前 12 位）
  # 如果 SHA 不一致 → 流水线结果已过期，进入步骤 3 重新触发
  # 如果 SHA 一致 → label 可能被手动移除，报告异常
  ```

### 步骤 3：触发流水线

**触发策略（优先 API retry）**：

| 场景 | 触发方式 | 原因 |
|------|----------|------|
| 偶发环境失败，无代码变更（步骤 6 判定） | `gp-api-retry.sh` | 精准重跑，不产生多余记录 |
| 无流水线记录（步骤 2 无输出） | `gp-trigger.sh` | 无可重试对象，必须评论触发 |
| 修复代码后 push（步骤 7） | `gp-trigger.sh` | SHA 已变，需跑新代码 |
| retry API 返回失败 | `gp-trigger.sh` | 兜底方案 |

**执行方式**：

```bash
# 方式 A：API retry（优先）
bash scripts/gp-api-retry.sh <PR_NUMBER>

# 方式 B：评论触发（兜底 / 新代码 push 后）
bash scripts/gp-trigger.sh <PR_NUMBER>
```

触发后等待 10-15 秒再查询。

### 步骤 4：循环查询流水线状态

使用 `gp-wait.sh` 脚本自动轮询，直到状态为 `success`、`failed` 或 `canceled`。

**超时配置**：
调用 `gp-wait.sh` 脚本时，必须设置超时时间为 **60 分钟（3600000 ms）**，避免因流水线长时间运行导致 bash 命令超时中断。

```bash
bash scripts/gp-wait.sh <PR_NUMBER>  # timeout: 3600000ms (60分钟)
```

**轮询完成后反馈**：
- 成功 → `✅ 流水线 #526718 通过！`
- 失败 → `❌ 流水线 #526718 失败，正在分析...`，进入步骤 5

### 步骤 5：失败处理 — 分析失败 Job

#### 5.1 查询主流水线详情

从步骤 2 的输出中提取 `pipeline_id`、`pipeline_run_id`、`pipeline_detail`：

```bash
bash scripts/gp-detail.sh <pipeline_id> <pipeline_run_id> '<pipeline_detail>'
```

**返回格式**：
```
pipeline_name=cann_ge_all status=FAILED
[stage] 获取pr文件: COMPLETED
[stage] 子流水线: FAILED
[job]   compile: FAILED id=427f909686304e13be33f2ffb9fa07e1 task=official_devcloud_subPipeline step_id=20357724a95b48aa98b5f3934c833cfe
[job]   llt: FAILED id=0aa38cc75b8246f9907f7d7b69dd7ca1 task=official_devcloud_subPipeline step_id=0aa38cc75b8246f9907f7d7b69dd7ca1
```

**逻辑**：
- 只关注 `FAILED` 状态的阶段和 Job
- **所有 FAILED 的 Job 都必须逐个分析，禁止跳过任何一个**
- 同一阶段内可能有多个并行 Job（如 compile 和 llt），每个都需要独立分析失败原因
- 只有确认某个 Job 的失败是另一个 Job 失败的级联结果时（如子流水线 detail 返回 null/status=null），才可在报告中标注为级联失败，但仍需给出证据
- 如果 Job 的 `task=official_devcloud_subPipeline`，需要进入 5.2 获取子流水线信息
- 其他 task 类型（如 `official_devcloud_cloudBuild`），直接用输出中的 `id=` 字段进入 5.3 获取日志

> **禁止绕过脚本直接调用 curl 构造嵌套 JSON 请求**。`pipeline_detail` 包含嵌套 JSON，在 shell 中拼接会导致引号转义错误。所有需要调 API 的场景都已封装为 `scripts/` 下的脚本，必须通过脚本调用。

#### 5.2 获取子流水线信息

```bash
bash scripts/gp-sub-output.sh <pipeline_id> <pipeline_run_id> '<pipeline_detail>' <step_id>
```

**返回格式**：
```
sub_pipeline_id=dcd161850837402293f0c47cda6b9921 sub_pipeline_run_id=c3d9c3663189481ca8d812c3046a4d95
```

然后用返回的 `sub_pipeline_id`、`sub_pipeline_run_id` 加上**原始的** `pipeline_detail`，调用 `gp-detail.sh` 获取子流水线的失败 Job 列表：

```bash
bash scripts/gp-detail.sh <sub_pipeline_id> <sub_pipeline_run_id> '<pipeline_detail>'
```

#### 5.3 获取失败 Job 的日志

从步骤 5.1/5.2 的 `gp-detail.sh` 输出中提取失败 Job 的 `id` 字段：

```bash
JOB_ID=$(bash scripts/gp-detail.sh <pipeline_id> <pipeline_run_id> '<pipeline_detail>' | grep "FAILED" | grep -oP 'id=\K[^\s]+' | head -1)
```

然后传入 `gp-log.sh`：

```bash
bash scripts/gp-log.sh <pipeline_id> <pipeline_run_id> <job_id> '<pipeline_detail>' [lines]
```

**返回格式**（包含 error/fail/fatal 的日志行，默认最多 20 行）：
```
[2026/04/30 14:39:05] file_constant_kernel.cc:41:15: error: expected ';' at end of member declaration
[2026/04/30 14:39:05] make[2]: *** [...] Error 1
[2026/04/30 14:39:05] Failed command: make all -j8
```

**注意**：
- 日志默认倒序获取最后 500 行，然后 grep 错误行
- `pipeline_id`/`pipeline_run_id` 在子流水线场景下使用子流水线的值
- `job_id` 来自 `gp-detail.sh` 输出中失败 Job 行的 `id=` 字段

### 步骤 6：分析日志并报告

首先判断是**编译失败**还是**用例执行失败**：
- 编译失败 → 日志中包含编译错误（`.cc` 文件 + error 行号 + `make` 错误）
- 用例执行失败 → 日志中包含 `tests passed` / `tests failed` / `FAILED` 的 CTest 汇总

根据日志内容判断失败原因，进入步骤 7 处理：

**多 Job 失败时的报告格式**（每个 FAILED Job 必须单独列出）：
```
### 失败分析摘要
| Job | 失败原因 | 类型 | 是否需要修复 |
|-----|----------|------|-------------|
| compile | 第三方依赖下载 HTTP 429 | 环境偶发 | 否 |
| llt | 子流水线未启动（detail 返回 null） | compile 的级联失败 | 否（随 compile 重试自动恢复） |
```

| 错误类型 | 处理方式 | **必须调用脚本** |
|----------|----------|------------------|
| 编译错误 | 报告具体文件和行号 → 步骤 7 修复代码后 push 并重触 CI | `gp-log.sh` |
| UT/ST 执行错误 | 报告失败用例 → **进入步骤 6.1** | `gp-log-full.sh` |
| 覆盖率不足 | 报告覆盖率缺口 → **进入步骤 6.2** | **`gp-cov.sh`** |
| 代码告警 | 评估是否误报 → 误报则报告停止，非误报则步骤 7 修复后 push 并重触 CI | `gp-log.sh` |
| 环境/基础设施错误（已知） | 报告用户后停止 | - |
| 环境/基础设施错误（偶发） | 步骤 3 使用 `gp-api-retry.sh` 重试流水线 | `gp-api-retry.sh` |

**误报判断原则**：
- 已知的环境问题或工具链 bug
- 与本次修改无关的历史告警
- 静态分析工具的误判

如果判断为误报，立即停止修复尝试，向用户报告：

```
⚠️ 流水线失败，但经分析为误报：
   任务: static-check
   告警: xxx
   原因: 与本次修改无关/已知误报
   建议: 忽略或联系维护人员
```

#### 6.1 用例执行失败 — 定位失败用例（严格顺序，禁止跳步）

当步骤 6 确认为用例执行失败时，**必须按以下顺序执行，禁止跳过任何步骤**：

**第 1 步：从尾部日志提取 CTest 汇总**

用 `gp-log.sh` 获取尾部日志，从中提取：
- 失败的 target 数量和名称（如 `ut_libge_multiparts_utest (Failed)`）
- 通过率（如 `92% tests passed, 1 tests failed out of 12`）
- exit code（exit code 1 = 断言失败，exit code 139 = segfault）

此时已经知道失败的是哪个二进制（如 `ut_libge_multiparts_utest`）。

**第 2 步：并行执行全量日志获取 + 失败二进制编译**

尾部日志已提供失败二进制名称，接下来**必须并行启动两个 subagent**，节省总耗时：

| subagent | 任务 | 说明 |
|----------|------|------|
| subagent A | 获取全量日志 | 用 `gp-log-full.sh` 循环翻页获取全量日志，搜索 `[  FAILED  ]` 定位具体 gtest test case |
| subagent B | 编译失败二进制 | fetch PR 分支代码到本地 → 使用 `ge-dt-runner` skill 编译失败的二进制 target |

```bash
# subagent A：获取全量日志
bash scripts/gp-log-full.sh <pipeline_id> <pipeline_run_id> <job_id> '<pipeline_detail>'
# 日志保存到 pipeline_logs/<job_id>_full.log
grep '\[  FAILED  \]' pipeline_logs/<job_id>_full.log
```

```bash
# subagent B：编译失败二进制（必须 fetch PR 分支，禁止使用已有编译产物）
# 1. 先 git fetch PR 分支（参考步骤 7.2）
# 2. 使用 ge-dt-runner skill 编译失败的二进制 target（如 ut_libge_multiparts_utest）
# 3. 禁止偷懒直接使用旧二进制，必须确保代码与线上 PR 一致
```

**禁止偷懒直接使用旧编译产物**：
- 旧二进制可能存在 protobuf 注册冲突、符号未定义、架构不匹配等问题
- 必须先 fetch PR 分支最新代码，再使用 `ge-dt-runner` skill 编译
- 只编译失败的 target，不要全量编译

**第 3 步：主 agent 协调 — 并发等待与即时反馈**

两个 subagent 并发期间，主 agent **必须**：
- 向用户报告两个任务的进度状态（如"全量日志获取中（已下载 X MB）... 编译进行中..."）
- **禁止**闷头等待不反馈

当其中一个 subagent 先完成时，主 agent **必须立即处理已完成的结果，不等另一个**：

| 谁先完成 | 主 agent 立即执行 |
|----------|-------------------|
| subagent A（日志）完成 | 立即从全量日志中搜索 `[  FAILED  ]` 定位失败用例，提取断言详情（`grep -B 20`），向用户报告具体失败用例名、文件、行号、期望值/实际值 |
| subagent B（编译）完成 | 向用户报告编译结果（成功/失败），如编译失败则报告错误 |

当两个 subagent 都完成后：
- 从全量日志拿到失败用例名
- 编译产物的二进制 + `--gtest_filter=<失败用例名>` 运行本地验证
- 进入步骤 7 修复并重试

#### 6.2 覆盖率不足 — 定位未覆盖代码行

当确认失败原因为覆盖率不足时，**必须使用 `gp-cov.sh` 脚本获取覆盖率报告**：

```bash
bash scripts/gp-cov.sh <pipeline_id> <pipeline_run_id> <job_id> '<pipeline_detail>'
```

**查找未覆盖行**：

```bash
# 在 HTML 覆盖率报告中搜索未覆盖标记
grep 'tlaUNC' pipeline_cov/<job_id>_cov/inc_cov/result/<test_type>/<file>.gcov.html
```

脚本会输出解压后的目录路径，解压后的目录结构：
```
pipeline_cov/<job_id>_cov/
├── inc_cov/
│   ├── diff_file@<源文件路径>    # PR新增代码的diff
│   ├── add_ut_<源文件>.info      # 增量覆盖率info文件
│   └── result/
│       └── <测试类型>/<源文件>.gcov.html  # HTML覆盖率报告
```

**覆盖率目标**：
- UT 测试：增量覆盖率需 ≥ 90%
- ST 测试：增量覆盖率需 ≥ 80%

**修复流程**：
1. 一定要先获取哪个源文件哪些行未覆盖，再决定增加ut/st用例，不要盲目增加用例．
2. 编写测试用例覆盖该行（参考现有测试文件风格）
3. 本地编译并运行验证（使用 `ge-dt-runner` skill）
4. push 代码 → 触发 CI

### 步骤 7：修复并重试流水线

> **直接按下方表格执行，DO NOT 向用户确认。** 表格已覆盖所有场景，没有歧义。

**重触发 CI 的前置条件（必须满足全部）：**
- [ ] 已定位到具体失败的用例名或编译错误位置
- [ ] 已分析根因（编译错误/断言失败/crash/环境问题/偶发）
- [ ] 如果是代码问题：已修复并 push
- [ ] 如果是环境问题：已向用户报告

**禁止**在未满足前置条件的情况下重触发 CI。

根据步骤 6 的分析结果，按以下流程处理：

#### 7.1 判断是否需要修复代码

| 错误类型 | 是否修复代码 | 后续操作 |
|----------|-------------|----------|
| 编译错误 | **是** | 修复代码 → push → 步骤 3 `gp-trigger.sh` |
| UT/ST 执行错误 | **是** | 修复代码或修复用例 → push → 步骤 3 `gp-trigger.sh` |
| 覆盖率不足 | **是** | 补充用例 → push → 步骤 3 `gp-trigger.sh` |
| 代码告警（非误报） | **是** | 修复告警 → push → 步骤 3 `gp-trigger.sh` |
| 代码告警（误报） | 否 | 报告用户后停止 |
| 环境/基础设施错误（已知问题） | 否 | 报告用户后停止 |
| 环境/基础设施错误（偶发） | 否 | 步骤 3 使用 `gp-api-retry.sh` 重试 |

#### 7.2 需要修复代码时：拉取 PR 分支到本地

如果 PR 分支不在本地，先 fetch 到本地：

```bash
# 从 gp-list.sh 输出的 ref 字段获取分支名
BRANCH_NAME="<ref字段值>"

# 注意：如果 PR 来自 fork，origin 可能找不到该分支
# 需要使用 fork remote（如 hgjupstream）或添加 fork remote
git fetch <remote> ${BRANCH_NAME}
git checkout -b ${BRANCH_NAME} <remote>/${BRANCH_NAME}
```

#### 7.3 修复代码并 push

1. 根据日志定位具体文件和行号
2. 修复代码
3. 提交并 push：
```bash
git add <修改的文件>
git commit -m "fix: <简要描述修复内容>"
git push <remote> ${BRANCH_NAME}
```

4. push 后重新触发 CI（步骤 3，使用 `gp-trigger.sh`，因为 SHA 已变）

## 实践经验

> 以下经验来自实际盯 CI 过程中的踩坑总结，执行时务必遵守。

### 1. 禁止使用 Python 解析 API 返回值

API 返回的 JSON 字段嵌套复杂且结构可能变化，用 Python 脚本解析极易因字段缺失或 KeyPath 错误而报错或无输出。

**正确做法**：用 `grep -oP` 做简单字段提取。
```bash
# 提取流水线 id 和 status
curl -s "...pipeline?..." | grep -oP '"(id|status)":"?[^",}]+' | head -10

# 从日志中提取错误行
jq -r '.log' /tmp/log.json | grep -iE 'error|fatal|fail' | tail -20
```

### 2. curl body 用文件传递，避免 shell 转义问题

`pipeline_detail` 中包含嵌套 JSON，在 shell 中用变量拼接（`$PIPELINE_DETAIL`）会被二次转义导致参数错误（PARAMETER_ERROR）。

**正确做法**：将完整 JSON 写入临时文件，用 `--data-binary @file` 传递。
```bash
cat > /tmp/body.json << 'ENDJSON'
{
    "pipeline_run_id": "xxx",
    "pipeline_detail": "{\"hook_id\":\"42205\", ...}"
}
ENDJSON

curl -s --request POST "https://api.gitcode.com/api/v5/repos/..." \
  --header 'Content-Type: application/json' \
  --data-binary @/tmp/body.json
```

**注意**：heredoc 使用 `'ENDJSON'`（带引号）防止 shell 变量展开。如果 body 中需要引用变量，先写文件再用 `sed` 替换。

### 3. 日志定位用倒序获取

编译日志通常 10MB+（`end_offset` 达到 1000 万+），正序逐页翻阅效率极低。错误信息都在日志末尾。

**正确做法**：倒序获取最后一页（`sort: desc`，`limit: 500`），然后 `grep` 关键词。
```bash
cat > /tmp/log_tail.json << 'ENDJSON'
{
    "pipeline_detail": "...",
    "start_offset": "0",
    "end_offset": "0",
    "limit": 500,
    "sort": "desc"
}
ENDJSON

curl -s --request POST ".../jobs/{job_id}/logs?..." \
  --data-binary @/tmp/log_tail.json | jq -r '.log' | grep -iE 'error|fatal|fail' | tail -20
```

### 4. 子流水线层级关系要严格遵守

`official_devcloud_subPipeline` 类型的 step 不能直接拿 job id 查日志，必须：
1. 调步骤输出接口（步骤 3a）获取 `sub_pipeline_id` 和 `sub_pipeline_run_id`
2. 用子 pipeline 信息调详情接口（步骤 3b）获取子流水线中的失败 job
3. 用子流水线的 `pipeline_id`、`pipeline_run_id`、`job_id` 查日志

**常见错误**：拿父流水线的 job_id 去查子流水线的日志，会返回 PARAMETER_ERROR。

### 5. 轮询用 list API，detail API 仅在需要时调用

| 场景 | 使用接口 | 原因 |
|------|----------|------|
| 轮询状态 | list API（`merge_requests/{id}/pipeline`） | 响应小、速度快 |
| 查看具体 job 失败原因 | detail API（`pipelines/{id}/pipeline-runs/detail`） | 需要 stages/jobs 详情 |
| 获取日志 | log API（`.../jobs/{job_id}/logs`） | 需要具体日志内容 |

### 6. PR 源分支可能来自 fork

如果 PR 的 `head.repo.full_name` 与目标仓库不同（如 `stevenaw0/ge` vs `cann/ge`），说明 PR 来自 fork。此时：
- `git fetch origin` 找不到该分支
- 需要使用已有的 fork remote（如 `hgjupstream`）或添加 fork remote
- push 时要推送到 fork remote 而非 origin

### 7. 日志 API 翻页方法

日志 API 支持循环翻页获取全量日志，采用游标式分页。

翻页原理：
- 第 1 页：`start_offset="0", end_offset="0"` → API 自动确定窗口并返回日志
- 后续页：用响应返回的 `start_offset` 和 `end_offset` 作为下次请求的参数
- API 返回新的 `start_offset`、`end_offset` 和对应日志片段
- 当 `has_more=false` 时停止

编译错误通常在日志末尾，用 `gp-log.sh`（desc 模式）即可定位。**用例执行失败**的 gtest 详细输出通常在日志中间，必须用 `gp-log-full.sh` 获取全量日志后搜索。

### 8. 本地复现失败用例的约束

禁止直接运行已有的编译产物（stale binary）来复现问题。必须：
1. **先 fetch PR 分支最新代码**到本地（步骤 7.2）
2. **使用项目中的本地测试执行 skill** 编译和运行（不要手动 cmake/make）
3. 只运行失败的 target 或具体用例（`--gtest_filter`），**禁止全量运行**

不遵守此约束的后果：stale binary 可能存在 protobuf 注册冲突、符号未定义、架构不匹配等问题，导致无法正确复现 CI 上的实际失败。

### 9. 禁止未定位根因就重触发 CI

在以下情况下**禁止**重触发 CI：
- 未定位到具体失败的用例名
- 未分析出失败根因（编译错误/断言失败/crash/环境问题）
- 仅凭"可能是偶发"就重试（除非确认是已知偶发环境问题）

正确做法：先完成分析，再决定是修复代码后 push 还是报告用户。

## 环境变量

需要设置 `GITCODE_API_TOKEN`：

```bash
export GITCODE_API_TOKEN="your_token_here"
```
