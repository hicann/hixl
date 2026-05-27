---
name: gitcode-issue
description: '读取 GitCode issue 详情和评论。当用户提到 GitCode issue 时必须使用此技能。

**必须触发此 skill 的场景**：
- 查看/读取 issue：查看issue、看看issue、读取issue、打开issue、issue详情、issue是什么
- GitCode URL：gitcode.com/**/issues/**、issue链接
- 直接说编号：issue 123、#123、问题123
- 查看评论：issue评论、评论内容

**重要**：不要使用 WebFetch 或 curl，内容通过 JavaScript 动态加载。使用 GitCode API 获取。'
---

# GitCode Issue 读取器

此技能提供 GitCode API 集成，用于读取和分析 GitCode 项目中的 issues。

## 快速开始
**第一步**：看环境变量 GITCODE_API_TOKEN 是否存在，如果不存在执行第二步，如果存在，执行第三步
**第二步**：询问用户的 GitCode 访问令牌
需要提醒用户如何获取
获取令牌步骤：
1. 登录 [GitCode](https://gitcode.com)
2. 点击头像 → 设置 → 访问令牌
3. 创建新令牌，选择 `read_repository` 权限
4. 复制并安全保存你的令牌
5. 建议保存在 ~/.bashrc中， export GITCODE_API_TOKEN="your_token_here"

**第三步**：读取特定 issue：
```bash
curl -s -H "private-token: $GITCODE_API_TOKEN" "https://gitcode.com/api/v5/repos/{owner}/{repo}/issues/{issue_number}"
```

## API 使用

### 认证
GitCode API 需要私有令牌才能访问：
- Header: `private-token: YOUR_TOKEN`
- 将 `YOUR_TOKEN` 替换为你的实际访问令牌

### API 端点
- **单个 Issue**：`GET /api/v5/repos/{owner}/{repo}/issues/{number}`
- **Issue 列表**：`GET /api/v5/repos/{owner}/{repo}/issues`
- **Issue 评论**：`GET /api/v5/repos/{owner}/{repo}/issues/{number}/comments`

### 响应格式
Issues API 返回包含以下关键字段的 JSON：
- `number`：Issue ID
- `title`：Issue 标题
- `state`：open/closed（状态）
- `body`：Issue 描述（markdown 格式）
- `user`：作者信息
- `created_at`：创建时间戳
- `updated_at`：最后更新时间戳
- `labels`：标签对象数组

## Issue 分析指南

分析 issues 时：
1. 检查是 bug 报告、功能请求还是问题咨询
2. 注意环境信息（CANN 版本、硬件平台）
3. 查找错误日志和堆栈跟踪
4. 检查标签进行分类
5. 审查评论以获取更多上下文

## 常见 Issue 类型

- **Bug 报告**：通常包含图编译错误日志
- **功能请求**：新算子支持或图优化
- **环境问题**：安装或兼容性问题
