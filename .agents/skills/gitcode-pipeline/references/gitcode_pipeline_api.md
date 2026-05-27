# GitCode 流水线 API 使用摘要

> **核心规则**: 所有接口调用中的 `pipeline_detail` 字段均来自**步骤1流水线列表**返回的 `pipeline_detail` 字段。

---

## 步骤0: 重试流水线

通过 API 重试指定 PR 的流水线，无需评论触发。适用于流水线失败后需要重跑的场景（如偶发环境问题）。

```
curl -s -X POST "https://api.gitcode.com/api/v5/repos/{owner}/{repo}/merge_requests/{pr_number}/pipelines/retry?access_token=$GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"pipeline_detail": "{pipeline_detail}", "pipeline_run_id": "{pipeline_run_id}"}'
```

**路径参数**:
| 参数 | 说明 |
|------|------|
| `{owner}` | 仓库所有者 |
| `{repo}` | 仓库名称 |
| `{pr_number}` | PR 编号 |

**Body 参数**:
| 参数 | 必填 | 说明 |
|------|------|------|
| `pipeline_detail` | 是 | 步骤1流水线列表中返回的 `pipeline_detail` 字段完整 JSON 字符串 |
| `pipeline_run_id` | 否 | 流水线运行实例 ID |

**参考完整命令**:
```shell
curl -s -X POST "https://api.gitcode.com/api/v5/repos/{owner}/{repo}/merge_requests/2260/pipelines/retry?access_token=$GITCODE_API_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"pipeline_detail": "{\"hook_id\":\"42205\",\"hook_type\":\"project\",\"job_log_url\":null,\"job_run_time\":null,\"pipeline_total_time\":806,\"project_id\":\"8e3ae2e30c3a4cf4a75e4cdc75d41bac\",\"pipeline_host\":\"https://cloudpipeline-ext.cn-north-4.myhuaweicloud.com\"}", "pipeline_run_id": "b4ec5188566b4810b97c5069384e08c4"}'
```

**与评论触发的区别**:
- 评论 `compile`：触发全新的流水线运行，会产生新的 pipeline 记录
- API `retry`：重试指定流水线，更精准，适用于已知偶发失败的重跑

---

## 步骤1: 查询流水线状态
命令
```
curl --location --request GET 'https://api.gitcode.com/api/v5/repos/{owner}/{repo}/merge_requests/{pr_number}/pipeline?access_token=$GITCODE_API_TOKEN&type=report_pipeline'
```

**响应示例**:

```json
{
  "page_num": 1,
  "page_size": 20,
  "total": 1,
  "page_count": 1,
  "content": [
    {
      "id": 505783,
      "web_url": "https://gitcode.com/stevenaw0/ge/pipelines/505783",
      "sha": "3294d777fd7eab87b20ea512d050cfc7b280cc6e",
      "ref": "tmp",
      "status": "failed",
      "name": "cann_ge_all",
      "type": "REPORT PIPELINE",
      "pipeline_id": "c85338dd05464b82b0a098dacc32415d",
      "pipeline_run_id": "b4ec5188566b4810b97c5069384e08c4",
      "pipeline_detail": "{\"hook_id\":\"42205\",\"hook_type\":\"project\",\"job_log_url\":null,\"job_run_time\":null,\"pipeline_total_time\":806,\"project_id\":\"8e3ae2e30c3a4cf4a75e4cdc75d41bac\",\"pipeline_host\":\"https://cloudpipeline-ext.cn-north-4.myhuaweicloud.com\"}",
      "created_at": "2026-04-27T16:26:04.086+08:00",
      "updated_at": "2026-04-27T16:39:29.818+08:00",
      "user": {
        "name": "cann-robot",
        "username": "cann-robot"
      }
    }
  ]
}
```

**逻辑判断**：
- 如果`status`为成功，可以立即返回了，不需要执行后续步骤
- 否则，在输出中获取`｛pipeline_id｝`，`｛pipeline_run_id｝`，`｛pipeline_detail｝`　三个字段．，继续后续步骤

**关键字段说明**:
- `content[].pipeline_id`: 用于步骤2的路径参数
- `content[].pipeline_run_id`: 用于步骤2的 body 参数
- `content[].pipeline_detail`: **必须完整保存**，用于后续所有接口调用的 body 参数

---

## 步骤2: 查询流水线详情

```
curl --location --request POST 'https://api.gitcode.com/api/v5/repos/{owner}/{repo}/pipelines/{pipeline_id}/pipeline-runs/detail?access_token=$GITCODE_API_TOKEN' \
--header 'Content-Type: application/json' \
--data-raw '{
"pipeline_run_id": "{pipeline_run_id}",
"pipeline_detail": "{pipeline_detail}"
}'
```

**响应示例**:

```json
{
  "id": "b4ec5188566b4810b97c5069384e08c4",
  "pipeline_id": "c85338dd05464b82b0a098dacc32415d",
  "name": "cann_ge_all",
  "status": "FAILED",
  "stages": [
    {
      "name": "获取pr文件",
      "status": "COMPLETED",
      "jobs": [
        {
          "id": "f9ee261e299742e6b97779ee7761bef2",
          "name": "执行shell",
          "status": "COMPLETED",
          "steps": [{
            "id": "cedd78aa85dc42b59ee4473f35e6a26e",
            "job_run_id": "f9ee261e299742e6b97779ee7761bef2",
            "name": "解析CI分支",
            "task": "official_shell_plugin",
            "status": "COMPLETED"
          }]
        },
        {
          "id": "e5ca19a3f8e5439f9b829418b7e737ab",
          "name": "get_pr_file",
          "status": "COMPLETED",
          "steps": [{
            "id": "93f8d23b37c4435082c0aadc3b790dbc",
            "job_run_id": "e5ca19a3f8e5439f9b829418b7e737ab",
            "name": "get_pr_file",
            "task": "official_devcloud_cloudBuild",
            "status": "COMPLETED"
          }]
        }
      ]
    },
    {
      "name": "子流水线",
      "status": "FAILED",
      "jobs": [
        {
          "id": "990b3fe2178c44e388889bf52259bbc2",
          "name": "static-check",
          "status": "COMPLETED",
          "steps": [{
            "id": "2af66d16e5eb4fee865cc71b0039b9db",
            "job_run_id": "990b3fe2178c44e388889bf52259bbc2",
            "name": "static-check",
            "task": "official_devcloud_subPipeline",
            "status": "COMPLETED"
          }]
        },
        {
          "id": "41674a06d0c0444692b2aa240e467530",
          "name": "compile",
          "status": "FAILED",
          "message": "步骤compile执行失败，错误信息：点击任务查看详情",
          "steps": [{
            "id": "26216a0e34b84470a85b31b644184915",
            "job_run_id": "41674a06d0c0444692b2aa240e467530",
            "name": "compile",
            "task": "official_devcloud_subPipeline",
            "status": "FAILED"
          }]
        },
        {
          "id": "b977f183e0844f69bcfecc50f178a24e",
          "name": "llt",
          "status": "FAILED",
          "message": "步骤llt执行失败，错误信息：点击任务查看详情",
          "steps": [{
            "id": "6ccff51126ac4c75bc7c13ec96d5e725",
            "job_run_id": "b977f183e0844f69bcfecc50f178a24e",
            "name": "llt",
            "task": "official_devcloud_subPipeline",
            "status": "FAILED"
          }]
        }
      ]
    },
    {
      "name": "后处理阶段",
      "status": "FAILED",
      "jobs": [
        {
          "id": "99462c67c7c94ccea1ac5d62eb4c21b3",
          "name": "last_comment",
          "status": "FAILED",
          "message": "{\"errorMessage\":\" 构建任务执行失败!\",\"errorCode\":\"DEV-CODECI-35002\"}",
          "steps": [{
            "id": "051e7c2994e24b40bc3ec12bde38ff96",
            "job_run_id": "99462c67c7c94ccea1ac5d62eb4c21b3",
            "name": "last_comment",
            "task": "official_devcloud_cloudBuild",
            "status": "FAILED"
          }]
        }
      ]
    }
  ]
}
```

**后续处理思路**：
- 对于`stages[]` 表示流水线的几个阶段，阶段间串行，前面阶段失败就不会执行后面的阶段了．因此如果该阶段失败(`stages[].status == "FAILED"`)，之需要查询该阶段任务的失败原因就可以了，不需要查询后续阶段的失败原因了．
- 对于`stages[].jobs[]`: 表示每个阶段的所有任务，每个阶段内的所有任务是并发执行的，如果这个阶段失败了，需要获取所有任务的失败日志
- 对于`stages[].jobs[].steps[]`: 表示每个任务的步骤，如果失败了，需要获取所有失败的步骤的日志
- 对于`stages[].jobs[].steps[].task == 'official_devcloud_subPipeline'`的步骤，内部还包含着多个子任务，需要额外调用一次步骤3a和步骤3b, 需要获取`steps[].id`，例如`26216a0e34b84470a85b31b644184915`作为`｛step_run_ids｝`
- 其他步骤（如 `official_devcloud_cloudBuild`、`official_shell_plugin`），如果要调用步骤4，需要获取　`jobs[].id`

**常见错误**：不要将 `content[].id`（数字，如 `525642`）当作 `${pipeline_id}` 使用，这会导致 400 错误。

---

## 步骤3a: 查询步骤输出

**逻辑判断**：`steps[0].task == 'official_devcloud_subPipeline'`　类型需要额外执行步骤3a: 查询步骤输出，获取新的`{sub_pipeline_run_id}` `{sub_pipeline_id}`，再调用步骤2: 查询流水线详情，这一点非常重要．

```
curl --location --request POST 'https://api.gitcode.com/api/v5/repos/{owner}/{repo}/pipelines/{pipeline_id}/pipeline-runs/｛pipeline_run_id｝/steps/gitcode/outputs?access_token=$GITCODE_API_TOKEN' \
--header 'Content-Type: application/json' \
--data-raw '{
    "pipeline_detail": "{pipeline_detail}",
    "step_run_ids":"｛step_run_ids｝"
}'
```
参考完整命令：
```shell
curl --location --request POST 'https://api.gitcode.com/api/v5/repos/{owner}/{repo}/pipelines/c85338dd05464b82b0a098dacc32415d/pipeline-runs/b4ec5188566b4810b97c5069384e08c4/steps/gitcode/outputs?access_token=$GITCODE_API_TOKEN' \
--header 'Content-Type: application/json' \
--data-raw '{
    "pipeline_detail": "{\"hook_id\":\"36634\",\"hook_type\":\"project\",\"job_log_url\":null,\"job_run_time\":null,\"pipeline_total_time\":null,\"project_id\":\"1edbdd3f43a64a6dbd3825cd8dcb04c2\",\"pipeline_host\":\"https://cloudpipeline-ext.cn-north-4.myhuaweicloud.com\"}",
    "step_run_ids":"26216a0e34b84470a85b31b644184915"
}'
```

**响应示例**:
```json
{
  "step_outputs": [
    {
      "step_run_id": "26216a0e34b84470a85b31b644184915",
      "output_result": [
        {
          "key": "pipeline_run_id",
          "value": "9e31fe9b20ee4da8b8eb565fa40fb0b3",
          "primitive": true
        },
        {
          "key": "pipeline_id",
          "value": "dcd161850837402293f0c47cda6b9921",
          "primitive": true
        }
      ]
    }
  ],
  "current_system_time": 1777452890119
}
```
- 获取`9e31fe9b20ee4da8b8eb565fa40fb0b3`作为`{sub_pipeline_run_id}`，
- 获取`dcd161850837402293f0c47cda6b9921`作为`{sub_pipeline_id}`
###

## 步骤3b: 查询子流水线详情
```shell
curl --location --request POST 'https://api.gitcode.com/api/v5/repos/{owner}/{repo}/pipelines/{sub_pipeline_id}/pipeline-runs/detail?access_token=$GITCODE_API_TOKEN' \
--header 'Content-Type: application/json' \
--data-raw '{
    "pipeline_run_id": "{sub_pipeline_run_id}",
    "pipeline_detail": "{pipeline_detail}"
}'
```
**路径参数** `sub_pipeline_id`: 来自步骤3a的 `pipeline_id` value

**Body 参数**:
| 参数 | 来源 |
|------|------|
| `pipeline_run_id` | 步骤3a的 `pipeline_run_id` value |
| `pipeline_detail` | 步骤1的 `pipeline_detail` |

**子流水线详情输出示例**（compile 子流水线）：
```json
{
  "id": "9e31fe9b20ee4da8b8eb565fa40fb0b3",
  "pipeline_id": "dcd161850837402293f0c47cda6b9921",
  "name": "cann_ge_compile",
  "status": "FAILED",
  "stages": [
    {
      "name": "Image",
      "status": "COMPLETED",
      "jobs": [
        {
          "id": "e692476a075147f5b07239ad4164c515",
          "job_run_id": "e692476a075147f5b07239ad4164c515",
          "name": "解析image",
          "status": "COMPLETED",
          "steps": [{ "task": "analyze_image@0.0.5", "id": "c3a242a3ad6b484aa7bae5adbd085307", "status": "COMPLETED" }]
        }
      ]
    },
    {
      "name": "编译构建",
      "status": "FAILED",
      "jobs": [
        {
          "id": "6ac13dd3243143f1b0aba852a420f631",
          "job_run_id": "6ac13dd3243143f1b0aba852a420f631",
          "name": "pre_comment",
          "status": "COMPLETED",
          "steps": [{ "task": "official_devcloud_cloudBuild", "id": "51731a54191b4871b7b55e904e9d5acd", "status": "COMPLETED" }]
        },
        {
          "id": "427f909686304e13be33f2ffb9fa07e1",
          "job_run_id": "427f909686304e13be33f2ffb9fa07e1",
          "name": "Compile_X86_compiler_ubuntu24",
          "status": "FAILED",
          "message": "步骤Compile_X86_compiler_ubuntu24执行失败，错误信息：{\"errorMessage\":\" 构建任务执行失败!\",\"errorCode\":\"DEV-CODECI-35002\"}",
          "steps": [{ "task": "official_devcloud_cloudBuild", "id": "3dc9227c7f484789ae57289f10928185", "status": "FAILED" }]
        },
        {
          "id": "8922fec2e32142cb92b89bd3c9e712a8",
          "job_run_id": "8922fec2e32142cb92b89bd3c9e712a8",
          "name": "Compile_X86_executor_ubuntu24",
          "status": "COMPLETED",
          "steps": [{ "task": "official_devcloud_cloudBuild", "id": "8a92aec09d794634ae3c19e967577c5d", "status": "COMPLETED" }]
        },
        {
          "id": "ed6c8c8dd9e14785a70e6cd3e1a88231",
          "job_run_id": "ed6c8c8dd9e14785a70e6cd3e1a88231",
          "name": "Compile_ARM_compiler_ubuntu24",
          "status": "FAILED",
          "message": "步骤Compile_ARM_compiler_ubuntu24执行失败，错误信息：{\"errorMessage\":\" 构建任务执行失败!\",\"errorCode\":\"DEV-CODECI-35002\"}",
          "steps": [{ "task": "official_devcloud_cloudBuild", "id": "97cfabbc7313490c8452fbf6d0c9cd5d", "status": "FAILED" }]
        },
        {
          "id": "d726dd8d10c34c648947d07333baecd3",
          "job_run_id": "d726dd8d10c34c648947d07333baecd3",
          "name": "Compile_ARM_compiler",
          "status": "FAILED",
          "message": "步骤Compile_ARM_compiler执行失败，错误信息：{\"errorMessage\":\" 构建任务执行失败!\",\"errorCode\":\"DEV-CODECI-35002\"}",
          "steps": [{ "task": "official_devcloud_cloudBuild", "id": "32b7ac17bc7e4ce6972bfaa33a8e6512", "status": "FAILED" }]
        }
      ]
    },
    {
      "name": "PreSmoke",
      "status": "INIT",
      "jobs": [
        {
          "id": "4267cccba16b43489e693085f7194daf",
          "job_run_id": "4267cccba16b43489e693085f7194daf",
          "name": "API_Check",
          "status": "INIT",
          "steps": [{ "task": "official_devcloud_cloudBuild", "id": "1fbb66804eb843878d8ee0d945263772", "status": "INIT" }]
        }
      ]
    },
    {
      "name": "后处理阶段",
      "status": "COMPLETED",
      "jobs": [
        {
          "id": "5fbe3151416c47dd826f485f8cf8e256",
          "job_run_id": "5fbe3151416c47dd826f485f8cf8e256",
          "name": "last_comment",
          "status": "COMPLETED",
          "steps": [{ "task": "official_devcloud_cloudBuild", "id": "734d8b02570e49d5875a59af91db0f53", "status": "COMPLETED" }]
        }
      ]
    }
  ]
}
```

- **需要获取的字段**: 同步骤2，遍历 `stages[].jobs[]`，获取 `name`、`status`、`message`、`steps[0].task`、`steps[0].id`。

**关键获取信息**：
- 失败阶段：`编译构建` (status: FAILED)
- 失败任务（需查询日志）：
    - `Compile_X86_compiler_ubuntu24` - job_run_id: `427f909686304e13be33f2ffb9fa07e1`, step_id: `3dc9227c7f484789ae57289f10928185`
    - `Compile_ARM_compiler_ubuntu24` - job_run_id: `ed6c8c8dd9e14785a70e6cd3e1a88231`, step_id: `97cfabbc7313490c8452fbf6d0c9cd5d`
    - `Compile_ARM_compiler` - job_run_id: `d726dd8d10c34c648947d07333baecd3`, step_id: `32b7ac17bc7e4ce6972bfaa33a8e6512`
- 注意：`PreSmoke` 阶段为 INIT（未执行），因为编译构建阶段失败导致后续阶段跳过

### 步骤4: 查询日志
```shell
curl --location --request POST 'https://api.gitcode.com/api/v5/repos/{owner}/{repo}/pipelines/{pipeline_id}/pipeline-runs/{pipeline_run_id}/jobs/{job_id}/logs?access_token=$GITCODE_API_TOKEN' \
--header 'Content-Type: application/json' \
--data-raw '{
    "pipeline_detail": "{pipeline_detail}",
    "start_offset": "0",
    "end_offset": "0",
    "limit": 1000,
    "sort": "desc"
}'
```

**路径参数**:
| 参数 | 来源 |
|------|------|
| `{pipeline_id}` | 当前流水线详情的 `pipeline_id`（步骤2或步骤3b） |
| `{pipeline_run_id}` | 当前流水线详情的 `id`（步骤2或步骤3b） |
| `{job_id}` | 目标 job 的 `jobs[].id` |

**Body 参数**:
| 参数 | 说明 |
|------|------|
| `pipeline_detail` | 步骤1的 `pipeline_detail` |
| `start_offset` | 分页起始偏移，首次传 `"0"`，后续用响应返回的 `start_offset` |
| `end_offset` | 分页结束偏移，首次传 `"0"`，后续用响应返回的 `end_offset` |
| `limit` | 每页条数，如 `"5000"` |
| `sort` | 排序方式：`"asc"` 正序 / `"desc"` 倒序，这里默认使用倒序 |

**响应字段**:
| 字段 | 说明 |
|------|------|
| `log` | 日志内容（字符串） |
| `has_more` | 是否有更多日志（`true`/`false`） |
| `start_offset` / `end_offset` | 用于下次请求的偏移量 |

**翻页逻辑**:
- 首次请求：`start_offset="0", end_offset="0"` → API 自动确定窗口
- 后续请求：用响应返回的 `start_offset` 和 `end_offset` 作为下次请求参数
- 当 `has_more=false` 时停止翻页

---