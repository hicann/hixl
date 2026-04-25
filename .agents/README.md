# HIXL Agent Skills

## Skills 列表

| Skill                                          | 功能 | 触发场景 |
|------------------------------------------------|-------|---------|
| [hixl-troubleshoot](skills/hixl-troubleshoot/) | HIXL/ADXL 运行时问题定位 | 用户明确要求诊断 HIXL，或日志中出现 HIXL、ADXL、Ascend direct transport 相关报错或调用栈 |
| [hixl-pr-review](skills/hixl-pr-review)        | HIXL代码检视/审查       | 用户明确要求检视PR或审查PR |
| [hixl-ut-generator](skills/hixl-ut-generator/) | HIXL UT 生成        | 用户要求基于当前改动生成测试、补充 UT、或根据 git diff 编写测试用例 |

## SKILL 命名规范

- `SKILL.md` 文件必须严格命名为 `SKILL.md`（区分大小写）
- Skill 文件夹使用 **kebab-case** 命名法，例如 `hixl-troubleshoot`
- Skill 的补充资料放在 `references/` 、 `scripts/` 或 `assets/` 目录下，避免将说明散落在多个入口文件中

## 使用方式

### 安装

将 `.agents/skills/` 目录放置到项目根目录下。支持 [Agent Skills 规范](https://agentskills.io/specification) 的工具（如 OpenCode 等）会自动扫描并加载这些 Skill。

### 触发方式

Skill 有两种触发方式：

1. **场景匹配**：当用户的任务描述命中 Skill 的触发场景时，Agent 自动识别并调用对应 Skill。例如用户说“帮我分析这段 HIXL 建链失败日志”或“帮我检视一下这个PR：<PR链接>”，Agent 会匹配到对应的 Skill。
2. **指定调用**：用户直接指定使用某个 Skill，例如 `/hixl-troubleshoot 分析一下建链失败日志`、`/hixl-pr-review 检视一下PR：<PR链接>`。

`SKILL.md` frontmatter 中的 `description` 定义了主要触发边界，也可参考 [Skills 列表](#skills-列表) 中的"触发场景"列。

## 免责声明

1. 本目录中的 Agent Skills 内容仅供技术参考和学习使用，不代表其适用于任何生产环境或关键业务系统。
2. 开发者在使用时应自行评估其安全性、兼容性和适用性。作者及贡献者不对因使用本内容导致的任何直接或间接损失承担责任。
3. 部分诊断步骤可能依赖系统命令、设备环境或外部仓库信息，相关权限、网络与合规性需由开发者自行核实。
4. 除非另有明确约定，本目录所有内容均基于仓库当前许可证发布，不提供任何形式的技术支持或担保。
