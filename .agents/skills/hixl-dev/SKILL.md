---
name: hixl-dev
description: >-
  HIXL 本地开发闭环：改码、按范围跑 UT、增量覆盖率估算、PR 前检视；开 PR 后配合既有
  gitcode-pipeline 盯 CI，并基于 OpenLiBing 修复低级错误。修改 HIXL 源码/测试/构建脚本、
  本地验证、/hixl-dev、CI 失败修复时使用。创建 PR 用 gitcode-pr，流水线轮询用 gitcode-pipeline。
license: CANN Open Software License Agreement Version 2.0
---

# HIXL 本地开发与验证

本 skill 覆盖「改码 → 本地验证 →（可选）开 PR 后跟进 CI」。不改写既有 skill：

- 创建/更新 PR、评论 API → [`gitcode-pr`](../gitcode-pr/SKILL.md)
- 触发/轮询/重试流水线 → [`gitcode-pipeline`](../gitcode-pipeline/SKILL.md)

编码与安全规范以 [`AGENTS.md`](../../../AGENTS.md) 与
[`docs/zh/contributions/coding_standards/`](../../../docs/zh/contributions/coding_standards/)
为准。

## 开发原则

### 先思考再编码

**不臆测，不掩盖不确定性，显式说明取舍。**

- 先说明已确认的事实、假设和待确认项；信息不足时先问，不擅自选择一种解释；
- 有多个方案时说明影响、风险和较简单的替代方案；
- 接口语义、目标分支、并发行为或错误路径不明确时，先定位依据再修改。

### 简单优先

**用满足当前需求的最小改动解决问题，不做投机性设计。**

- 不实现需求之外的功能、配置项或一次性抽象；
- 不为理论上不可能发生的场景堆砌处理逻辑；
- 若方案明显比已有模式复杂，先确认复杂性确有必要。

### 精准修改

**只修改与目标直接相关的内容，只清理本次改动引入的问题。**

- 保持相邻代码、注释和格式不因顺手而变化；
- 遵循当前模块的既有风格；发现无关缺陷只记录，不随本次改动处理；
- 移除由本次改动产生的无用引用、变量或代码，不清理存量死代码。

### 以验证为目标推进

**将任务拆成可检查的结果；验证失败则修复后回到相应检查。**

- 新增校验或错误路径时，补能证明该行为的测试；
- 修复缺陷时，优先复现问题，再验证修复；
- 多步骤任务先列出“步骤 → 验证方式”，完成前不把推测当作结论。

## 完成标准

- 改动范围、目标分支和验证范围已确认；
- 行为变更有对应测试，受影响测试通过；
- 修改生产 C++ 时已给出增量覆盖率结论；
- 已检查 diff、命令、路径和新增文件许可证头；
- 未执行的验证项及原因如实说明。

仅改文档、Agent Skill 或配置时，不要求运行无关的 C++ UT；应验证链接、命令、
文件路径及示例与仓库现状一致。

## 1. 先确定比较范围

默认以 PR 的目标分支 `upstream/master` 为基准。目标分支不同、`upstream` 不存在或
分支尚未确定时，先确认目标，不要静默改用 `origin/master`。

```bash
BASE_REF="${HIXL_BASE_REF:-upstream/master}"
BASE_REMOTE="${BASE_REF%%/*}"
BASE_BRANCH="${BASE_REF#*/}"
test "$BASE_BRANCH" != "$BASE_REF" || {
  echo "请使用 <remote>/<目标分支> 形式设置 HIXL_BASE_REF" >&2
  exit 1
}
git remote get-url "$BASE_REMOTE"
git fetch "$BASE_REMOTE" "$BASE_BRANCH"
BASE_COMMIT="$(git merge-base HEAD "$BASE_REF")"

# 包含已提交、暂存和未暂存的跟踪文件改动
git diff --name-status "$BASE_COMMIT"
git diff --check "$BASE_COMMIT"
```

阅读 diff 后，将改动分为：生产代码、测试、Python、构建/脚本、文档或配置。不要因为
相邻代码可顺手整理而扩大改动范围；如果发现无关问题，只记录，不随本次改动处理。

## 2. 设计与实现

- 行为、错误路径、并发或资源生命周期发生变化时，先定位已有测试和 mock/stub，再补最小
  必要用例；
- 新增源文件按仓库要求添加许可证头，并接入对应构建列表；
- 优先复用 `tests/depends/` 的桩件，不依赖真实 NPU、多机或多卡；
- 实现与测试遵循仓库现有目录、命名和格式，不为一次性需求增加抽象层。

不确定接口、预期行为或目标分支时，明确说明缺失信息并询问；不要把推测写成事实。

## 3. 选择并运行本地验证

所有构建与测试均使用仓库入口 `bash build.sh` 和 `bash tests/run_test.sh`，不要手写
零散 CMake 命令。先运行最小受影响范围；跨模块、公共接口或 PR 收尾时扩大范围。

| 改动区域 | 首选验证 |
| --- | --- |
| `src/llm_datadist/` 或对应测试 | `bash tests/run_test.sh -t cpp -s llm_datadist` |
| `src/hixl/` 或公共 C++ 接口 | `bash tests/run_test.sh -t cpp -s hixl` |
| fabric memory 相关改动 | `bash tests/run_test.sh -t cpp -s fabric_mem` |
| ADXL / channel pool 相关改动 | 对应 `-s adxl` 或 `-s channel_pool` |
| 多个 C++ 模块、共享构建逻辑 | `bash tests/run_test.sh -t cpp` |
| `src/python/` 或 Python 测试 | `bash tests/run_test.sh -t py` |

失败时先保留失败命令和日志，定位后复跑受影响 suite。不得把偶现或环境问题直接归因于
本次代码；无法复现时记录证据和风险。

### macOS（Lima）

macOS 上在 Lima 实例中构建和测试，不直接使用宿主机工具链。仓库路径由当前 worktree
动态取得，避免写死个人目录。

```bash
REPO_ROOT="$(git rev-parse --show-toplevel)"
limactl shell hixl -- env REPO_ROOT="$REPO_ROOT" bash -lc '
  CANN_ROOT="${ASCEND_HOME_PATH:-/usr/local/Ascend/cann}"
  if [ -f "${CANN_ROOT}/set_env.sh" ]; then
    source "${CANN_ROOT}/set_env.sh"
  elif [ -f "${CANN_ROOT}/bin/setenv.bash" ]; then
    source "${CANN_ROOT}/bin/setenv.bash"
  else
    echo "找不到 CANN 环境脚本：${CANN_ROOT}" >&2
    exit 1
  fi
  cd "$REPO_ROOT"
  bash tests/run_test.sh -t cpp
'
```

若 Lima 实例、挂载路径或 CANN 环境不可用，说明阻塞项，不要声称测试已经执行。

### Linux

```bash
CANN_ROOT="${ASCEND_HOME_PATH:-/usr/local/Ascend/cann}"
if [ -f "${CANN_ROOT}/set_env.sh" ]; then
  source "${CANN_ROOT}/set_env.sh"
elif [ -f "${CANN_ROOT}/bin/setenv.bash" ]; then
  source "${CANN_ROOT}/bin/setenv.bash"
else
  echo "找不到 CANN 环境脚本：${CANN_ROOT}" >&2
  exit 1
fi
bash tests/run_test.sh -t cpp
```

## 4. C++ 增量覆盖率

仅在修改生产 C++ 时评估。`hixl-dev` 的本地目标为 **增量覆盖率至少 85%**；仓库分支或
CI 有更严格规则时，以其为准。测试未通过前，先修测试，不做覆盖率结论。

### 默认：逐 hunk 估算

```bash
# BASE_COMMIT 取自“先确定比较范围”
git diff --name-only "$BASE_COMMIT" -- \
  ':(glob)src/**/*.cc' ':(glob)src/**/*.cpp' ':(glob)src/**/*.cxx'
git diff "$BASE_COMMIT" -- src/
```

对每个生产文件的新增或修改逻辑逐 hunk 映射已有或新增 UT：

- **已覆盖**：用例会直接执行该路径；
- **部分覆盖**：只覆盖正常路径或部分分支，按 0.5 计；
- **未覆盖**：没有可到达的用例。

估算值为 `Σ(已覆盖行 + 部分覆盖行 × 0.5) / Σ计入的新增可执行行`。注释、纯格式、
仅删除内容和纯声明通常不计入；有条件、循环、错误处理或资源清理的新增逻辑必须逐项说明。

| 估算结果 | 后续动作 |
| --- | --- |
| ≥85% | 记录依据，进入 diff 检查或 PR 准备 |
| 80%–85% | 补充最小 targeted UT，或执行精算确认 |
| <80% | 补测或简化冗余逻辑后重新验证 |

### 存疑、临界或 CI 覆盖率失败：精算

覆盖率构建较慢，不作为默认步骤。仅在手工估算存疑、接近门槛或 CI 已报告覆盖率问题时运行：

```bash
# 确认环境已具备 lcov、gcov、genhtml；缺少 diff-cover 时先征得同意再安装
bash tests/run_test.sh --cov -t cpp
diff-cover cov/coverage.info --compare-branch="$BASE_COMMIT"
```

根据 `diff-cover` 输出的未覆盖行，先定位具体分支再补测试；不要盲目增加用例数量。

覆盖率结论必须包含基准、生产文件、方法（估算或实测）、结果和最小补测建议。例如：

```markdown
## 增量覆盖率
Base: <BASE_COMMIT>
生产 C++ 文件: <文件列表>
方法: 手工逐 hunk 估算 | diff-cover
结果: <百分比或未覆盖行>
结论: <达标 / 需补测及原因>
```

## 5. PR 前检查

1. 复查 `git diff --check "$BASE_COMMIT"` 与 `git status`；
2. 对受影响文件执行适用的 `pre-commit run --files ...`（环境可用时）；
3. 确认测试结果、覆盖率结论和未验证风险均与实际一致；
4. 代码改动准备 PR 时，按 [`hixl-pr-review`](../hixl-pr-review/SKILL.md) 的适用范围进行检视；
   远程评论或 `/lgtm` 属于外部写操作，须在用户要求后执行；
5. 通过后用 [`gitcode-pr`](../gitcode-pr/SKILL.md) 创建或更新 PR（不修改该 skill）。

## 6. CI 跟进与低级错误修复

用户要求开 PR、盯 CI，或已有 PR 需要跟进时：

1. 用 [`gitcode-pr`](../gitcode-pr/SKILL.md) 创建/更新 PR（若尚未创建）；
2. 用 [`gitcode-pipeline`](../gitcode-pipeline/SKILL.md) 触发（通常评论 `compile`）、轮询
   （`gp-list` / `gp-wait` 等）并判断 `ci-pipeline-passed`；
3. 失败时按 [openlibing.md](references/openlibing.md)，用 Agent 的浏览器能力打开 OpenLiBing，
   读取规则/日志（不要 curl SPA 页面）；
4. 仅修复能本地复现、规则明确的低级错误 → 回到 §3–4 验证 → 更新 PR 分支 → 再触发/轮询。

```text
本地验证通过 → gitcode-pr 开/更 PR → gitcode-pipeline 触发并轮询
      ├─ ci-pipeline-passed → 报告通过
      └─ 失败 → 浏览器读 OpenLiBing → 修低级错误 → §3–4
               → 更新分支 → 再走 gitcode-pipeline
```

根因不清、无浏览器/权限，或基础设施问题时报告证据后停止，不反复空跑 CI。
不新增与 `gitcode-pipeline` 重复的轮询脚本。

## 最终回复

简要说明：

- 改动及影响范围；
- 执行的命令与通过/失败结果；
- 增量覆盖率方法和结论（如适用）；
- 已完成的检视；若跟进过 PR：链接、CI 终态、修复次数；
- 未执行项、原因和剩余风险。
