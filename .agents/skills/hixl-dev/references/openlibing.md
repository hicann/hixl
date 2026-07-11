# OpenLiBing 失败详情（SPA）

cann/hixl 流水线的**具体失败原因**（codecheck 规则、编译 stderr、LLT 用例）托管在
[OpenLiBing](https://www.openlibing.com) 的前端页面，由 cann-robot 评论嵌入 GitCode。

静态 HTTP（curl / 普通网页抓取 / 仅 REST API）拿不到有效内容，必须用 Agent 的**浏览器自动化能力**
打开并读取页面 DOM（各产品名称不同，按当前 Agent 可用的浏览器工具执行）。

## 分工

| 信息 | 获取方式 |
| --- | --- |
| 是否在跑 / 是否通过 | [`gitcode-pipeline`](../../gitcode-pipeline/SKILL.md)（label、`gp-list` / `gp-wait` 等） |
| 触发 / 重试流水线 | `gitcode-pipeline`（评论 `compile` 或 retry 脚本） |
| codecheck 规则、文件行号、编译/LLT 详情 | 浏览器打开 OpenLiBing |

## 从 PR 评论提取链接

```bash
PR=<PR_NUMBER>
curl -sS -H "PRIVATE-TOKEN: $GITCODE_API_TOKEN" \
  "https://api.gitcode.com/api/v5/repos/cann/hixl/pulls/${PR}/comments?per_page=50" \
  | python3 -c "
import sys, json, re
base = 'https://www.openlibing.com/apps/'
qs = '?projectId=300033&codeHostingPlatformFlag=gitcode'
for c in reversed(json.load(sys.stdin)):
    body = c.get('body', '')
    for pat, label in [
        (r'pipelineDetail/[^\"\\s>]+', 'pipeline'),
        (r'entryCheckDashCode/[^\"\\s>]+', 'codecheck'),
    ]:
        m = re.search(pat, body)
        if m:
            print(f'{label}: {base}{m.group(0)}{qs}')
"
```

也可打开 `https://gitcode.com/cann/hixl/pull/<PR>/check`（同样是 SPA，需浏览器）。

## 浏览器排查步骤

1. 打开 codecheck 或 pipeline 的 OpenLiBing 链接（或 PR `/check` 页）。
2. 等待页面渲染完成（出现阶段名、规则表或日志区域）。
3. 定位最早失败的 stage / 规则，记录规则 ID、文件、行号或失败用例。
4. 区分根因失败与前序失败导致的级联取消。
5. 按本 skill 做最小修复与本地验证，再更新 PR 分支并重新触发 CI。

无浏览器能力、无登录权限或日志过期时，如实报告阻塞，不要臆测失败原因。
