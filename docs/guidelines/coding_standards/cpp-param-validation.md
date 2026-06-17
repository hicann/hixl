# 不可信入参参数校验规范

> **适用场景**：当函数入参来自不可信模块（如 device kernel 入口、`extern "C"` 导出函数、Python 绑定、跨进程 RPC 回调等）时，必须在参数被解引用或使用前完成校验，防止非法输入导致崩溃、任意地址读写或资源耗尽。

## 核心原则

**报告会导致非法解引用、非法内存访问、未定义行为或 OOM/DoS 的问题。** 以下情况不算安全问题，不应报告：

- 参数只是透传给下层函数，本层未解引用
- 下层函数在使用前已有校验（不要求在信任边界入口层重复校验）
- 调用方按 API 约定保证的指针+长度组合（如 C API 的 `desc_list` + `list_num`）

## 信任边界识别

以下函数签名模式视为信任边界入口：

| 入口类型 | 识别特征 | 示例 |
|---------|---------|------|
| Device kernel 入口 | `extern "C"` + kernel 函数名 | `HixlBatchPut(HixlOneSideOpParam *param)` |
| 公开 C API | `extern "C"` 或 `include/` 下头文件声明 | `HixlCSServerCreate(...)` |
| Python 绑定 | `pybind11` module def | `m.def("batch_put", ...)` |
| 跨进程回调 | RPC/消息处理函数 | `OnMessage(...)` |

## 校验检查清单

对每个信任边界入口函数，检查参数在**被解引用或使用前**是否完成校验。校验可以不在入口层，但必须在首次解引用前完成。

| 编号 | 检查项 | 校验要求 | 违规后果 |
|------|--------|---------|---------|
| P1 | 指针解引用前判空 | 指针在解引用（`*p`、`p->`、`std::string(p)`、`strlen(p)` 等）前必须判空 | 空指针解引用导致崩溃 |
| P2 | 地址字段解引用前非零 | 结构体中以整数存储的地址字段（如 `xxx_addr`），在解引用前必须校验非 0（`reinterpret_cast` 本身不解引用，转换后判空也可以） | 零地址解引用导致崩溃或非法访问 |
| P3 | 数量/长度范围 | 表示数量、长度、大小的整数字段必须校验 `> 0` 且 `<= 上限`；仅用于循环遍历且调用方保证数组长度时不需要 | 0 值导致空循环或 OOM；极大值导致整数溢出或 OOM |
| P4 | 校验失败阻断执行 | 校验失败必须立即返回错误码，不能继续执行后续逻辑 | 非法参数透传到底层触发更严重问题 |
| P5 | 校验前不解引用参数 | 校验完成前不得对参数做任何解引用 | 校验前的非法访问已经造成崩溃 |
| P6 | 避免未定义行为 | 对空容器取地址（`&vec[0]`）、对空指针取引用等 C++ 标准未定义行为必须避免 | 未定义行为可能导致崩溃或不可预测结果 |

## 非问题（排除项）

以下情况**不应报告**为安全问题：

| 排除项 | 说明 | 示例 |
|--------|------|------|
| 透传不解引用 | 参数只是传递给下层函数，本层未解引用 | `mem_tag` 透传给 `HcommMemReg`，HIXL 代码中未解引用 |
| 下层已有校验 | 下层函数在首次使用前已做校验，不要求在入口层重复 | `endpoint_list` 在 C API 层未判空，但 `Initialize` 内部已 `CHECK_NOTNULL` |
| 调用方保证的指针+长度 | C API 调用约定中，调用方保证指针指向至少 N 个元素 | `desc_list` + `list_num`：调用方保证 `desc_list` 至少有 `list_num` 个元素 |
| 整数溢出但无非法访问 | 整数运算溢出但结果不会被用于解引用、内存访问或资源分配 | 计数器溢出但只用于日志打印 |

## 检查方法

### 1. 定位信任边界入口

在变更文件中搜索以下模式：

```
extern "C"
PYBIND11_MODULE
m\.def\(
```

以及 `include/` 目录下头文件中声明的公开函数。

### 2. 逐函数检查校验完整性

对每个入口函数：

1. 列出所有入参字段（含结构体成员）
2. 对每个字段，追踪到**首次解引用或使用**的位置
3. 检查在首次解引用前是否完成校验（可以在本层或下层）
4. 对照排除项，确认不是误报
5. 检查校验失败路径是否立即返回错误码

## 正确示例

```cpp
uint32_t HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  // P1: 指针判空（param 即将被解引用）
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] param is nullptr");
    return PARAM_INVALID;
  }
  // P3: list_num 需要校验上限，因为后续会用 list_num 构造 vector 并访问 op_desc_list_addr 指向的数组
  constexpr uint32_t kMaxBatchSize = 8192;
  if (param->list_num == 0 || param->list_num > kMaxBatchSize) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] invalid list_num=%u, valid range is [1, %u]",
              param->list_num, kMaxBatchSize);
    return PARAM_INVALID;
  }
  // P2: 地址字段非零（即将 reinterpret_cast 并访问）
  if (param->op_desc_list_addr == 0) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] op_desc_list_addr is null");
    return PARAM_INVALID;
  }
  // P5: 校验完成后才开始使用参数
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(
      static_cast<uintptr_t>(param->op_desc_list_addr));
  ...
}
```

## 错误示例

```cpp
// 错误：server_ip 未判空，std::string 构造函数内部调用 strlen 解引用 nullptr
HixlCSServer(const char *ip, uint32_t port) : ip_(ip), port_(port) {};
// 调用处：
auto server = new HixlCSServer(server_desc->server_ip, server_desc->server_port);
// server_ip 为 nullptr 时，std::string(nullptr) 崩溃
```

## 非问题示例（不应报告）

```cpp
// 不是问题：mem_tag 只是透传，本层未解引用
HIXL_CHK_STATUS_RET(server->RegisterMem(mem_tag, mem, mem_handle), ...);

// 不是问题：endpoint_list 在 Initialize 内部已判空
HIXL_CHK_STATUS_RET(server->Initialize(server_desc->endpoint_list, ...), ...);
// Initialize 内部：HIXL_CHECK_NOTNULL(endpoint_list);

// 不是问题：调用方保证 desc_list 至少有 list_num 个元素
for (uint32_t i = 0; i < list_num; i++) {
    void *dst = desc_list[i].local_buf;  // 调用方保证，不是库的责任
}
```
