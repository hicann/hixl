# GitCode PR API 参考（v5）

## 目录

1. [认证方式](#认证方式)
2. [获取仓库信息（查询 Fork 来源）](#获取仓库信息查询-fork-来源)
3. [获取 PR 信息](#获取-pr-信息)
4. [获取 PR 文件变更](#获取-pr-文件变更)
5. [获取 PR 评论列表](#获取-pr-评论列表)
6. [提交普通评论](#提交普通评论)
7. [提交行内评论](#提交行内评论)
8. [回复 Discussion](#回复-discussion)
9. [删除 PR 评论](#删除-pr-评论)

---

## 认证方式

GitCode v5 API 使用以下认证方式：

| 认证方式 | 说明 | 推荐度 |
|---------|------|--------|
| Query 参数 | `?access_token=<token>` | ✅ 推荐（官方文档） |
| Header | `PRIVATE-TOKEN: <token>` | ✅ 可用 |
| Header | `Authorization: Bearer <token>` | ⚠️ 部分端点可用 |

**获取访问令牌**：
1. 登录 [GitCode](https://gitcode.com)
2. 点击头像 → 设置 → 访问令牌
3. 创建新令牌，选择 `read_repository`、`write_repository` 和 `read_api` 权限
4. 保存到环境变量：`export GITCODE_API_TOKEN="your_token_here"`

---

## 获取仓库信息（查询 Fork 来源）

查询仓库详情，判断是否为 fork 仓库并获取其原仓库信息。

### API 端点

```bash
GET https://api.gitcode.com/api/v5/repos/:owner/:repo
```

### 请求示例

```bash
curl -s "https://api.gitcode.com/api/v5/repos/${owner}/${repo}?access_token=$GITCODE_API_TOKEN"
```

### 响应字段

| 字段 | 类型 | 说明 |
|------|------|------|
| `id` | integer | 仓库 ID |
| `name` | string | 仓库名称 |
| `full_name` | string | 仓库完整名称（格式：`owner/repo`） |
| `fork` | boolean | 是否为 fork 仓库 |
| `parent` | object | 如果是 fork，包含原仓库信息 |
| `default_branch` | string | 默认分支 |

---

## 获取 PR 信息

### API 端点

```bash
GET https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number
```

### 路径参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `owner` | string | ✅ | 仓库所属空间地址（组织或个人的地址 path） |
| `repo` | string | ✅ | 仓库路径（path） |
| `number` | integer | ✅ | PR序号 |

### 请求示例

```bash
curl -s "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805?access_token=$GITCODE_API_TOKEN"
```

### 响应示例

```json
{
  "number": 2805,
  "title": "PR标题",
  "state": "open",
  "base": {
    "sha": "base_commit_sha"
  },
  "head": {
    "sha": "head_commit_sha"
  }
}
```

---

## 获取 PR 文件变更

### API 端点

```bash
GET https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/files.json
```

### 路径参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `owner` | string | ✅ | 仓库所属空间地址 |
| `repo` | string | ✅ | 仓库路径 |
| `number` | integer | ✅ | PR序号 |

### 请求示例

```bash
curl -s "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/files.json?access_token=$GITCODE_API_TOKEN"
```

### 响应字段说明

| 字段 | 说明 |
|------|------|
| `diffs[].statistic.new_path` | 新文件路径 |
| `diffs[].statistic.old_path` | 旧文件路径 |
| `diffs[].added_lines` | 新增行数 |
| `diffs[].remove_lines` | 删除行数 |
| `diff_refs.base_sha` | Base SHA |
| `diff_refs.start_sha` | Start SHA |
| `diff_refs.head_sha` | Head SHA |

### 响应示例

```json
{
  "code": 0,
  "count": 1,
  "diff_refs": {
    "base_sha": "...",
    "start_sha": "...",
    "head_sha": "..."
  },
  "diffs": [
    {
      "statistic": {
        "new_path": "runtime/v1/opskernel_executor/ops_kernel_executor_manager.cc",
        "old_path": "runtime/v1/opskernel_executor/ops_kernel_executor_manager.cc"
      },
      "added_lines": 0,
      "remove_lines": 1,
      "content": {
        "text": [...]
      }
    }
  ]
}
```

---

## 获取 PR 评论列表

### API 端点

```bash
GET https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments
```

### 路径参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `owner` | string | ✅ | 仓库所属空间地址 |
| `repo` | string | ✅ | 仓库路径 |
| `number` | integer | ✅ | PR序号 |

### 请求示例

```bash
curl -s "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments?access_token=$GITCODE_API_TOKEN"
```

### 响应字段

| 字段 | 说明 |
|------|------|
| `[].id` | 评论 ID |
| `[].body` | 评论内容 |
| `[].path` | 文件路径（行内评论） |
| `[].position` | 行号（行内评论） |

---

## 提交普通评论

### API 端点

```bash
POST https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments
```

### 路径参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `owner` | string | ✅ | 仓库所属空间地址 |
| `repo` | string | ✅ | 仓库路径 |
| `number` | integer | ✅ | PR序号 |

### Body 参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `body` | string | ✅ | 评论内容 |

### 请求示例

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "这是一个普通评论"
  }'
```

### 响应示例

```json
{
  "id": "comment_id_hash",
  "body": "这是一个普通评论",
  "path": null,
  "position": null
}
```

---

## 提交行内评论

### API 端点

```bash
POST https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/comments
```

### 路径参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `owner` | string | ✅ | 仓库所属空间地址 |
| `repo` | string | ✅ | 仓库路径 |
| `number` | integer | ✅ | PR序号 |

### Body 参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `body` | string | ✅ | 评论内容 |
| `path` | string | ✅ | 文件相对路径 |
| `position` | integer | ✅ | 代码行号（结束行） |
| `start_position` | integer | ❌ | 起始行号（多行选择，未文档化） |

### 单行评论示例

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "第70行有问题",
    "path": "runtime/v1/opskernel_executor/ops_kernel_executor_manager.cc",
    "position": 70
  }'
```

### 多行评论示例

**注意**：`start_position` 参数是未文档化的参数，但测试证明可能有效：

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "第70-75行需要重构",
    "path": "runtime/v1/opskernel_executor/ops_kernel_executor_manager.cc",
    "position": 75,
    "start_position": 70
  }'
```

### 响应示例

```json
{
  "id": "comment_id_hash",
  "body": "第70行有问题",
  "path": null,
  "position": null
}
```

---

## 回复 Discussion

### API 端点

```bash
POST https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/:number/discussions/:discussion_id/comments
```

### 路径参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `owner` | string | ✅ | 仓库所属空间地址 |
| `repo` | string | ✅ | 仓库路径 |
| `number` | integer | ✅ | PR序号 |
| `discussion_id` | string | ✅ | 讨论 ID |

### Body 参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `body` | string | ✅ | 回复内容 |

### 请求示例

```bash
curl -X POST \
  -H "Content-Type: application/json" \
  -H "Accept: application/json" \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/2805/discussions/171372926/comments?access_token=$GITCODE_API_TOKEN" \
  -d '{
    "body": "感谢反馈，我会修改"
  }'
```

---

## 删除 PR 评论

### API 端点

```bash
DELETE https://api.gitcode.com/api/v5/repos/:owner/:repo/pulls/comments/:id
```

### 路径参数

| 参数 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `owner` | string | ✅ | 仓库所属空间地址 |
| `repo` | string | ✅ | 仓库路径 |
| `id` | string | ✅ | 评论 ID |

### 请求示例

```bash
curl -X DELETE \
  "https://api.gitcode.com/api/v5/repos/cann/ge/pulls/comments/comment_id_hash?access_token=$GITCODE_API_TOKEN"
```

### 响应

| 状态码 | 说明 |
|--------|------|
| `200` | 成功删除 |
| `400` | 参数错误（评论 ID 格式问题） |
| `401` | 未授权 |
| `403` | 无权限删除此评论 |
| `404` | 评论不存在 |

---

## 注意事项

1. **Token 配置**：使用环境变量 `GITCODE_API_TOKEN`
2. **认证方式**：推荐使用 query 参数 `?access_token=<token>`（官方文档推荐）
3. **项目路径**：v5 API 使用 `/repos/:owner/:repo`，不需要 URL 编码
4. **评论 ID**：返回的评论 ID 是哈希字符串，不是数字
5. **多行评论**：`start_position` 参数未文档化，但测试证明可能有效
6. **文件变更格式**：`files.json` 返回的数据结构嵌套在 `diffs[].statistic` 中

---

## 测试脚本

已提供测试脚本用于验证 API 功能：

- `scripts/test_api_v5.sh` - 测试基本 v5 API 功能
- `scripts/test_inline_comment.sh` - 测试行内评论和多行选择

---

## 官方 API 文档

以下 HTML 文档来自 GitCode 官方（位于 `references/` 目录）：

- `提交pull request 评论 _ GitCode 帮助文档.html` - v5 API 提交评论官方文档
- `获取某个Pull Request的所有评论 _ GitCode 帮助文档.html` - v5 API 获取评论官方文档
- `pr提交的文件变更信息 _ GitCode 帮助文档.html` - v5 API 文件变更官方文档
- `回复Pull Request评论 _ GitCode 帮助文档.html` - v5 API 回复评论官方文档

在线文档：https://docs.gitcode.com/docs/apis/