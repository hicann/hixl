# CANN C++ ABI 兼容性编码规范

>  **适用场景**：适用于`include/`目录下全部公开头文件（`hixl`、`adxl`、`cs`、`llm_datadist`）的接口设计，以及`libcann_hixl.so`、`libllm_datadist.so`导出符号的演进管理。当PR涉及`include/`下文件或公开接口实现变更时，必须执行本规范。

## 规范列表

| 规范编号 | 规范名称 | 类别 | 严重级别 |
|---------|---------|------|---------|
| 1.1 | 理解ABI变更的构成 | 总体原则 | —— |
| 1.2 | ABI变更必须显式评审 | 总体原则 | 高 |
| 2.1 | 公开头文件禁止std::string/std::list | STL类型准入 | 高 |
| 2.2 | 禁止含双ABI类型的模板实例化 | STL类型准入 | 高 |
| 2.3 | 字符串参数使用AscendString或C风格 | STL类型准入 | 中 |
| 2.4 | 公开头文件保持自包含 | STL类型准入 | 中 |
| 3.1 | 新增字段必须追加在结构体末尾 | 数据布局 | 高 |
| 3.2 | 新增字段优先复用reserved空间 | 数据布局 | 高 |
| 3.3 | 新增对外结构体必须预留reserved字段 | 数据布局 | 中 |
| 3.4 | 禁止修改已有字段类型和顺序 | 数据布局 | 高 |
| 3.5 | 枚举值显式赋值且仅允许末尾追加 | 数据布局 | 高 |
| 4.1 | 公开符号只增不改 | 符号演进 | 高 |
| 4.2 | 公开constexpr常量值禁止变更 | 符号演进 | 高 |
| 4.3 | 默认参数值变更视同签名变更 | 符号演进 | 高 |
| 5.1 | 公开类新增虚函数只能加在末尾 | 类布局 | 高 |
| 5.2 | 公开类优先使用pimpl惯用法 | 类布局 | 中 |
| 6.1 | _GLIBCXX_USE_CXX11_ABI设置保持统一 | 构建约定 | 中 |

## 说明

本规范基于GCC libstdc++双ABI行为（`_GLIBCXX_USE_CXX11_ABI`）、CANN软件包构建设置以及HIXL公开接口现状整理，与[CANN C++ 安全编码规范](cpp-secure.md)中规则10.11、10.12互为补充：本文件是ABI兼容性主题的权威规则来源。如果对规则有异议，建议提交issue并说明理由，经CANN运营团队评审后可接纳并修改生效。

## 适用范围

HIXL相关开源仓的公开头文件设计、公开接口（C++类接口与C接口）演进、导出符号管理。

---

### 1. 总体原则

#### 规则 1.1 理解ABI变更的构成

API兼容是源码级兼容（老代码重新编译仍可用）；ABI兼容是二进制级兼容（老编译产物不重编，直接替换新版`.so`仍正确链接、正确运行）。以下任一变化都构成ABI变更，修改`include/`或公开接口实现前应逐项自查：

- 导出符号的删除、重命名（C++ mangled name编码完整签名，改签名即改符号）
- 对外结构体大小、字段偏移、对齐的变化
- 枚举取值的变化
- 公开`constexpr`常量值的变化（编译期烧入调用方）
- 类的vtable布局变化（虚函数增删、顺序调整）
- 函数默认参数值的变化（在调用方展开）

#### 规则 1.2 ABI变更必须显式评审

非必要不变更公开ABI。确需变更时，PR描述中必须显式说明ABI影响、兼容性结论与版本演进方案，经评审确认后才能合入。判断依据是规则1.1的构成清单，而非"是否改了头文件"——部分ABI变更（如导出符号增删）不触碰`include/`。

---

### 2. 公开头文件STL类型准入

#### 规则 2.1 公开头文件禁止std::string/std::list

GCC 5.1引入`_GLIBCXX_USE_CXX11_ABI`双ABI后，`std::string`与`std::list`的布局和符号名均随该宏取值变化：`std::string`从COW实现（对象8字节）变为SSO实现（32字节）；`std::list`节点头新增`size_t`计数成员（对象16字节变为24字节）；新ABI下二者的mangled name均携带`__cxx11`标签。调用方与库的ABI宏取值不一致时，轻则链接期断链，重则按不同布局读写同一对象造成静默内存踩踏，是公开接口最经典的ABI破坏来源。

[反例]：

```cpp
struct NotifyDesc {
  std::string name;         // 禁止：双ABI类型，布局随_GLIBCXX_USE_CXX11_ABI变化
  std::string notify_msg;
};
```

[正例]：

```cpp
struct NotifyDesc {
  AscendString name;        // 布局由metadef自控，不受libstdc++双ABI影响
  AscendString notify_msg;
};
```

#### 规则 2.2 禁止含双ABI类型的模板实例化

禁用类型的毒性会沿模板实例化链传导：`std::vector<std::string>`、`std::map<std::string, AscendString>`等模板参数含`std::string`/`std::list`的组合，其元素布局与整条实例化链的符号名同样随双ABI变化，等同直接引入禁用类型。

[反例]：

```cpp
Status Initialize(const std::map<std::string, std::string> &options);
```

[正例]：

```cpp
Status Initialize(const std::map<AscendString, AscendString> &options);
```

#### 规则 2.3 字符串参数使用AscendString或C风格

C++公开接口的字符串参数使用`ge::AscendString`；C公开接口使用`const char *`配合显式长度或`NUL`结束约定。`std::vector`、`std::map`、`std::pair`、`std::unique_ptr`（pimpl惯用法）不带`__cxx11`标签、布局稳定，允许出现在公开签名中，其含义是接口绑定libstdc++标准库家族。

#### 规则 2.4 公开头文件保持自包含

公开头文件只允许依赖C++标准库与指定的CANN头（`ge_common`、`acl`、`hcomm`），不得包含仓库内部头或`src/`下的实现头，保证下游可以独立包含公开头进行编译。

---

### 3. 数据布局

#### 规则 3.1 新增字段必须追加在结构体末尾

在已有字段之前或之间插入新字段会使后续所有字段偏移变化，破坏已按旧布局编译的调用方。新增字段只能追加在最后一个字段之后（实践中的末尾是`reserved`字段，见规则3.2）。

[反例]：

```cpp
struct MemDesc {
  uint32_t priority;        // 禁止：中部插入，addr/len/reserved偏移全部改变
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
};
```

[正例]：

```cpp
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};  // 新增字段只能在reserved区内扩展，见规则3.2
};
```

#### 规则 3.2 新增字段优先复用reserved空间

对外结构体的`reserved`字段是为兼容性扩展预留的空间，新增字段应置于`reserved`之前（即有效字段末尾），连同其引入的对齐填充一起从`reserved`划出空间，保持结构体总大小不变。总大小不变意味着调用方按旧大小分配的栈对象、数组、通信缓冲都不会越界。不建议依赖隐式对齐填充：优先选择不引入填充的字段类型与位置（如1字节字段紧邻其他1字节字段）；填充不可避免时应显式演算并在注释中说明。

[正例]：`HixlClientDesc`总大小保持128字节不变，新增`uint8_t`字段紧邻`sl`之后（位于偏移30），无需对齐填充，共占用1字节，`reserved`等量缩减：

```cpp
struct HixlClientDesc {
  const EndpointDesc *local_endpoint;
  const EndpointDesc *remote_endpoint;
  const char *server_ip;
  uint32_t server_port;
  uint8_t tc;
  uint8_t sl;
  uint8_t priority;          // 新增字段：1字节，紧邻sl之后位于偏移30，无对齐填充
  uint8_t reserved[97];      // 98 - 1(字段)，总大小仍为128字节
};
```

#### 规则 3.3 新增对外结构体必须预留reserved字段

新增对外结构体必须预留`reserved`字段，预留量建议64~128字节（跟随现有公开结构体的惯例），为未来不破坏布局的扩展保留空间。

#### 规则 3.4 禁止修改已有字段类型和顺序

修改已有字段的类型、顺序或删除字段都会改变结构体大小与字段偏移，属于ABI破坏。确需演进时按规则3.1、3.2新增字段并以新字段承载新语义，旧字段保持不变。

#### 规则 3.5 枚举值显式赋值且仅允许末尾追加

对外枚举必须显式赋值：隐式取值在插入或重排成员时会发生整体漂移。已有枚举值禁止修改（编译期烧入调用方，且无符号痕迹可查）；新增枚举值只能在末尾追加并显式赋值。优先使用带显式底层类型的`enum class`。

[反例]：

```cpp
enum FeatureType : int32_t {
  AUTO_CONNECT = 0,
  NEW_FEATURE = 1,           // 禁止：占用已有取值，CLIENT_SERVER_COMM被迫漂移
  CLIENT_SERVER_COMM = 2,
};
```

[正例]：

```cpp
enum FeatureType : int32_t {
  AUTO_CONNECT = 0,
  CLIENT_SERVER_COMM = 1,
  NEW_FEATURE = 2,           // 仅末尾追加，显式赋值
};
```

---

### 4. 符号与接口演进

#### 规则 4.1 公开符号只增不改

公开接口的实现符号禁止删除、重命名或修改签名（参数类型、顺序、个数）。C++ mangled name编码完整签名，实现侧重命名或改签名即使头文件同步修改，也会导致下游已编译产物链接失败。C公开接口（`HixlCS*`系列）的符号名本身就是ABI，不可变更。新能力以新增接口承载；旧接口废弃走保留加标记的deprecation流程，不直接移除。

#### 规则 4.2 公开constexpr常量值禁止变更

公开头文件中的`constexpr`常量（状态码、特性值等）在调用方编译期直接烧入指令，变更后新旧二进制对同一常量的判断不一致。此类变更不改变任何符号与布局，是ABI变更中最隐蔽的一类，必须等同接口签名变更对待。

[反例]：

```cpp
constexpr Status PARAM_INVALID = 103999U;  // 禁止：103900改为103999，已编译调用方仍持旧值
```

#### 规则 4.3 默认参数值变更视同签名变更

默认参数在调用点展开，修改默认参数值会改变所有省略该参数的已编译调用方的行为，等同修改函数签名。`timeout_in_millis = 1000`变更为其他取值必须按ABI变更评审。

---

### 5. 类布局

#### 规则 5.1 公开类新增虚函数只能加在末尾

虚函数在vtable中的槽位偏移是ABI的一部分，在中间插入虚函数会使后续虚函数槽位整体偏移，已编译的虚调用全部错位。公开类新增虚函数只能追加在现有虚函数之后。现有公开类（`hixl::Hixl`、`adxl::AdxlEngine`、`llm_datadist::LlmDataDist`）均为非虚接口，保持该设计可规避此类约束。

#### 规则 5.2 公开类优先使用pimpl惯用法

公开类仅持有`std::unique_ptr<Impl> impl_`成员（8字节、布局稳定），实现细节全部封装在`Impl`中：实现侧任意演进都不影响公开类大小与布局。析构函数必须在实现文件中定义（`Impl`完整类型要求）。现有公开类均采用该模式，新增公开类必须沿用。

---

### 6. 构建约定

#### 规则 6.1 _GLIBCXX_USE_CXX11_ABI设置保持统一

CANN生态预编译库按`_GLIBCXX_USE_CXX11_ABI=0`构建，HIXL主库经CANN软件包的构建设置注入相同取值，下游（examples、benchmarks、hixl_tool）已显式固定为0。任何构建侧对该宏取值的变更都可能引起`__cxx11`标签符号差异导致混链失败，必须按ABI变更评审。结合规则2.1，公开头文件不引入`std::string`/`std::list`时，该宏取值对公开接口的符号与布局无影响。
