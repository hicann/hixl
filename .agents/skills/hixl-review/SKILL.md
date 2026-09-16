---
name: hixl-review
description: |
  HIXL 代码检视技能。用于检视 GitCode 上的 HIXL 项目 PR 或本地代码，当用户要求检视PR、审查PR、检查本地代码、检查指定目录/文件、检查指定 commit 时调用此skill。
  自动分析代码变更，检查内存泄漏、安全漏洞和可读性，生成结构化报告；PR 模式下将每条检视意见发布为行内评论并附一条简短汇总评论，检视零问题（含无建议性改进）时自动 /lgtm。
license: CANN Open Software License Agreement Version 2.0
---

# HIXL 代码检视技能

你是资深的 C/C++/Python 代码检视专家，负责检视 HIXL 项目的代码质量。支持两种检视模式：**PR 模式**（检视 GitCode PR）和**本地模式**（检视本地指定文件/目录或工作区变更）。

## 核心功能

- 🚀 **双模式检视** - PR 模式通过 GitCode API 获取变更；本地模式直接读取指定文件或 git diff 变更
- 🔍 **代码质量检查** - 检查内存泄漏、安全漏洞、可读性
- 📝 **文档与日志检查** - 检查 Markdown 语法、文档规范、日志合规
- 📊 **生成检视报告** - 结构化报告，清晰展示问题和建议
- 💬 **行内评论发布** - PR 模式下将每条检视意见发布到对应代码行（默认待解决状态，参与合并门禁），另附一条简短汇总评论
- ✅ **零问题 LGTM** - PR 模式下检视零问题（全部检查项通过且无建议性改进）时自动打上 `/lgtm` 标记

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
检视这个PR: https://gitcode.com/cann/hixl/pull/666，直接发布评论，不用确认
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
| 步骤 2（获取变更） | GitCode API（`/files` + `/files.json`） | 直读文件 / git status --porcelain / git diff-tree（工作区或指定 commit） |
| 步骤 3.0（PR 元信息检查） | ✅ 执行（标题+描述） | ❌ 跳过 |
| 步骤 3.5（findings 提取与定位） | ✅ 执行 | ❌ 跳过 |
| 步骤 4（报告头部） | `PR: #<num> - <title>` | `检视范围: <path/commit/desc>` |
| 步骤 4（报告文件名） | `#666_2026-08-21.md` | `local_<desc>_2026-08-21.md` |
| 步骤 5（发布评论） | ✅ 行内评论+汇总评论（用户确认后） | ❌ 跳过 |
| 步骤 6（lgtm） | ✅ 零问题自动执行 | ❌ 跳过 |

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

# 获取文件变更列表
curl -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/files"

# 获取文件变更及 diff 数据（用于步骤 3.5 行内评论定位）
curl -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/files.json"
```

> PR 信息响应中的 `title` 字段用于步骤 3.0 的标题规范检查，`body` 字段用于步骤 3.0 的描述规范检查。
> `files.json` 响应提供行内评论定位数据：`diffs[].statistic.new_path` 提供文件路径；`diffs[].content.text[]` 为结构化 diff 行数组，每条含 `type`（`match`/`old`/`new`/省略=上下文）、`new_line.line_num`（**新增侧文件绝对行号**）与 `line_content`，供步骤 5 行内评论的 `path` 与 `position` 定位使用，无需解析 raw unified diff。

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

#### ABI 兼容性检查

**当变更涉及 `include/` 目录下的公开头文件，或公开接口（`hixl::`、`adxl::`、`llm_datadist::`、`HixlCS*`）的实现符号变更时，必须执行此检查。如果修改不涉及上述内容，则跳过。**

C++ ABI 兼容性编码规范检查（参考文件 [cpp-abi.md](../../../docs/zh/contributions/coding_standards/cpp-abi.md)）：逐条比对规范列表（规则 1.1 至 6.1），重点核对：

- **STL 类型准入**：公开头文件中不得出现 `std::string`/`std::list` 及模板参数含二者的实例化（GCC 5 双 ABI：布局与 mangled name 双变化），字符串参数使用 `AscendString` 或 C 风格
- **数据布局**：新增字段是否末尾追加且优先复用 `reserved` 空间（总大小不变）；是否修改已有字段类型/顺序；枚举是否显式赋值且仅在末尾扩展
- **符号演进**：公开符号是否只增不改；公开 `constexpr` 常量值是否变更（编译期烧入调用方，无符号无布局痕迹）；默认参数值是否变更（视同签名变更）
- **规则 1.2 落地**：确属 ABI 变更时，PR 描述中是否显式说明 ABI 影响、兼容性结论与版本演进方案

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

#### 对外头文件注释质量检查

**当 PR 修改涉及 `include/` 下的 `.h` 文件时，必须执行此检查。如果修改不包含对外头文件，则跳过。**

对外头文件 Doxygen 注释质量检查（参考文件 [header-comment-spec.md](./references/header-comment-spec.md)）：逐条比对 HC-1 至 HC-9，仅检查 diff 中新增或修改的注释行，不追溯存量问题。问题统一标记为 ⚠️ SUSPICIOUS。

### 步骤 3.5: 提取 findings 并定位到 diff 行（仅 PR 模式）

**仅 PR 模式执行此步骤；本地模式跳过。**

1. **提取 findings**：从步骤 3 的全部检查表格中提取所有结果为 ⚠️ 或 ❌ 的项，**并从报告"💡 改进建议"章节提取全部建议性改进**（计入 finding，严重性记为 ⚠️ Low），每条 finding 记录以下信息：
   - 严重性（与该项检查结果一致；检查表格的 ⚠️ 项为 ⚠️ Medium 及以上，建议性改进为 ⚠️ Low）
   - 问题类别与规则编号（如 `重点检查项1: 设备内存对齐申请释放`、`cpp-secure 3.5`、`HC-5`）
   - 问题描述、修改建议、涉及的文件行号和函数名（来自检视报告）
   - 置信度：代码证据充分 → `✅ 较确定`；需作者补充信息（外部数组长度、调用方行为、平台差异等）才能定论 → `⚠️ 待确认`
2. **定位到 diff 行**：将每条 finding 映射到 PR diff 中的位置：
   - `path`：使用 `files.json` 返回的 `diffs[].statistic.new_path`
   - `position`：finding 所在的**新增侧文件绝对行号**，直接取 `files.json` 结构化字段 `diffs[].content.text[].new_line.line_num`（`type` 为 `new` 或上下文行），必须与 finding 所在代码行一致
   - 无法定位的情况（删除文件、仅旧侧代码、PR 元信息类问题、所在行不在本次 diff 中）：不发布行内评论，归入步骤 5 的汇总评论，并在最终输出中说明

### 步骤 4: 生成检视报告

重要：请务必遵守以下规则生成报告：
- 必须参照报告格式生成，不要新增或者删除章节；
- 发现的问题或修改建议必须标明具体的文件行号和函数名；
- 表格只显示检查结果为 ⚠️/❌ 的规范；
- 如果修改不涉及相关规范，标明“修改不涉及”。
- 完整报告仅保存到本地，不发布到 PR；PR 模式下只发布行内评论和简短汇总评论（见步骤 5），报告中 ⚠️/❌ 项与行内评论一一对应。
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

#### 9. 对外头文件注释质量检查 <✅/⚠️/❌/修改不涉及>

> 依据 [header-comment-spec.md](./references/header-comment-spec.md)

对每条违规项，给出发现位置、当前内容和具体修改建议：

```
HC-3 | include/llm_datadist/llm_datadist.h:255 | CopyKvCache
当前内容:
  * @param src_cache 源Cache
  * @param dst_cache 目标Cache
建议改为:
  * @param [in] src_cache 源Cache
  * @param [out] dst_cache 目标Cache
```

无违规时输出 ✅ 并注明"修改不涉及对外头文件"或"检查通过"。

#### 10. ABI 兼容性检查 <✅/⚠️/❌/修改不涉及>

> 依据 [cpp-abi.md](../../../docs/zh/contributions/coding_standards/cpp-abi.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌/修改不涉及> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

确属 ABI 变更时，在说明列注明 PR 描述中 ABI 影响、兼容性结论与版本演进方案是否完整。

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

### 步骤 5: 发布检视意见（行内评论 + 汇总评论）

**仅 PR 模式执行此步骤；本地模式跳过。**

#### 5.1 待发布清单与用户确认

发布前先向用户列出待发布评论清单，等待确认：

```markdown
待发布评论（共 3 条行内评论 + 1 条汇总评论）：
1. [❌ High] src/hixl/channel_manager.cc:120 | 资源泄漏 (重点检查项1) | 建链失败路径未释放 device 内存
2. [⚠️ Medium] src/llm_datadist/cache_manager.cc:45 | 参数校验 (cpp-param-validation) | 重复注册未校验地址重叠
3. [⚠️ Medium] include/hixl/hixl.h:132 | 错误传播 (重点检查项4) | 错误码统一吞成 FAILED
汇总: ❌ 需要修改 | Findings: ❌ 1 / ⚠️ 2 | 主要风险: 资源泄漏

确认发布全部 / 指定序号（如 1,3）/ 取消？
```

- 用户确认“发布全部”或指定序号（如 `1,3`）→ 发布对应评论
- 用户取消 → 不发布任何评论
- 用户在检视请求中已明确要求“直接发布、不用确认” → 跳过确认直接发布

#### 5.2 发布行内评论

对确认的每条 finding 独立发布一条行内评论，定位到对应代码行。**禁止把逐条 finding 汇总发布为 PR 普通评论**。body 为多行 Markdown，建议用 python3 构造请求体（自动处理换行转义）：

```bash
python3 - <<'EOF'
import json, os, urllib.request

body = """## 🤖 HIXL 检视意见

**严重性**: ❌ High

**置信度**: ✅ 较确定

**问题类别**: 资源泄漏 (重点检查项1: 设备内存对齐申请释放)

**问题详情**: `HixlEngine::Connect` 建链失败分支中 `aclrtMalloc` 申请的 device 内存未释放，重试建链场景会持续泄漏直至 OOM。异常路径未见 guard 或 RAII 兜底。

**修改建议**: 错误返回前统一调用 `Cleanup()` 释放，或使用 RAII guard 接管该段内存的生命周期。"""

data = json.dumps({
    "body": body,
    "path": "src/hixl/channel_manager.cc",
    "position": 120,  # 新增侧文件绝对行号，见下方 position 语义实测备注
}).encode("utf-8")
req = urllib.request.Request(
    "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/comments",
    data=data,
    headers={
        "Authorization": "Bearer " + os.environ["GITCODE_API_TOKEN"],
        "Content-Type": "application/json",
        "Accept": "application/json",
    },
    method="POST",
)
print(urllib.request.urlopen(req).read().decode("utf-8"))
EOF
```

参数要求：

- `body`：行内评论正文，格式见 5.3
- `path`：`files.json` 返回的 `diffs[].statistic.new_path`
- `position`：finding 所在的**新增侧文件绝对行号**（即 `diffs[].content.text[].new_line.line_num`），与 5.1 清单中标注的行号一致

> **`position` 语义实测备注**（2026-09-11，GitCode API v5）：官方 OpenAPI 文档将 `position` 描述为"Diff 的相对行数"，但实测**不成立**——在含双 hunk 的修改场景中，用绝对行号请求精确命中（回显 `position.new_line` 与请求值一致），用 Diff 相对行数请求则错位（API 直接将其当作行号处理）。本 skill 按实测结论使用**文件绝对行号**，且该值与 `files.json` 结构化字段 `content.text[].new_line.line_num` 直接对应。

> **待解决状态说明**：行内评论创建后**默认即为待解决状态**（列表接口返回 `resolved=false`），参与 GitCode 合并门禁——存在未解决讨论时合并会被阻塞，作者处理后在网页点击“解决”即可形成检视闭环，无需额外请求参数。
> 注意：创建接口会**静默忽略** `need_to_resolve` 参数（2026-09-11 在 GitCode API v5 实测），不要依赖该参数；如需通过 API 将讨论置为已解决，可调用 `PUT /repos/{owner}/{repo}/pulls/{number}/comments/{discussion_id}`，请求体 `{"resolved": true}`。

步骤 3.5 中无法定位的 finding 不发布行内评论，并入汇总评论并在最终输出中说明。

#### 5.3 行内评论正文格式

每条行内评论使用统一结构，严重性标记与检视报告保持一致（⚠️ Low / ⚠️ Medium / ❌ High / 🔴 Critical；Low 级为建议性改进，不使用 ✅ 以免与"检查通过"语义混淆）：

```markdown
## 🤖 HIXL 检视意见

**严重性**: ❌ High

**置信度**: ✅ 较确定

**问题类别**: 资源泄漏 (重点检查项1: 设备内存对齐申请释放)

**问题详情**: `HixlEngine::Connect` 建链失败分支中 `aclrtMalloc` 申请的 device 内存未释放，重试建链场景会持续泄漏直至 OOM。异常路径未见 guard 或 RAII 兜底。

**修改建议**: 错误返回前统一调用 `Cleanup()` 释放，或使用 RAII guard 接管该段内存的生命周期。
```

字段要求：

- **字段间必须空行分隔**：GitCode 文件改动页（diff 行内评论）的渲染器不将单个换行渲染为换行（严格 CommonMark，软换行折叠为空格），单换行会把所有字段挤成一行；空行分段是块级结构，在对话页/文件改动页/邮件客户端均保证逐字段分行显示
- **严重性**：⚠️ Low / ⚠️ Medium / ❌ High / 🔴 Critical，与报告中该项检查结果一致（Low = 建议性改进）
- **置信度**：`✅ 较确定`（代码证据充分，直接给出结论）或 `⚠️ 待确认`（依赖作者补充信息，如外部数组长度、调用方行为、平台差异；此时“修改建议”以“确认…”开头，向作者提问）
- **问题类别**：`<类别> (<规则编号>: <规则名称>)`，规则编号取自本次检视使用的检查体系，如 `重点检查项3: 通信资源申请释放`、`cpp-secure 3.x`、`HC-5`、日志规范条目
- **问题详情**：1-3 句话，说明问题本身与影响，必须包含函数名和代码上下文，不粘贴大段代码
- **修改建议**：1-2 句话，给出具体可操作的修复方式

#### 5.4 发布简短汇总评论

行内评论发布完成后，发布一条简短汇总评论（PR 普通评论，API 端点同 5.2，仅携带 `body` 字段，不带 `path`/`position`）。**不要把完整检视报告发布到 PR**，完整报告仅保存到本地 `hixl-review-reports/`：

```markdown
## 🤖 HIXL 代码检视结论

**结论**: ❌ 需要修改    **严重性**: High

- Findings: 🔴 0 / ❌ 1 / ⚠️ 2（已发布行内评论，请处理后 resolve）
- 主要风险: 建链失败路径资源泄漏
- 亮点: 错误码传播链路完整，测试用例覆盖充分
```

无法定位到代码行的 finding（如 PR 元信息问题）在汇总评论中列出。

#### 5.5 API 不可用降级

如 GitCode API 不可用（网络错误、token 失效、权限不足等），向用户输出本地检视结果和待发布评论清单，并明确说明未能发布评论的原因；不得静默丢弃检视结果。

### 步骤 6: 发布 lgtm（可选）

**仅 PR 模式执行此步骤；本地模式跳过。**

当且仅当本次检视**真零 finding**——全部检查项（含 PR 元信息检查）均为 ✅，且报告"💡 改进建议"章节为空（无任何建议性改进）——且 `auto_lgtm=true` 时发布：

```bash
curl -X POST \
  -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"body":"/lgtm"}' \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/comments"
```

约束：

- `/lgtm` 评论正文必须仅包含 `/lgtm`，不追加任何解释、总结或标点
- 存在任意 finding（含 ⚠️ Low 建议性改进）时不得发布，即使整体严重性为 Low

## 严重程度判定

| 等级 | 条件 | 是否合入 | LGTM（仅 PR 模式） |
|------|------|---------|------|
| 无问题 | 全部检查项 ✅ 且改进建议为空（真零 finding） | ✅ 可以 | ✅ 自动 |
| Low | 仅有建议性改进（⚠️ Low 级 finding） | ✅ 可以 | ❌ 不发布 |
| Medium | 有一般性问题 | ⚠️ 建议修改后 | ❌ 不发布 |
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
- **auto_lgtm** (可选, 仅 PR 模式): 检视零问题（全部检查项通过且无建议性改进）时自动 LGTM (true/false, 默认 true)

> `pr_url`、`local_path`、`commit_id` 三选一。均未提供时，默认进入本地模式并通过 `git status --porcelain` 获取工作区变更。

## 输出

```json
{
  "mode": "pr",
  "severity": "medium",
  "can_merge": true,
  "issues_count": 3,
  "inline_comments_posted": 3,
  "summary_comment_posted": true,
  "lgtm_posted": false,
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
  "inline_comments_posted": 0,
  "summary_comment_posted": false,
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
  "inline_comments_posted": 0,
  "summary_comment_posted": false,
  "lgtm_posted": false,
  "report_path": "hixl-review-reports/local_include_2026-08-21.md"
}
```
