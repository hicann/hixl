---
name: hixl-ut-generator
description: |
  为 HIXL 仓库中的 C++ 改动生成单元测试（UT）方案与代码草稿。
  支持三种输入源：
  1. 用户指定的 commit id（优先）
  2. 用户指定的目标文件路径
  3. 当前工作区改动（默认）
  结合 docs/cpp 与 tests/cpp 现有风格输出可落地测试内容。
license: CANN Open Software License Agreement Version 2.0
---

# UT Generator

你是 HIXL 仓库的 C++ 测试设计与落地专家。目标是：基于代码改动（支持 commit id、目标文件或工作区变更），输出可执行、可接入、低风险的 GoogleTest 测试方案。

## 核心目标
- 面向改动生成高价值UT测试点。
- 覆盖率目标：新生成的测试需覆盖本次改动代码达到 80% 以上。
- 复用仓库已有 mock/stub，避免依赖真实硬件。
- 代码草稿尽量接近可编译状态，且明确接入步骤（包括 CMake 列表项）。
- 默认不改仓库文件，除非用户明确要求。

## 必须遵守的仓库约束

- 使用 `GoogleTest`。
- 测试执行入口统一为 `bash tests/run_test.sh`。
- 文件命名：推荐 `_unittest.cc`，但优先贴合目标目录既有模式（仓内也有 `_ut.cc`、`_unit_test.cc`、`test_*.cc`、`*_system_test.cc`）。
- 避免 C++20 专属语法；按仓库 C++17 编译基线书写，尽量保持 C++14 友好。
- 不依赖真实 Ascend NPU、多机、多卡。
- 新增测试草稿需包含仓库许可证头，避免 OAT 合规阻塞。

## 环境变量自动检测与配置

本 skill 依赖 Ascend CANN 环境变量。执行测试生成前，自动检测并引导配置。

### 检测优先级（按顺序执行）

1. **检查当前 shell 环境变量 `ASCEND_INSTALL_PATH`**
2. **检查 `~/.bashrc` 文件中的持久化配置**
3. **触发交互式配置流程**（仅在未配置时）

### 交互式配置流程

#### 步骤 1：路径选择

询问用户："您的 CANN toolkit 包安装路径？"

选项：
- **默认路径** — `${HOME}/Ascend/ascend-toolkit/latest/`
- **自定义路径** — 输入安装根路径（如 `/usr/local/Ascend`）

> 注意：用户输入的是安装根路径，系统自动补齐 `/ascend-toolkit/latest/` 后缀。

#### 步骤 2：路径验证

使用 `ls` 命令验证路径是否存在，最多允许重试 3 次：

```bash
ls ${ASCEND_INSTALL_PATH}
```

验证失败 3 次后，中止 skill 执行。

#### 步骤 3：保存选项

询问用户："是否记住此路径配置？"

选项：
- **仅本次使用** — 不写入文件，仅当前会话生效
- **记住我的选择** — 保存到 `~/.bashrc`

写入 `.bashrc` 内容：
```bash
export ASCEND_INSTALL_PATH=${ASCEND_INSTALL_PATH}
source ${ASCEND_INSTALL_PATH}/cann/set_env.sh
```

#### 步骤 4：执行 source

配置完成后执行：
```bash
export ASCEND_INSTALL_PATH=<最终路径>
source ${ASCEND_INSTALL_PATH}/cann/set_env.sh
```

### 强制约束

- 环境变量未配置时，必须先完成配置流程，再进入测试生成环节。
- 配置流程中不允许跳过路径验证。
- 路径验证失败 3 次后中止 skill 执行。
- 用户拒绝配置时中止 skill 执行，避免后续构建失败。
- 若用户选择保存到 `.bashrc`，必须在写入前检查文件可写性。

## 执行流程（严格顺序）

1. **先验证环境变量**
   - 检查 `ASCEND_INSTALL_PATH` 是否已配置。
   - 未配置时触发交互式配置流程。
   - 用户拒绝配置时中止 skill 执行。
- 配置完成后继续后续步骤。

2. **探测改动**
   
   支持三种输入源，按优先级处理：
   
   a. **用户指定的 commit id**（最高优先级）
      - 若用户提供 commit id（如 `abc123` 或完整 SHA），执行：
        ```bash
        git show <commit_id> --stat
        ```
        验证 commit 是否存在。
      - 验证通过后，执行：
        ```bash
        git diff <commit_id>^..<commit_id> --name-only
        ```
        获取该 commit 引入的所有文件变更。
      - 若 commit 不存在，提示用户提供有效的 commit id。
   
   b. **用户指定的目标文件**（次优先级）
      - 若用户提供目标文件路径（如 `src/hixl/engine/channel.cpp`），直接以该文件为分析对象。
      - 需验证文件是否存在。
   
   c. **当前工作区改动**（默认）
      - 执行 `git diff --name-only` 获取未提交改动。
      - 若无未提交改动，执行 `git diff --cached --name-only` 获取暂存改动。
      - 若仍无改动，执行 `git diff HEAD~1 --name-only` 获取最近一次提交的改动。
   
   重点关注：`src/hixl/`、`src/llm_datadist/`、`include/hixl/`、`include/llm_datadist/`。
   
   若无任何可定位改动：明确说明"无可定位改动"，请求用户提供目标文件或 commit id。

3. **识别受影响行为**
   - 提取类/函数/状态机边界。
   - 判断输入输出、错误路径、生命周期、并发风险是否变化。

4. **按需读取文档与已有测试**
   - 接口与错误码优先：
     - `docs/cpp/HIXL接口.md`
     - `docs/cpp/HIXL_CS接口.md`
     - `docs/cpp/LLM-DataDist接口.md`
     - `docs/cpp/HIXL错误码.md`
     - `docs/cpp/LLM-DataDist错误码.md`
   - 测试模式优先：
     - `tests/cpp/llm_datadist/adxl_engine_api_unittest.cc`
     - `tests/cpp/hixl/engine/hixl_engine_unittest.cc`
     - `tests/cpp/llm_datadist/llm_datadist_v2_api_unittest.cc`
     - 同目录相邻 `_unittest.cc`/`*_test.cc`
   - 打桩（Mock/Stub）优先：
     - `tests/depends/llm_datadist/src/data_cache_engine_test_helper.h`
     - `tests/depends/ascendcl/src/ascendcl_stub.h`
     - `tests/depends/runtime/src/runtime_stub.h`
     - `tests/depends/mmpa/src/mmpa_stub.h`

5. **先给测试计划，再给代码草稿**
   - 先输出测试点、mock 策略、文件落点。
   - 再输出测试代码草稿。

6. **补充接入与验证说明**
   - 必须指出需要更新哪个 `tests/cpp/*/CMakeLists.txt`，否则新增测试不会编译执行。
   - 给出增量验证命令（使用 `--gtest_filter`）和全量验证命令。

## 验证策略

采用两阶段验证流程，优先验证新生成测试，通过后再做全量验证。

### 增量验证

使用 `--gtest_filter` 参数只运行新生成的测试套件：

```bash
# 构建测试二进制（仅构建，不运行）
bash build.sh

# 增量验证：只运行新生成的测试套件
./build_test/tests/cpp/hixl/hixl_test --gtest_filter=<新TestSuite>.*
./build_test/tests/cpp/llm_datadist/llm_datadist_test --gtest_filter=<新TestSuite>.*
```

示例：
```bash
# 假设新增测试套件为 HixlEngineChannelTest
./build_test/tests/cpp/hixl/hixl_test --gtest_filter=HixlEngineChannelTest.*
```

### 全量验证

增量验证通过后，运行仓库标准测试入口：

```bash
bash tests/run_test.sh
```

**重要**：全量测试耗时较长，必须使用后台执行模式避免 agent 等待超时：

- 使用 `run_in_background: true` 参数执行全量测试
- 使用 `timeout: 600000` 设置最大 10 分钟超时
- 测试完成后 agent 会自动收到通知

示例 agent 调用：
```json
{
  "command": "bash tests/run_test.sh",
  "description": "Run full C++ test suite in background",
  "run_in_background": true,
  "timeout": 600000
}
```

### 强制约束

- 增量验证失败时，不得执行全量验证。
- 增量验证命令必须明确指定测试套件名称（使用通配符 `TestSuite.*`）。
- 若无法确定测试套件名称，必须先阅读生成的测试代码提取 TestSuite 名称。
- **全量测试和覆盖率测试必须使用后台模式执行**，禁止同步等待超时。

### 覆盖率验证

增量覆盖率检测使用 `diff-cover` 工具，仅计算改动代码行的覆盖率。

#### 前置依赖

```bash
pip install diff-cover
```

#### 验证步骤

1. 运行覆盖率测试（耗时较长，使用后台模式）：

   ```bash
   bash tests/run_test.sh --cov -t cpp
   ```

   **agent 执行要求**：
   - 必须使用 `run_in_background: true` 执行
   - 设置 `timeout: 600000`（10 分钟上限）
   - 示例调用：
     ```json
     {
       "command": "bash tests/run_test.sh --cov -t cpp",
       "description": "Run C++ tests with coverage in background",
       "run_in_background": true,
       "timeout": 600000
     }
     ```

2. 检测增量覆盖率：
   ```bash
   # 基于当前工作区改动（对比 HEAD）
   diff-cover cov/coverage.info --compare-branch=HEAD

   # 或基于指定 commit（对比该 commit 的父提交与 HEAD）
   diff-cover cov/coverage.info --compare-branch=<commit_id>^

   # 或对比上游分支（如 origin/master）
   diff-cover cov/coverage.info --compare-branch=origin/master
   ```

3. 解析输出，确认覆盖率 >= 80%：
   - 输出示例：`Coverage: 85%`（Total 行显示）
   - 若覆盖率 < 80%，输出会列出每个文件的未覆盖行号。

#### 覆盖率不达标处理

- **阻塞流程**：覆盖率不足时终止测试生成流程。
- **输出补测建议**：明确列出未覆盖的代码行及其所属函数/分支。
- **测试补充循环**：用户补充测试后重新验证覆盖率，直至达标。

## 测试策略

- 优先级：主流程 > 关键边界 > 非法输入/失败路径 > 资源释放 > 并发行为。
- **覆盖率策略**：
  - 识别改动代码的所有分支路径（if/else、switch、异常处理）。
  - 每个分支至少有一个测试用例覆盖。
  - 重点覆盖业务逻辑变更，忽略日志、注释、格式调整。
- 优先确定性同步点，避免脆弱 sleep 断言；若必须使用 sleep，说明原因与风险。
- 能复用 `tests/depends/` 则优先复用，不新建重复基础设施。

## 输出格式（固定顺序）

### 1. 改动分析
- 改动文件、影响模块、需要补测行为。
- 区分“仓库已确认事实”与“推断”。

### 2. 测试计划
每条包含：
- 目标类/函数
- 测试意图
- 预期行为

### 3. Mock 策略
- 指定需要 mock/stub/fake 的依赖。
- 说明硬件隔离与错误路径注入方式。

### 4. 文件落点建议
- 给出建议新增文件路径（贴合 `tests/cpp/` 现有组织）。

### 5. 测试代码草稿
- 输出尽量可编译的 GoogleTest 草稿。
- 不臆造不存在接口；若细节不明确，写清假设。

### 6. 执行与验证
- 使用仓库标准入口命令。
- 列出 CMake 接入点（新增源文件列表项）。
- 给出增量验证命令（使用 `--gtest_filter`）。
- 增量验证通过后给出全量验证命令。
- 标注未验证项及其原因（环境/依赖/硬件条件）。

## 禁止事项

- 生成明显不可编译测试。
- 把推测当事实。
- 忽视 `tests/cpp/` 既有风格。
- 默认修改现有仓库文件。
- 依赖真实硬件作为前置条件。
- 覆盖率不足时声称测试完成（必须达到 80% 以上）。

## Commit ID 无效处理

当用户提供的 commit id 无效时：

1. 使用 `git show <commit_id> --stat` 验证 commit 是否存在。
2. 若 commit 不存在，提示用户检查并提供有效的 commit id。
3. 若 commit 存在但无 C++ 相关改动，明确说明并建议用户提供其他 commit 或目标文件。
4. commit id 无效时，不执行后续测试生成流程，避免产生无效测试。

## 信息不足处理

当信息不足以支撑“接近可编译”的测试代码时：

1. 明确缺失信息。
2. 明确列出假设。
3. 提供保守但可执行的测试计划。
4. 在假设范围内输出代码草稿。
5. 清晰区分“已确认”与“推断”。

