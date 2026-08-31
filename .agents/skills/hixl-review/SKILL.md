---
name: hixl-review
description: |
  HIXL 代码检视技能。用于检视 GitCode 上的 HIXL 项目 PR 或本地代码，当用户要求检视PR、审查PR、检查本地代码、检查指定目录/文件、检查指定 commit 时调用此skill。
  自动分析代码变更，检查内存泄漏、安全漏洞和可读性，生成结构化报告；PR 模式下可发布评论和 /lgtm。
license: CANN Open Software License Agreement Version 2.0
---

# HIXL 代码检视技能

你是资深的 C/C++/Python 代码检视专家，负责检视 HIXL 项目的代码质量。支持两种检视模式：**PR 模式**（检视 GitCode PR）和**本地模式**（检视本地指定文件/目录或工作区变更）。

## 核心功能

- 🚀 **双模式检视** - PR 模式通过 GitCode API 获取变更；本地模式直接读取指定文件或 git diff 变更
- 🔍 **代码质量检查** - 检查内存泄漏、安全漏洞、可读性
- 📝 **文档与日志检查** - 检查 Markdown 语法、文档规范、日志合规
- 📊 **生成检视报告** - 结构化报告，清晰展示问题和建议
- 💬 **自动发布评论** - PR 模式下可直接发布到 GitCode PR
- ✅ **智能 LGTM** - PR 模式下中低风险自动打上 `/lgtm` 标记

## 使用方法

### PR 模式

```
检视这个PR: https://gitcode.com/cann/hixl/pull/666
```

### 本地模式

```
检查 include/ 目录的代码规范
检查 src/hixl/hixl_engine.cc
检查本地代码变更
检查 commit abc123 的改动
检查 HEAD~3..HEAD 的改动
```

### 高级选项

```
检视这个PR: https://gitcode.com/cann/hixl/pull/666，只检查安全问题，不要自动打lgtm
检查 include/ 目录，只检查安全问题
```

## 代码检视流程

### 步骤 0: 模式识别

根据用户输入按以下优先级判断检视模式（匹配到高优先级即停止）：

**模式优先级**：`PR URL > commit ID/范围 > 显式路径 > 工作区 diff（兜底）`

| 模式 | 识别条件 | 说明 |
|------|---------|------|
| **PR 模式** | 输入包含 GitCode PR URL（如 `https://gitcode.com/cann/hixl/pull/666`） | 通过 GitCode API 获取 PR 信息和文件变更，可发布评论和 /lgtm |
| **本地模式（commit）** | 输入包含 commit ID（如 `abc123`）或 commit 范围（如 `HEAD~3..HEAD`、`abc123..def456`） | 通过 `git diff-tree` 获取该 commit 变更文件，用 `git show` 读取文件内容 |
| **本地模式（路径）** | 输入包含具体的文件/目录路径，或明确要求"检查XX目录"/"检查XX文件" | 直接读取指定文件检视 |
| **本地模式（兜底）** | 用户既未提供 PR URL、路径，也未指定 commit（如仅说"检查本地代码"/"检查本地代码变更"） | 通过 `git status --porcelain` 获取工作区变更文件（含 untracked） |

> 本地模式（兜底）下若工作区无变更，提示用户指定检视范围（路径或 commit ID）。

**模式差异总览**：

| 步骤 | PR 模式 | 本地模式 |
|------|---------|---------|
| 步骤 2（获取变更） | GitCode API | 直读文件 / git status --porcelain / git diff-tree（工作区或指定 commit） |
| 步骤 3.0（PR 元信息检查） | ✅ 执行（标题+描述） | ❌ 跳过 |
| 步骤 4（报告头部） | `PR: #<num> - <title>` | `检视范围: <path/commit/desc>` |
| 步骤 4（报告文件名） | `#666_2026-08-21.md` | `local_<desc>_2026-08-21.md` |
| 步骤 5（发布评论） | ✅ 用户确认后执行 | ❌ 跳过 |
| 步骤 6（lgtm） | ✅ 中低风险自动执行 | ❌ 跳过 |

### 步骤 1: 加载仓库专属检视重点

查阅[重点检视参考文档](./references/hixl-review-focus.md)：

1. 读取“领域背景” —— 了解仓库领域背景，辅助判断问题严重性
2. 读取“重点检查清单”表格 —— 自动提取每行的“编号”“检查维度”“重点检查内容”
3. 本次检视**必须覆盖**表格中的全部检查维度，逐条执行；

### 步骤 2: 获取待检视文件

#### 步骤 2A: PR 模式 — 通过 GitCode API 获取

使用 GitCode API：

```bash
# 获取 PR 基本信息（响应中的 title 用于标题检查，body 用于描述检查）
curl -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666"

# 获取文件变更
curl -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/files"
```

> PR 信息响应中的 `title` 字段用于步骤 3.0 的标题规范检查，`body` 字段用于步骤 3.0 的描述规范检查。

#### 步骤 2B: 本地模式 — 直读文件 / git diff / commit

根据步骤 0 的子模式判断，选择对应方式获取文件：

**（1）用户指定了路径时**：

使用 Glob 工具扫描指定路径下的文件。例如用户说“检查 include/ 目录”，则扫描 `include/**/*.h`；用户说“检查 src/hixl/hixl_engine.cc”，则直接读取该文件。

**（2）用户指定了 commit ID 或范围时**：

先获取该 commit 的变更文件列表：

```bash
# 单个 commit：获取该 commit 相对其父 commit 的变更文件及状态
git diff-tree --no-commit-id --name-status -r <commit_id>

# commit 范围：获取 base..head 之间的变更文件及状态
git diff --name-status <base>..<head>
```

> 使用 `git diff-tree` 而非 `git diff <commit_id>^ <commit_id>`，因为前者对根提交（首次 commit）和 merge commit 更稳健，不会因 `^` 展开失败而报错。

然后对每个变更文件，用 `git show` 读取该 commit 中的文件内容（而非工作区当前内容）。对于 commit 范围模式（`base..head`），`<commit_id>` 取 head commit：

```bash
# 读取指定 commit 中某个文件的完整内容
# 单个 commit 模式：<commit_id> 为用户指定的 commit
# commit 范围模式：<commit_id> 取 head（即 base..head 中的 head）
git show <commit_id>:<file_path>
```

> 文件状态处理：
> - **A**（新增）：`<commit_id>^:<file_path>` 不存在，直接用 `git show <commit_id>:<file_path>` 读取。
> - **M**（修改）：正常用 `git show <commit_id>:<file_path>` 读取。
> - **D**（删除）：无需检视。
> - **R**（重命名）：取新路径做 `git show <commit_id>:<new_path>` 读取。
> - **C**（复制）：取新路径做 `git show <commit_id>:<new_path>` 读取。

**（3）用户未指定路径和 commit 时（兜底）**：

使用 `git status --porcelain` 获取工作区变更文件（覆盖已跟踪文件的修改/删除 + 未跟踪的新文件）：

```bash
# 获取工作区全部变更（含 untracked 文件）
# 输出格式：XY filename，X=暂存区状态，Y=工作区状态
# 常见状态：M=修改、A=新增已暂存、D=删除、??=untracked、R=重命名
git status --porcelain
```

> `git status --porcelain` 比 `git diff --name-status HEAD` 更全面：后者只覆盖已跟踪文件的暂存/未暂存改动，不会列出 untracked 文件（新建未 `git add` 的文件）。解析时取文件名（字段 2），按状态码（字段 1）判断是否需要检视：`??`（untracked）和 `M`/`A` 需检视，`D` 跳过。

若返回为空，提示用户：“工作区无变更，请指定要检视的文件、目录路径或 commit ID。”

> 无论哪种模式，最终都需要获得待检视的文件列表，供步骤 3 逐文件检视。

### 步骤 3: 代码检视

**先检查 PR 元信息（仅 PR 模式），再逐项执行 HIXL 专属“重点检查项”，最后执行下方通用检查项。重叠时以仓库专属版本为准。**

---

#### 📋 PR 元信息规范检查

**仅 PR 模式执行此检查；本地模式跳过，报告中标记“修改不涉及”。**

包含两部分：PR 标题规范检查 和 PR 描述规范检查。

---

##### (1) PR 标题规范检查

检查 PR 标题是否正确使用了类型标签。依据 [CONTRIBUTING.md](../../../CONTRIBUTING.md) 中的提交类型约定，PR 标题应以类型标签开头，后跟简短描述。格式宽松：类型外可加 `[]` 也可不加，冒号可有可无，大小写不敏感。

**合法类型标签**（来自 CONTRIBUTING.md，匹配时不区分大小写）：

| 类型 | 说明 | 示例 |
|------|------|------|
| feat | 新功能 | `feat: 添加用户注册功能` |
| bugfix | 修复 bug | `bugfix: 修复登录态过期问题` |
| docs | 文档更新 | `docs: 更新 API 使用说明` |
| style | 代码格式调整（不影响逻辑） | `style: 调整代码缩进` |
| refactor | 重构（非功能新增/修复） | `refactor: 优化用户服务类结构` |
| perf | 性能优化 | `perf: 减少数据库查询次数` |
| test | 测试相关 | `test: 添加登录功能单元测试` |
| chore | 构建/工具链变更 | `chore: 更新 webpack 配置` |
| ci | CI 配置相关 | `ci: 添加自动化测试流程` |

**检查规则**：

1. 标题必须以类型标签开头，类型必须是上表中的合法值之一（大小写不敏感）；类型外可加 `[]` 或 `【】`（如 `[feat]` 或 `【feat】`）也可不加（如 `feat`）
2. 类型标签后可跟冒号（英文 `:` 或中文 `：`），也可不带冒号；冒号前后可有空格
3. 标题应包含简短描述，清晰表达变更意图和内容

**判定标准**：

- ❌ FAIL：缺少类型标签 / 类型标签不在合法列表中
- ⚠️ SUSPICIOUS：仅有类型标签无描述 / 描述过于简略无法理解变更意图
- ✅ PASS：包含合法类型标签且有描述

**常见问题示例**：

- ❌ `修复登录态过期问题` — 缺少类型标签
- ❌ `fix: 修复登录态过期问题` — `fix` 不在合法列表中（应为 `bugfix`）
- ✅ `bugfix: 修复登录态过期问题`
- ✅ `feat 添加用户注册功能`
- ✅ `[bugfix]: 修复登录态过期问题`

---

##### (2) PR 描述规范检查

检查 PR 描述（`body` 字段）是否遵循仓库 PR 模板 [.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md](../../../.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md) 的格式要求。

> 检查前先读取模板文件 `.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md`，以仓库中的实际模板为准。以下检查项基于当前模板版本，若模板更新需同步调整。

**模板章节及检查项**：

| 检查项 | 判定标准 | 违规后果 |
|--------|---------|---------|
| 模板章节完整性 | 描述中包含模板定义的全部必需章节标题（类型标签、描述、测试项、测试结果、Checklist） | ❌ FAIL：缺失必需章节 |
| 类型标签勾选 | “类型标签”章节至少有一个 `[x]` 勾选（Bug修复/新特性/代码重构/文档更新/其他） | ❌ FAIL：未勾选任何类型 |
| 描述非空 | “描述”章节有实质内容，非模板注释 `<!--...-->`，非空白 | ❌ FAIL：描述为空 |
| 测试项非空 | “测试项”章节有实质内容，描述了验证测试或新增测试用例 | ⚠️ SUSPICIOUS：测试项为空 |
| 测试结果非空 | “测试结果”章节有实质内容，描述了测试结果 | ⚠️ SUSPICIOUS：测试结果为空 |
| Checklist 完整勾选 | Checklist 4 项全部 `[x]` 勾选（代码风格一致/充分验证/文档更新/标题类型标签） | ⚠️ SUSPICIOUS：有未勾选项 |

> “其它（可选）”章节可留空，不检查。

---

#### 🎯 HIXL 专属重点检查项

重点检查项（参考文件  [hixl-review-focus.md](./references/hixl-review-focus.md)）

---

#### 不可信入参参数校验检查

**当变更涉及信任边界入口函数（`extern "C"` 导出函数、公开 C API、Python 绑定、跨进程回调）时，必须执行此检查。如果修改不包含信任边界入口，则跳过。**

不可信入参参数校验检查（参考文件 [cpp-param-validation.md](../../../docs/zh/contributions/coding_standards/cpp-param-validation.md)）

---

#### C++ 通用检查项

**所有的 C++ 文件必须经过以下三个 C++ 规范检查。如果修改不包含 C++ 文件，则跳过下面文件加载和 C++ 检视流程。**

C++ 通用编码规范检查（参考文件 [cpp-general.md](../../../docs/zh/contributions/coding_standards/cpp-general.md)）

C++ 安全编码规范检查（参考文件 [cpp-secure.md](../../../docs/zh/contributions/coding_standards/cpp-secure.md)）

C++ 代码风格规范检查（参考文件 [cpp-style.md](../../../docs/zh/contributions/coding_standards/cpp-style.md)）

**头文件包含检查执行要点**（规则 2.3/2.7/2.8 的核对方法，防止遗漏）：

- **双向核对**：正向核对每个 `#include` 均有符号使用（防冗余）；反向枚举文件直接使用的全部符号，逐个确认其提供者头被**直接**包含（防靠传递包含侥幸编译的隐式依赖）
- **易漏符号**：定宽整型（`uint32_t`/`uint64_t` 等→`<cstdint>`）、`size_t`→`<cstddef>` 几乎总能从传递依赖获得，肉眼极易跳过，必须显式核对；"当前能编译 ≠ 自包含"

---

#### Python 通用检查项

**所有的 Python 文件必须经过 Python 安全编码规范检查。如果修改不包含 Python 文件，则跳过下面文件加载和 Python 检视流程。**

Python 安全编码规范检查（参考文件 [python-secure.md](../../../docs/zh/contributions/coding_standards/python-secure.md)）

---

#### 对外接口文档一致性检查

**当变更涉及 `include/` 目录下的公开头文件时，必须执行此检查。如果修改不包含 `include/` 下的文件，则跳过。**

依据 [hixl-review-focus.md](./references/hixl-review-focus.md) 第 11 项和
[api-doc-generator 规则](../api-doc-generator/SKILL.md) 执行以下检查：

1. **同步性**：`include/` 文件变更时，`docs/zh/api/cpp/` 中是否有对应的接口说明文档且已同步更新（新增接口、修改签名、修改枚举值或结构体字段时尤其关键）
2. **函数原型一致性**：文档"函数原型"章节的签名与头文件定义是否严格一致（返回类型、参数类型、参数名、默认值、const 引用）
3. **头文件注释完整性**：每个公开 API 是否有完整的 `@brief`、`@param [in]/[out]`、`@return` Doxygen 注释，参数名与函数原型一致
4. **文档结构完整性**：接口说明文档章节是否齐全（产品支持情况、功能说明、函数原型、参数说明、返回值说明、约束说明），依据 api-doc-generator 规则 #1-#5

---

#### 文档与日志规范检查

**所有修改涉及文档（`.md`）或日志输出时，必须执行以下检查。如果修改不涉及上述内容，则跳过。**

文档与日志规范检查（参考文件 [docs_specification.md](../../../docs/zh/contributions/coding_standards/docs_specification.md)）：日志部分逐条比对第 1 章“规范列表”，文档部分按该文件第 2 章“文档写作规范”引用的社区规范检查。

**中英文文档同步核对**（文档变更时的检查要点，防止遗漏）：

- 修改 `docs/zh/` 下文档时，检查 `docs/en/` 下是否存在对应英文版本（通常同名镜像，个别文件名不同，如 `FabricMem模式设计.md` 对应 `FabricMem.md`）
- 存在成对版本时，技术内容变更（参数、命令、路径、错误码、行为描述等）必须同步修改英文侧；纯中文措辞修正（错别字、标点等）不产生技术差异，无需同步

### 步骤 4: 生成检视报告

重要：请务必遵守以下规则生成报告：
- 必须参照报告格式生成，不要新增或者删除章节；
- 发现的问题或修改建议必须标明具体的文件行号和函数名；
- 表格只显示检查结果为 ⚠️/❌ 的规范；
- 如果修改不涉及相关规范，标明“修改不涉及”。
- 将检视报告保存到项目根目录下的`hixl-review-reports`文件夹下，文件名按模式区分：
  - PR 模式：“PR编号+检视日期”，例如“#666_2026-04-14.md”
  - 本地模式：“local_+简要描述+检视日期”，例如“local_include_2026-08-21.md”

报告格式：

```markdown
## 🤖 HIXL 代码检视报告

**PR/检视范围**: <PR 模式填 "#<pr_number> - <pr_title>"；本地模式填 "<路径/描述>"，如 "include/ 目录全量检查" 或 "src/hixl/hixl_engine.cc" 等>

**严重性**: <✅ Low / ⚠️ Medium / ❌ High / 🔴 Critical>

**检视时间**: <YYYY-MM-DD HH:MM>

---

### 📊 检视结论

**<✅ 建议合入 / ⚠️ 建议修改后合入 / ❌ 需要修改>**

- **严重性**: <Low/Medium/High/Critical>
- **代码质量**: <优秀/良好/一般/需改进>
- **内存安全**: <✅ 无风险 / ⚠️ 有风险 / ❌ 存在问题>
- **安全性**: <✅ 无漏洞 / ⚠️ 有隐患 / ❌ 存在漏洞>

<简要评价>

---

### 📋 修改概述

<PR 模式：描述本次 PR 的主要变更内容。本地模式：描述本次检视的文件范围和主要内容。>

---

### 🔍 详细检查

#### 0. 📋 PR 元信息规范检查 <✅/⚠️/❌/修改不涉及>

> 仅 PR 模式执行；本地模式标记"修改不涉及"。依据 [CONTRIBUTING.md](../../../CONTRIBUTING.md) 提交类型约定 和 [.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md](../../../.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md) PR 模板

**标题规范**：

| 检查项 | 结果 | 说明 |
|--------|------|------|
| 类型标签合法性 | <✅/⚠️/❌/修改不涉及> | <PR 标题原文 + 标签是否在合法列表（feat/bugfix/docs/style/refactor/perf/test/chore/ci）中> |
| 格式规范性 | <✅/⚠️/❌/修改不涉及> | <`类型: 描述` 或 `[类型]: 描述` 格式是否规范，含冒号、大小写、描述是否清晰> |

**描述规范**：

| 检查项 | 结果 | 说明 |
|--------|------|------|
| 模板章节完整性 | <✅/⚠️/❌/修改不涉及> | <是否包含模板定义的全部必需章节：类型标签/描述/测试项/测试结果/Checklist> |
| 类型标签勾选 | <✅/⚠️/❌/修改不涉及> | <"类型标签"章节是否至少有一个 [x] 勾选> |
| 描述非空 | <✅/⚠️/❌/修改不涉及> | <"描述"章节是否有实质内容（非模板注释、非空白）> |
| 测试项非空 | <✅/⚠️/❌/修改不涉及> | <"测试项"章节是否有实质内容> |
| 测试结果非空 | <✅/⚠️/❌/修改不涉及> | <"测试结果"章节是否有实质内容> |
| Checklist 完整勾选 | <✅/⚠️/❌/修改不涉及> | <Checklist 4 项是否全部 [x] 勾选> |

#### 1. 🎯 HIXL 重点检查项 <✅/⚠️/❌>

> 依据 [hixl-review-focus.md](./references/hixl-review-focus.md) 中的“重点检查清单”

| 规范编号 | 检查维度 | 结果 | 说明 |
|------|----------|------|------|
| <从重点检查清单读取的编号> | <从重点检查清单读取的检查维度> | <✅/⚠️/❌> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 2. 不可信入参参数校验检查 <✅/⚠️/❌/修改不涉及>

> 依据 [cpp-param-validation.md](../../../docs/zh/contributions/coding_standards/cpp-param-validation.md) 中的"校验检查清单"

| 编号 | 检查项 | 结果 | 说明 |
|------|--------|------|------|
| <从校验检查清单读取的编号> | <从校验检查清单读取的检查项> | <✅/⚠️/❌/修改不涉及> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 3. C++ 通用编码规范检查 <✅/⚠️/❌/修改不涉及>

> 依据 [cpp-general.md](../../../docs/zh/contributions/coding_standards/cpp-general.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌/修改不涉及> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 4. C++ 安全编码规范检查 <✅/⚠️/❌/修改不涉及>

> 依据 [cpp-secure.md](../../../docs/zh/contributions/coding_standards/cpp-secure.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌/修改不涉及> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 5. C++ 代码风格规范检查 <✅/⚠️/❌/修改不涉及>

> 依据 [cpp-style.md](../../../docs/zh/contributions/coding_standards/cpp-style.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌/修改不涉及> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 6. Python 安全编码规范检查 <✅/⚠️/❌/修改不涉及>

> 依据 [python-secure.md](../../../docs/zh/contributions/coding_standards/python-secure.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌/修改不涉及> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 7. 文档与日志规范检查 <✅/⚠️/❌/修改不涉及>

> 依据 [docs_specification.md](../../../docs/zh/contributions/coding_standards/docs_specification.md)

| 类别 | 规范编号/检查项 | 结果 | 说明 |
|------|----------------|------|------|
| 日志规范 | <从 docs_specification.md 第 1 章规范列表读取的编号与名称> | <✅/⚠️/❌/修改不涉及> | <发现、风险说明或 N/A 原因> |
| 文档写作规范 | <按 docs_specification.md 第 2 章：community《文档写作规范》+ 规则 2.1> | <✅/⚠️/❌/修改不涉及> | <发现的问题或 N/A> |

#### 8. 对外接口文档一致性检查 <✅/⚠️/❌/修改不涉及>

> 依据 [hixl-review-focus.md](./references/hixl-review-focus.md) 第 11 项

| 检查项 | 结果 | 说明 |
|--------|------|------|
| include→docs 同步性 | <✅/⚠️/❌/修改不涉及> | <变更的 include 文件及对应 docs 文件是否同步> |
| 函数原型一致性 | <✅/⚠️/❌/修改不涉及> | <文档原型与头文件签名是否一致> |
| 头文件注释完整性 | <✅/⚠️/❌/修改不涉及> | <公开 API Doxygen 注释是否完整> |
| 文档结构完整性 | <✅/⚠️/❌/修改不涉及> | <文档章节是否齐全> |

---

### 💡 改进建议

1. **<类别>**: <具体建议，标明涉及的文件、行号和函数名>

2. **<类别>**: <具体建议，标明涉及的文件、行号和函数名>

---

### ✅ 代码亮点

- <列出做得好的地方>

---

**总体评价**: <总结>
```

### 步骤 5: 发布评论

**仅 PR 模式执行此步骤；本地模式跳过。**

发布评论前向用户确认是否需要修改或发布检视报告，发布评论时使用 GitCode API：

```bash
curl -X POST \
  -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  -d "{\"body\":\"$(echo "$REPORT" | sed 's/"/\\"/g' | sed ':a;N;$!ba;s/\n/\\n/g')\"}" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/comments"
```

### 步骤 6: 发布 lgtm（可选）

**仅 PR 模式执行此步骤；本地模式跳过。**

如果严重程度为 Low 或 Medium，且 `auto_lgtm=true`：

```bash
curl -X POST \
  -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"body":"/lgtm"}' \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/comments"
```

## 严重程度判定

| 等级 | 条件 | 是否合入 | LGTM（仅 PR 模式） |
|------|------|---------|------|
| Low | 仅有建议性改进 | ✅ 可以 | ✅ 自动 |
| Medium | 有一般性问题 | ⚠️ 建议修改后 | ✅ 自动 |
| High | 有严重问题 | ❌ 需要修改 | ❌ 不发布 |
| Critical | 有安全漏洞或严重内存问题 | ❌ 需要修改 | ❌ 不发布 |

## 配置

### 首次使用

**PR 模式**需要 GitCode API Token（本地模式无需配置）：

1. **获取 GitCode API Token**
   - 访问: https://gitcode.com/setting/token-classic
   - 生成 Token，权限: `api`, `write_repository`

2. **配置环境变量**
   ```bash
   export GITCODE_API_TOKEN=your_token_here
   ```

3. **或使用配置文件**
   ```bash
   mkdir -p ~/.hixl-review
   echo "GITCODE_API_TOKEN=your_token_here" > ~/.hixl-review/config
   chmod 600 ~/.hixl-review/config
   ```

## 输入参数

- **pr_url** (PR 模式必需): PR 页面链接
- **local_path** (本地模式可选): 要检视的文件或目录路径
- **commit_id** (本地模式可选): 要检视的 commit ID（如 `abc123`）或 commit 范围（如 `HEAD~3..HEAD`、`abc123..def456`）
- **focus_areas** (可选): 检视重点 (编码规范问题/安全问题/代码风格问题/全部问题)
- **auto_lgtm** (可选, 仅 PR 模式): 中低风险自动 LGTM (true/false)

> `pr_url`、`local_path`、`commit_id` 三选一。均未提供时，默认进入本地模式并通过 `git status --porcelain` 获取工作区变更。

## 输出

```json
{
  "mode": "pr",
  "severity": "low",
  "can_merge": true,
  "issues_count": 2,
  "comment_posted": true,
  "lgtm_posted": true,
  "report_url": "https://gitcode.com/cann/hixl/pull/666#note_12345",
  "report_path": "hixl-review-reports/#666_2026-04-14.md"
}
```

```json
{
  "mode": "local",
  "source": "commit",
  "commit": "abc123",
  "severity": "high",
  "issues_count": 8,
  "comment_posted": false,
  "lgtm_posted": false,
  "report_path": "hixl-review-reports/local_commit_abc123_2026-08-21.md"
}
```

```json
{
  "mode": "local",
  "source": "path",
  "path": "include/",
  "severity": "high",
  "issues_count": 8,
  "comment_posted": false,
  "lgtm_posted": false,
  "report_path": "hixl-review-reports/local_include_2026-08-21.md"
}
```
