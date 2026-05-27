---
name: gitcode-pr
description: |
  使用 GitCode API 创建 Pull Request 和获取 PR 评论。当用户需要**创建** GitCode PR、将代码**推送并创建合并请求**、**获取 PR 评论**、**查看 PR 讨论**、**查看 PR 改动**或**删除 PR 评论**时使用此 skill。支持读取普通评论和行内(diff)评论，包括评论内容、文件路径、代码行号等详细信息。
  **必须触发此 skill 的场景**（用户提到以下任何内容时使用）：
  - 创建/提交 PR：创建PR、提个PR、发PR、做个PR、帮我PR、生成PR、需要PR、pull request、merge request
  - 推送代码到远程：push代码、推代码、把代码推上去、提交到远程、推送到gitcode、提交代码到GitCode
  - 合并请求：合并请求、代码合入请求、请求合并、merge request
  - PR模板/描述：PR模板、PR描述、PR格式
  - 关联issue创建PR：关issue的PR、关联issue创建PR
  - 获取PR改动：查看PR变更、PR文件列表、PR改了什么、看PRdiff、获取PR文件
  - **获取 PR 评论**：查看PR评论、PR评论、获取评论、read comments
  - **查看 PR 讨论**：PR discussions、查看讨论、discussions
  - **删除 PR 评论**：删除评论、删除PR评论、移除评论、delete comment、移除这条评论
---

# GitCode PR Skill

创建 GitCode Pull Request 和获取 PR 评论的标准化流程。

## 工作流程

### 1. 获取访问令牌（第一步必须）

**询问用户**："请提供您的 GitCode 访问令牌（Access Token）"

检查环境变量：
```bash
echo $GITCODE_API_TOKEN
```

如果不存在，提示用户获取令牌：
1. 登录 [GitCode](https://gitcode.com)
2. 点击头像 → 设置 → 访问令牌
3. 创建新令牌，选择 `read_repository`、`write_repository` 和 `read_api` 权限
4. 复制令牌，建议保存到 `~/.bashrc`：`export GITCODE_API_TOKEN="your_token_here"`

### 2. 识别PR的目标仓库

#### 2.1 查询远程仓库

```bash
git remote -v
```

根据远程仓库 URL 确定目标仓库：
- **目标仓库**：PR 要合并到的仓库（通常是 origin 或 upstream）
- **源仓库**：当前工作分支所在的仓库

#### 2.2 提取仓库 owner/repo 信息

**关键**：所有 API 调用都需要使用当前仓库的 owner/repo，而非硬编码。

```bash
# 从远程 URL 提取 owner 和 repo
repo_url=$(git remote get-url origin)

# 处理不同 URL 格式
# SSH 格式: git@gitcode.com:owner/repo.git
# HTTPS 格式: https://gitcode.com/owner/repo.git
if [[ $repo_url == git@* ]]; then
  owner=$(echo $repo_url | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\1|')
  repo=$(echo $repo_url | sed 's|.*:\([^/]*\)/\([^/]*\)\.git$|\2|')
else
  owner=$(echo $repo_url | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\1|')
  repo=$(echo $repo_url | sed 's|.*gitcode\.com/\([^/]*\)/\([^/]*\)\.git$|\2|')
fi

# URL 编码（用于 API 路径）
encoded_repo=$(printf '%s' "${owner}/${repo}" | jq -sRr @uri)

echo "Owner: $owner"
echo "Repo: $repo"
echo "Encoded: $encoded_repo"  # 例如: cann%2Fge
```

**后续所有 API 调用都应使用这些变量**：
- GitHub API v5 格式：`/repos/${owner}/${repo}/...`

#### 2.3 查询 Fork 的原仓库（当当前仓库是 fork 时）

当当前仓库是 fork 仓库时，需要查询其 fork 的原仓库作为 PR 目标仓库：

```bash
# 查询仓库信息获取 fork 来源
curl -s -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/${owner}/${repo}" | \
  jq -r '
    if .fork then
      "原仓库: " + .parent.full_name + "\n" +
      "原仓库URL: " + .parent.html_url + "\n" +
      "目标分支: " + .parent.default_branch
    else
      "这不是一个 fork 仓库，直接使用当前仓库"
    end
  '
```

**响应关键字段**：
| 字段 | 说明 |
|------|------|
| `fork` | 是否为 fork 仓库（`true`/`false`） |
| `parent.full_name` | 原仓库完整名称（格式：`owner/repo`） |
| `parent.html_url` | 原仓库网页地址 |
| `parent.default_branch` | 原仓库默认分支 |

**示例输出**：
```json
{
  "fork": true,
  "parent": {
    "full_name": "cann-agent/skills",
    "html_url": "https://gitcode.com/cann-agent/skills",
    "default_branch": "main"
  }
}
```

### 3. 获取 PR 评论和讨论

#### 获取 PR 讨论列表（包含行内评论）

GitCode 使用 v5 API 获取 PR 评论：

```bash
# 获取 PR 的所有评论（包括行内评论）
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments?access_token=$GITCODE_API_TOKEN"
```

## GitCode API v5 说明
**认证头**：
- 推荐使用 query 参数：`?access_token=$GITCODE_API_TOKEN`
- 也可使用 Header：`PRIVATE-TOKEN: $GITCODE_API_TOKEN`

#### 解析评论数据

v5 API 返回**直接数组**，不是包装对象。评论数据包含以下关键字段：

| 字段 | 说明 |
|------|------|
| `id` | 评论 ID |
| `discussion_id` | 讨论 ID（用于回复） |
| `body` | 评论内容 |
| `user.login` | 评论作者用户名 |
| `created_at` | 评论创建时间 |
| `updated_at` | 评论更新时间 |
| `comment_type` | 评论类型：`pr_comment` 表示普通评论，其他表示行内评论 |
| `path` | 行内评论的文件路径（仅行内评论） |
| `position` | 行内评论的行号（仅行内评论） |

**示例返回结构**：
```json
[
  {
    "id": 171409022,
    "discussion_id": "b4ecb001ec8af59d02c3ffeffeb83fb7a8226c84",
    "body": "评论内容",
    "user": {
      "login": "username",
      "name": "显示名称"
    },
    "created_at": "2026-05-14T15:25:32+08:00",
    "comment_type": "pr_comment"
  }
]
```

**行内评论示例**：
```json
{
  "id": 123456,
  "body": "这段代码需要优化",
  "path": null,
  "position": null,
  "comment_type": "DiffNote"
}
```

**重要说明**：列表接口返回的 `path` 和 `position` 字段为 `null`。要获取完整位置信息，需要使用单条评论接口。

#### 获取单条评论详情（包含完整位置信息）

**端点**：
```
GET /repos/:owner/:repo/pulls/comments/:id
```

**示例**：
```bash
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/comments/<COMMENT_ID>?access_token=$GITCODE_API_TOKEN"
```

**返回完整字段**：
```json
{
  "id": 171419900,
  "body": "行内评论内容",
  "path": null,
  "position": {
    "base_sha": "d8eb570...",
    "start_sha": "d8eb570...",
    "head_sha": "e54ef3...",
    "old_path": "runtime/v1/executor.cc",
    "new_path": "runtime/v1/executor.cc",
    "position_type": "text",
    "old_line": 212,
    "new_line": 212
  },
  "comment_type": "DiffNote",
  "user": "username",
  "created_at": "2026-05-14T16:06:24+08:00"
}
```

**关键位置字段**：
| 字段 | 说明 |
|------|------|
| `position.new_path` | 文件路径 |
| `position.new_line` | 行号（新代码） |
| `position.old_line` | 行号（旧代码） |
| `position.base_sha` | Base 提交 SHA |
| `position.head_sha` | Head 提交 SHA |

**接口对比**：
| 接口 | 位置信息 | comment_type | 用途 |
|------|---------|-------------|------|
| `/pulls/:number/comments` | `path`/`position` 为 `null` | 有正确类型 | 获取评论列表 |
| `/pulls/comments/:id` | 完整 `position` 对象 | 为 `null` | 获取单条评论详情 |

**注意**：单条评论接口返回的 `comment_type` 字段为 `null`，要获取评论类型，需要使用列表接口。

### 4. 获取 PR 文件变更

```bash
# 使用 v5 API（files.json）
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/files.json?access_token=$GITCODE_API_TOKEN"

# 解析第一个文件路径
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/files.json?access_token=$GITCODE_API_TOKEN" | \
  jq -r '.diffs[0].statistic.new_path'
```

**响应字段说明**：
- `diffs[].statistic.new_path` - 新文件路径
- `diffs[].statistic.old_path` - 旧文件路径
- `diff_refs.base_sha` - Base SHA
- `diff_refs.head_sha` - Head SHA
- `diff_refs.start_sha` - Start SHA

### 5. 提交行内评论（支持多行选择）

#### 使用 v5 API（推荐）

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "评论内容",
    "path": "文件相对路径",
    "position": <结束行号>,
    "start_position": <起始行号>
  }'
```

**参数说明**：

| 参数 | 说明 | 必需 |
|------|------|------|
| `body` | 评论内容 | ✅ |
| `path` | 文件相对路径 | ✅ |
| `position` | 结束行号 | ✅ |
| `start_position` | 起始行号（多行选择） | 多行时 |

**单行评论示例**：
```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/2805/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "第70行需要重构",
    "path": "runtime/v1/opskernel_executor/ops_kernel_executor_manager.cc",
    "position": 70
  }'
```

**多行评论示例**：
```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/2805/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "第70-75行需要优化",
    "path": "runtime/v1/opskernel_executor/ops_kernel_executor_manager.cc",
    "position": 75,
    "start_position": 70
  }'
```

**注意**：`start_position` 参数是未文档化的参数，但测试证明可能有效。

#### 创建评论的返回值问题

**重要**：POST 创建评论 API 立即返回的是**哈希字符串格式 ID**，不是数字 ID。

**返回示例**：
```json
{
  "id": "b227720346ef5a37e410d17e76a1c1e1f60d5d80",  // 哈希字符串
  "discussion_id": null,
  "body": "评论内容",
  "created_at": null
}
```

**获取真正的数字 ID**：
创建评论后，需要查询评论列表获取数字 ID（用于后续删除等操作）：
```bash
# 查询评论列表，匹配创建的评论内容
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments?access_token=$GITCODE_API_TOKEN" | \
  jq '.[] | select(.body | contains("评论内容")) | {id: .id, discussion_id: .discussion_id, comment_type: .comment_type}'

# 返回：
# {
#   "id": 171431239,  // 数字 ID
#   "discussion_id": "b227720346ef5a37e410d17e76a1c1e1f60d5d80",
#   "comment_type": "DiffNote"
# }
```

### 6. 提交普通评论

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "评论内容"
  }'
```

### 7. 回复已有评论

v5 API 回复评论需要使用 `discussions` 端点：

```
POST /repos/:owner/:repo/pulls/:number/discussions/:discussion_id/comments
```

**步骤**：

1. **获取 discussion_id**：
```bash
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments?access_token=$GITCODE_API_TOKEN" | \
  jq '.[] | {id: .id, discussion_id: .discussion_id}'
```

2. **回复评论**：
```bash
curl -s -X POST \
  -H "Content-Type: application/json" \
  "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/discussions/<DISCUSSION_ID>/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "回复内容"
  }'
```

3. **验证回复成功**：
```bash
# 使用单条评论接口验证 discussion_id 是否匹配
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/comments/<REPLY_ID>?access_token=$GITCODE_API_TOKEN" | \
  jq '{discussion_id: .discussion_id, comment_type: .comment_type}'
```

**参数**：
- `discussion_id`（URL 中）：要回复的讨论 ID
- `body`：回复内容

**注意**：
- 回复评论不在 `/pulls/:number/comments` 列表显示，需要用单条接口验证
- 回复行内评论返回 `comment_type: "DiffNote"`
- 回复普通评论返回 `comment_type: "DiscussionNote"`

### 8. 删除 PR 评论

当用户需要删除 PR 中的评论时使用此功能。

**重要**：只能删除自己创建的评论，或具有仓库管理权限。

```bash
# 获取评论 ID（使用 v5 API）
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/<PR_NUMBER>/comments?access_token=$GITCODE_API_TOKEN" | \
  jq '.[] | {id: .id, body: .body}'

# 删除评论（使用 v5 API）
curl -X DELETE \
  "https://api.gitcode.com/api/v5/repos/${owner}/${repo}/pulls/comments/<COMMENT_ID>?access_token=$GITCODE_API_TOKEN"
```

**注意**：评论 ID 是哈希字符串，不是数字。

详细 API 参数、响应码和权限说明请参考 `references/gitcode_api.md` 的「删除 PR 评论」章节。

### 9. 创建 PR 的正确流程

**关键**：源分支必须基于目标分支，确保PR只包含期望的变更。

```bash
# 1. 拉取目标分支
git fetch origin <目标分支>

# 2. 基于目标分支创建新分支
git checkout -b <新分支名> origin/<目标分支>

# 3. Cherry-pick需要的commit
git cherry-pick <commit-sha>  # 单个commit
# 或
git cherry-pick <sha1> <sha2>  # 多个commit

# 4. 推送新分支
git push <个人远程仓库名> <新分支名> -u
```

**示例：往 origin/9.0.0 提交单个commit**
```bash
git fetch origin 9.0.0
git checkout -b fix/gcc13-link-error origin/9.0.0
git cherry-pick 4fbf3b183
git push hgjupstream fix/gcc13-link-error -u
  ```

### 10. 创建 PR

**目标分支策略**：
- **默认合入 `develop` 分支**（日常开发、新功能、bugfix）
- 仅当用户明确要求时，才合入其他分支（如 `master`、`release/x.y`）

#### 10.1 读取 PR 模板

**重要**：PR 描述必须严格遵循仓库中的模板文件，不要使用硬编码模板。

```bash
# 读取仓库 PR 模板
cat .gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md
```

模板文件路径固定为 `.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md`。根据模板结构填充各字段内容。

#### 10.2 使用 API 创建 PR

```bash
curl -s -X POST "https://gitcode.com/api/v5/repos/${owner}/${repo}/pulls" \
  -H "Authorization: Bearer $GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "title": "docs: 优化文档描述(#32)",
    "head": "username:fix/issue-32-description",
    "base": "develop",
    "body": "按模板填充的PR描述"
  }'
```

**参数说明**：
- `title`: PR 标题（必填）
- `head`: 源分支，格式 `username:branch`（必填）
- `base`: 目标分支，**默认为 `develop`**，除非用户明确要求合入其他分支
- `body`: PR 描述（Markdown 格式，按 `.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md` 模板填充）

### 11. PR URL 格式

创建 PR 后，PR 的访问地址格式为（注意是 `/pull/` 而非 `/pulls/`）：

```
https://gitcode.com/${owner}/${repo}/pull/<PR_NUMBER>
```

**示例**：`https://gitcode.com/${owner}/${repo}/pull/1807`

### 12. 后续跟进

- 检查 CI/CD 运行结果
- 回复审查者意见
- 根据反馈更新代码

---

## PR 标题格式

遵循 Conventional Commits 规范：

```
<type>: <描述>(#issue_id)
```

**类型**：

| 类型 | 说明 |
|------|------|
| `feat` | 新功能 |
| `fix` | Bug 修复 |
| `docs` | 文档更新 |
| `style` | 代码风格 |
| `refactor` | 重构 |
| `perf` | 性能优化 |
| `test` | 测试相关 |
| `chore` | 构建/工具 |

**示例**：
- `docs: 优化docs/api/README.md中的ge命名空间描述(#32)`
- `fix: 修复三库json下载安装问题`
- `feat: 添加operator注册V2接口支持(#45)`

---

## PR 描述模板

创建 PR 时**必须读取并使用**仓库中的模板文件：`.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md`

**步骤**：
1. 使用 `read` 工具读取 `.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md` 文件内容
2. 根据实际变更填充模板中的各字段（描述、测试项、测试结果等）
3. 将对应选项的 `[ ]` 改为 `[x]` 勾选
4. Checklist 默认全部勾选 `[x]`
5. 将填充后的模板内容作为 `body` 参数传入创建 PR 的 API

**不要使用硬编码的模板内容**，始终以仓库中的模板文件为准。

---

## Resources

### references/gitcode_api.md

GitCode API 完整参考文档，用于：
- 了解 API 参数格式
- 查看响应结构
- 排查 API 调用问题
