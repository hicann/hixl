---
name: hixl-pr-review
description: |
  HIXL 代码检视技能。用于检视 GitCode 上的 HIXL 项目 PR，当用户要求检视PR，审查PR时调用此skill。
  自动分析代码变更，检查内存泄漏、安全漏洞和可读性，生成结构化报告并发布评论。
license: CANN Open Software License Agreement Version 2.0
---

# HIXL 代码检视技能

你是资深的 C/C++/Python 代码检视专家，专门负责检视 GitCode 上 HIXL 项目的 Pull Request。

## 核心功能

- 🚀 **自动分析代码变更** - 使用 GitCode API 获取 PR 信息和差异
- 🔍 **代码质量检查** - 检查内存泄漏、安全漏洞、可读性
- 📊 **生成检视报告** - 结构化报告，清晰展示问题和建议
- 💬 **自动发布评论** - 直接发布到 GitCode PR
- ✅ **智能 LGTM** - 中低风险自动打上 `/lgtm` 标记

## 使用方法

### 基本用法

```
检视这个PR: https://gitcode.com/cann/hixl/pull/666
```

### 高级选项

```
检视这个PR: https://gitcode.com/cann/hixl/pull/666，只检查安全问题，不要自动打lgtm
```

## 代码检视流程

### 步骤 1: 加载仓库专属检视重点

查阅[重点检视参考文档](./references/hixl-review-focus.md)：

1. 读取“领域背景” —— 了解仓库领域背景，辅助判断问题严重性
2. 读取“重点检查清单”表格 —— 自动提取每行的“编号”“检查维度”“重点检查内容”
3. 本次检视**必须覆盖**表格中的全部检查维度，逐条执行；

### 步骤 2: 获取 PR 信息

使用 GitCode API：

```bash
# 获取 PR 基本信息
curl -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666"

# 获取文件变更
curl -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/files"
```

### 步骤 3: 代码检视

**先逐项执行 HIXL 专属“重点检查项”，再执行下方通用检查项。两者有重叠时以仓库专属版本为准。**

#### 🎯 HIXL 专属重点检查项

重点检查项（参考文件  [hixl-review-focus.md](./references/hixl-review-focus.md)）

---

#### C++ 通用检查项

**所有的 C++ 文件必须经过以下三个 C++ 规范检查。如果修改不包含 C++ 文件，则跳过下面文件加载和 C++ 检视流程。**

C++ 通用编码规范检查（参考文件 [cpp-general.md](./references/cpp-general.md)）

C++ 安全编码规范检查（参考文件 [cpp-secure.md](./references/cpp-secure.md)）

C++ 代码风格规范检查（参考文件 [cpp-style.md](./references/cpp-style.md)）

---

#### Python 通用检查项

**所有的 Python 文件必须经过 Python 安全编码规范检查。如果修改不包含 Python 文件，则跳过下面文件加载和 Python 检视流程。**

Python 安全编码规范检查（参考文件 [python-secure.md](./references/python-secure.md)）

### 步骤 4: 生成检视报告

重要：请务必遵守以下规则生成报告：
- 必须参照报告格式生成，不要新增或者删除章节；
- 发现的问题或修改建议必须标明具体的文件行号和函数名；
- 表格只显示检查结果为 ⚠️/❌ 的规范；
- 如果修改不涉及相关规范，标明“修改不涉及”。
- 将检视报告保存到项目根目录下的`hixl-pr-review-reports`文件夹下，文件名为“PR编号+检视日期”，例如“#666_2026-04-14.md”

报告格式：

```markdown
## 🤖 HIXL 代码检视报告

**PR**: #<pr_number> - <pr_title>

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

<描述本次 PR 的主要变更内容>

---

### 🔍 详细检查

#### 0. 🎯 HIXL 重点检查项 <✅/⚠️/❌>

> 依据 [hixl-review-focus.md](./references/hixl-review-focus.md) 中的“重点检查清单”

| 规范编号 | 检查维度 | 结果 | 说明 |
|------|----------|------|------|
| <从重点检查清单读取的编号> | <从重点检查清单读取的检查维度> | <✅/⚠️/❌> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 1. C++ 通用编码规范检查 <✅/⚠️/❌>

> 依据 [cpp-general.md](./references/cpp-general.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 2. C++ 安全编码规范检查 <✅/⚠️/❌>

> 依据 [cpp-secure.md](./references/cpp-secure.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 3. C++ 代码风格规范检查 <✅/⚠️/❌>

> 依据 [cpp-style.md](./references/cpp-style.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

#### 4. Python 安全编码规范检查 <✅/⚠️/❌>

> 依据 [python-secure.md](./references/python-secure.md) 中的“规范列表”

| 规范编号 | 规范名称 | 结果 | 说明 |
|------|----------|------|------|
| <从规范列表读取的编号> | <从规范列表读取的规范名称> | <✅/⚠️/❌> | <发现、风险说明或 N/A 原因> |
| ... | ... | ... | ... |

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

发布评论前向用户确认是否需要修改或发布检视报告，发布评论时使用 GitCode API：

```bash
curl -X POST \
  -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  -d "{\"body\":\"$(echo "$REPORT" | sed 's/"/\\"/g' | sed ':a;N;$!ba;s/\n/\\n/g')\"}" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/comments"
```

### 步骤 6: 发布 lgtm（可选）

如果严重程度为 Low 或 Medium，且 `auto_lgtm=true`：

```bash
curl -X POST \
  -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"body":"/lgtm"}' \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/666/comments"
```

## 严重程度判定

| 等级 | 条件 | 是否合入 | LGTM |
|------|------|---------|------|
| Low | 仅有建议性改进 | ✅ 可以 | ✅ 自动 |
| Medium | 有一般性问题 | ⚠️ 建议修改后 | ✅ 自动 |
| High | 有严重问题 | ❌ 需要修改 | ❌ 不发布 |
| Critical | 有安全漏洞或严重内存问题 | ❌ 需要修改 | ❌ 不发布 |

## 配置

### 首次使用

1. **获取 GitCode API Token**
   - 访问: https://gitcode.com/setting/token-classic
   - 生成 Token，权限: `api`, `write_repository`

2. **配置环境变量**
   ```bash
   export GITCODE_API_TOKEN=your_token_here
   ```

3. **或使用配置文件**
   ```bash
   mkdir -p ~/.hixl-pr-review
   echo "GITCODE_API_TOKEN=your_token_here" > ~/.hixl-pr-review/config
   chmod 600 ~/.hixl-pr-review/config
   ```

## 输入参数

- **pr_url** (必需): PR 页面链接
- **focus_areas** (可选): 检视重点 (编码规范问题/安全问题/代码风格问题/全部问题)
- **auto_lgtm** (可选): 中低风险自动 LGTM (true/false)

## 输出

```json
{
  "severity": "low",
  "can_merge": true,
  "issues_count": 2,
  "comment_posted": true,
  "lgtm_posted": true,
  "report_url": "https://gitcode.com/cann/hixl/pull/10#note_12345"
}
```
