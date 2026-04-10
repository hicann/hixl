# 贡献指南

本项目欢迎广大开发者体验并参与贡献，在参与社区贡献之前。请参见[cann-community](https://gitcode.com/cann/community)了解行为准则，进行CLA协议签署，了解源码仓的贡献流程，该仓详细介绍了如何参与CANN开源项目的贡献的前置条件，包括但不限于：

1. 如何提交PR
2. gitcode工作流
3. 流水线触发命令
4. 代码检视
5. 其他注意事项
   详情可以参考[cann-community](https://gitcode.com/cann/community)。

除此之外，开发者准备本地代码与提交PR时需要重点关注如下几点：

1. 提交PR时，请按照[PR模板](.gitcode/PULL_REQUEST_TEMPLATE.zh-CN.md)仔细填写本次PR的业务背景、目的、方案等信息。
2. 使用git进行代码提交前，可以参考[pre-commit工具使用说明](docs/precommit_guide.md)来使您的代码提交更合规高效。
3. 若您的修改不是简单的bug修复，而是涉及到新增特性、新增接口、新增配置参数或者修改代码流程等，请务必先通过Issue进行方案讨论，以避免您的代码被拒绝合入。若您不确定本次修改是否可被归为“简单的bug修复”，亦可通过提交Issue进行方案讨论。
4. 提交pr时，请确保您的代码符合项目的代码规范，具体参考google的[开源代码规范](https://google.github.io/styleguide/)，包括但不限于：
  - 代码格式化
  - 注释规范
  - 变量命名规范
  - 函数命名规范
  - 类命名规范
  - 接口命名规范
  - 配置参数命名规范
  - 代码流程规范
5. 提交pr时，如果存在多个无效commit，建议您在提交pr前先进行rebase操作，合并多个commit为一个，以保持代码的简洁性和可读性，具体参考[git rebase](https://git-scm.com/docs/git-rebase)，同时，commit message也需要符合项目的代码规范，能够清晰地描述本次变更的意图和内容，格式为：<类型>: <简短描述>。 例如:

| 类型 | 说明                    | 示例                     |
|------|-----------------------|------------------------|
| feat     | 新功能                   | [feat]：添加用户注册功能        |
| bugfix   | 修复 bug                | [bugfix]: 修复登录态过期问题    |
| docs     | 文档更新                  | [docs]: 更新 API 使用说明    |
| style    | 代码格式调整（不影响逻辑）   | [style]: 调整代码缩进        |
| refactor | 重构（非功能新增/修复）      | [refactor]: 优化用户服务类结构  |
| perf     | 性能优化                  | [perf]: 减少数据库查询次数      |
| test     | 测试相关                  | [test]: 添加登录功能单元测试     |
| chore    | 构建/工具链变更             | [chore]: 更新 webpack 配置 |
| ci       | CI 配置相关               | [ci]: 添加自动化测试流程        |

开发者贡献场景主要包括：

- Bug修复

  如果您在本项目中发现了某些Bug，希望对其进行修复，欢迎您新建Issue进行反馈和跟踪处理。

  您可以按照[提交Issue/处理Issue任务](https://gitcode.com/cann/community#提交Issue处理Issue任务)指引新建 `Bug-Report|缺陷反馈` 类Issue对Bug进行描述，然后在评论框中输入“/assign”或“/assign @yourself”，将该Issue分配给您进行处理。

- 贡献新功能

  如果您在本项目中发现了某些功能缺失，希望对其进行新增，欢迎您新建Issue进行反馈和跟踪处理。

  您可以按照[提交Issue/处理Issue任务](https://gitcode.com/cann/community#提交Issue处理Issue任务)指引新建 `Requirement|需求建议` 类Issue对新增功能进行说明，并提供您的设计方案，
  然后在评论框中输入“/assign”或“/assign @yourself”，将该Issue分配给您进行跟踪实现。

- 文档纠错

  如果您在本项目中发现某些文档描述错误，欢迎您新建Issue进行反馈和修复。

  您可以按照[提交Issue/处理Issue任务](https://gitcode.com/cann/community#提交Issue处理Issue任务)指引新建 `Documentation|文档反馈` 类Issue指出对应文档的问题，然后在评论框中输入“/assign”或“/assign @yourself”，将该Issue分配给您纠正对应文档描述。

- 帮助解决他人Issue

  如果社区中他人遇到的问题您有合适的解决方法，欢迎您在Issue中发表评论交流，帮助他人解决问题和痛点，共同优化易用性。

  如果对应Issue需要进行代码修改，您可以在Issue评论框中输入“/assign”或“/assign @yourself”，将该Issue分配给您，跟踪协助解决问题。
