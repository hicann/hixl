# AGENTS.md

本文件为 Agent 在本仓库中工作提供指引，重点说明仓库导航、常用命令和提交前约束。

## 项目概述

HIXL（Huawei Xfer Library）是面向昇腾芯片的单边通信库，用于分布式 AI 场景（LLM 推理 KV Cache 传输、PD 分离等）。支持通过 HCCS 和 RDMA 协议进行点对点 D2D/D2H/H2D 数据传输，并通过 pybind11 提供 Python 绑定。

两个核心组件：
- **HIXL Engine**（`src/hixl/`）：底层传输引擎，支持多种内存类型和传输协议。
- **LLM-DataDist**（`src/llm_datadist/`）：基于 HIXL 构建的上层 KV Cache 传输接口，对接 vLLM/SGLang。

## 关键目录

| 目录 | 用途 |
|------|------|
| `src/hixl/` | HIXL Engine 主要实现 |
| `src/llm_datadist/` | LLM-DataDist 主要实现 |
| `src/python/` | Python 绑定实现 |
| `include/` | 公开头文件和对外接口边界 |
| `tests/cpp/` | C++ 测试 |
| `tests/python/` | Python 测试 |
| `examples/` | 端到端样例 |
| `benchmarks/` | 性能用例与基准测试 |
| `docs/` | 构建、接口和其他说明文档 |

## 构建与测试命令

前置条件：已安装 Ascend CANN toolkit，且已执行 `source /usr/local/Ascend/cann/set_env.sh`。如果环境未满足，Agent 需要先说明阻塞原因，不要虚报构建或测试结果。

```bash
# 构建
bash build.sh                              # Release 构建，输出到 build_out/
bash build.sh --build-type=Debug --asan    # Debug 构建并开启 AddressSanitizer
bash build.sh --examples                   # 同时编译示例和 benchmark

# 测试
bash tests/run_test.sh                     # 执行全部 C++ 和 Python 测试
bash tests/run_test.sh -t cpp              # 仅执行 C++ 测试
bash tests/run_test.sh -t py               # 仅执行 Python 测试
bash tests/run_test.sh --cov               # 按覆盖率模式构建并运行测试

# 代码检查
pip3 install -r requirements.txt           # 安装 Python 依赖
pre-commit install                         # 安装 Git hooks
pre-commit run --files <changed-files>     # 只检查受影响文件
```

- 始终使用 `bash build.sh` 和 `bash tests/run_test.sh`，不要手写零散 CMake 命令。
- 代码改动至少运行受影响范围测试；跨模块变更优先运行对应类型的完整测试。
- 文档类改动至少检查引用路径、命令和文件名是否与仓库现状一致。
- 如果某项验证因环境、依赖或硬件条件无法执行，必须在结论中明确说明未验证项。

## 代码风格与测试要求

- **C++**：遵循 `.clang-format`（基于 Google 风格），2 空格缩进、120 列限制、附着式大括号、`SortIncludes: false`。类型用 `PascalCase`，函数用 `camelCase`。
- **Python**：使用 `ruff-check` 和 `ruff-format`，模块和测试文件用 `snake_case`。
- 测试文件命名：C++ 以 `_unittest.cc` 或 `_ut.cc` 结尾，Python 以 `test_*.py` 命名。
- 行为变更必须补齐或更新测试。
- C++ 用例按模块放置，例如 `tests/cpp/hixl/engine/`、`tests/cpp/llm_datadist/`。
- Python 用例放在 `tests/python/test_*.py`。

## 提交前检查与提交规范

- 建议先阅读 `docs/precommit_guide.md`，再执行 `pre-commit install` 和 `pre-commit run --files <changed-files>`。
- OAT（开源审计工具）会在 pre-commit 中检查二进制文件和许可证头；新增源码文件需要带正确的许可证头。
- 禁止提交 `build_out/`、`build_test/`、手动下载的第三方压缩包或其他二进制产物。
- 如果 pre-commit 或 OAT 因环境问题未执行成功，Agent 需要在最终说明中写明原因和影响范围。
- commit message 使用类型前缀：`[feat]`、`[bugfix]`、`[refactor]`、`[docs]`，搭配祈使句摘要。
- 示例：`[bugfix]: fix channel cleanup race`

## 关键约束

- 构建依赖 Ascend CANN >= 9.0.0，运行依赖 >= 8.5。
- 未 source CANN 环境变量将无法构建。
- 默认遵循仓库现有目录结构和工具链，不随意引入新的构建入口或测试入口。
