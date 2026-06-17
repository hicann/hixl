# CANN C++ 安全编码规范

> **适用场景**：安全编码红线规范，所有 C++ 代码必须 100% 遵守。

## 规范列表

| 规范编号 | 规范名称 | 类别 | 严重级别 |
|---------|---------|------|---------|
| 1.1 | 保证静态类型安全 | 总体原则 | 高 |
| 1.2 | 保证内存安全 | 总体原则 | 高 |
| 1.3 | 禁止使用未定义行为 | 总体原则 | 高 |
| 2.1 | 有符号整数运算不溢出 | 数值安全 | 高 |
| 2.2 | 无符号整数运算不回绕 | 数值安全 | 高 |
| 2.3 | 除法/余数运算除零保护 | 数值安全 | 高 |
| 3.1 | 禁止使用未初始化的变量 | 内存安全 | 高 |
| 3.2 | 资源释放后指针置新值 | 内存安全 | 中 |
| 3.3 | 数组索引校验 | 内存安全 | 高 |
| 3.4 | 禁止 sizeof 指针 | 内存安全 | 中 |
| 3.5 | 指针使用前判空 | 内存安全 | 高 |
| 3.6 | 字符串存储有足够空间 | 内存安全 | 高 |
| 4.1 | 外部输入合法性校验 | 输入验证 | 高 |
| 4.2 | 内存操作长度校验 | 输入验证 | 高 |
| 5.1 | 资源申请后判断是否成功 | 资源管理 | 高 |
| 5.2 | 资源泄露防护 | 资源管理 | 高 |
| 5.3 | new/delete 配对使用 | 资源管理 | 高 |
| 5.4 | 自定义 new/delete 配对定义 | 资源管理 | 中 |
| 5.5 | new 操作符错误处理 | 资源管理 | 高 |
| 5.6 | 避免 delete this | 资源管理 | 高 |
| 6.1 | 文件权限显式指定 | 文件IO | 中 |
| 6.2 | 文件路径规范化 | 文件IO | 高 |
| 6.3 | 不要在共享目录创建临时文件 | 文件IO | 中 |
| 7.1 | 访问临界资源需要保护 | 并发安全 | 高 |
| 8.1 | 使用安全函数替代危险函数 | 安全函数 | 高 |
| 8.2 | 正确设置安全函数 destMax 参数 | 安全函数 | 高 |
| 8.3 | 禁止封装安全函数 | 安全函数 | 中 |
| 8.4 | 禁止宏重命名安全函数 | 安全函数 | 中 |
| 8.5 | 禁止自定义安全函数 | 安全函数 | 中 |
| 8.6 | 检查安全函数返回值 | 安全函数 | 高 |
| 9.1 | delete/移动/swap 应有 noexcept | 类与对象 | 中 |
| 9.2 | 禁止逐位操作非 trivially copyable 对象 | 类与对象 | 中 |
| 10.1 | 禁止从空指针创建 std::string | 标准库 | 高 |
| 10.2 | 不要保存 c_str/data 指针 | 标准库 | 中 |
| 10.3 | 避免使用 atoi/atol/atof | 标准库 | 中 |
| 10.4 | 禁止 std::string 存敏感信息 | 标准库 | 高 |
| 10.5 | format 参数禁止外部控制 | 标准库 | 高 |
| 10.6 | 禁用退出函数和 atexit | 标准库 | 中 |
| 10.9 | 禁用 rand 产生安全随机数 | 标准库 | 中 |
| 10.10 | 敏感信息使用后清零 | 标准库 | 高 |
| 10.11 | 结构体字段末尾添加 | 标准库 | 中 |
| 10.12 | 接口变更考虑兼容性 | 标准库 | 中 |
| 11.1 | LOG API 禁止传入空指针 | LOG API 安全 | 高 |
| 11.2 | LOG API 参数数量与格式化占位符必须匹配 | LOG API 安全 | 高 |
| 11.3 | LOG API 参数类型与格式化说明符必须匹配 | LOG API 安全 | 高 |
| 11.4 | LOG API 禁止传入已释放内存的指针 | LOG API 安全 | 高 |

---

### 1. 总体原则

##### 规则 1.1 保证静态类型安全

C++应该是静态类型安全的，这样可以减少运行时的错误，提升代码的健壮性。但是由于C++存在下面的特性，会破坏C++静态类型安全，针对这部分特性要仔细处理：

- 联合体
- 类型转换
- 缩窄转换
- 类型退化
- 范围错误
- void* 类型指针

可以通过约束这些特性的使用，或者使用C++的新特性，例如std::variant（C++17）、std::span（C++20）等来解决这些问题，提升C++代码的健壮性。

##### 规则 1.2 保证内存安全

C++语言的内存完全由程序员自己控制，所以在操作内存的时候必须保证内存安全，防止出现内存错误：

- 内存越界访问
- 释放以后继续访问内存
- 解引用空指针
- 内存没有初始化
- 把指向局部变量的引用或者指针传递到了函数外部或者其他线程中
- 申请的内存或者资源没有及时释放

建议使用更加安全的C++的特性，比如RAII，引用，智能指针等，来提升代码的健壮性。

##### 规则 1.3 禁止使用编译器"未定义行为"

遵循ISO C++标准，标准中未定义的行为禁止使用。对于编译器实现的特性或者GCC等编译器提供的扩展特性也需要谨慎使用，这些特性会降低代码的可移植性。

---

### 2. 数值运算安全

#### 2.1 确保有符号整数运算不溢出

**【描述】**
有符号整数溢出是未定义的行为。出于安全考虑，对外部数据中的有符号整数值在如下场景中使用时，需要确保运算不会导致溢出：

- 指针运算的整数操作数(指针偏移值)
- 数组索引
- 变长数组的长度(及长度运算表达式)
- 内存拷贝的长度
- 内存分配函数的参数
- 循环判断条件

在精度低于int的整数类型上进行运算时，需要考虑整数提升。程序员还需要掌握整数转换规则，包括隐式转换规则，以便设计安全的算术运算。

**加法示例：**

```cpp
int num_a = ... // 来自外部数据
int num_b = ... // 来自外部数据
int sum = 0;
if (((num_a > 0) && (num_b > (INT_MAX - num_a))) ||
    ((num_a < 0) && (num_b < (INT_MIN - num_a)))) {
    ... // 错误处理
}
sum = num_a + num_b;
```

**除法示例：**

```cpp
int num_a = ... // 来自外部数据
int num_b = ... // 来自外部数据
int result = 0;
// 检查除数为0及除法溢出错误
if ((num_b == 0) || ((num_a == INT_MIN) && (num_b == -1))) {
    ... // 错误处理
}
result = num_a / num_b;
```

#### 2.2 确保无符号整数运算不回绕

**【描述】**
涉及无符号操作数的计算永远不会溢出，因为超出无符号整数类型表示范围的计算结果会按照（结果类型可表示的最大值 + 1）的数值取模。这种行为更多时候被非正式地称为无符号整数回绕。

**乘法示例：**

```cpp
size_t width = ... // 来自外部数据
size_t height = ... // 来自外部数据
if (width == 0 || height == 0) {
    ... // 错误处理
}
if (width > SIZE_MAX / height) {
    ... // 错误处理
}
unsigned char *buf = (unsigned char *)malloc(width * height);
```

#### 2.3 确保除法和余数运算不会导致除以零的错误

**【描述】**
整数的除法和取余运算的第二个操作数值为0会导致程序产生未定义的行为，因此使用时要确保整数的除法和余数运算不会导致除零错误。

---

### 3. 内存与指针安全

#### 3.1 禁止使用未初始化的变量

这里的变量，指的是局部动态变量，并且还包括内存堆上申请的内存块。因为他们的初始值都是不可预料的，所以禁止未经有效初始化就直接读取其值。

```cpp
void foo(...)
{
    int data;
    bar(data); // 错误：未初始化就使用
    ...
}
```

#### 3.2 指向资源句柄或描述符的变量，在资源释放后立即赋予新值

**【描述】**
指向资源句柄或描述符的变量包括指针、文件描述符、socket描述符以及其它指向资源的变量。

以指针为例，当指针成功申请了一段内存之后，在这段内存释放以后，如果其指针未立即设置为NULL，也未分配一个新的对象，那这个指针就是一个悬空指针。如果再对悬空指针操作，可能会发生重复释放或访问已释放内存的问题，造成安全漏洞。

**【正确代码示例】**

```cpp
int foo(void)
{
    SomeStruct *msg = NULL;
    ... // 初始化msg->type，分配 msg->body 的内存空间

    if (msg->type == MESSAGE_A) {
        ...
        free(msg->body);
        msg->body = NULL;
    }

    ...
EXIT:
    ...
    free(msg->body);
    return ret;
}
```

#### 3.3 外部数据作为数组索引时必须确保在数组大小范围内

**【描述】**
外部数据作为数组索引对内存进行访问时，必须对数据的大小进行严格的校验，确保数组索引在有效范围内，否则会导致严重的错误。

**【正确代码示例】**

```cpp
#define DEV_NUM 10
static Dev devs[DEV_NUM];

int set_dev_id(size_t index, int id)
{
    if (index >= DEV_NUM) {
        ... // 错误处理
    }
    devs[index].id = id;
    return 0;
}
```

#### 3.4 禁止通过对指针变量进行sizeof操作来获取数组大小

**【描述】**
将指针当做数组进行sizeof操作时，会导致实际的执行结果与预期不符。

**【错误代码示例】**

```cpp
char path[MAX_PATH];
char *buffer = (char *)malloc(SIZE);
...
(void)memset(path, 0, sizeof(path));
// sizeof与预期不符，其结果为指针本身的大小而不是缓冲区大小
(void)memset(buffer, 0, sizeof(buffer));
```

**【正确代码示例】**

```cpp
char path[MAX_PATH];
char *buffer = (char *)malloc(SIZE);
...
(void)memset(path, 0, sizeof(path));
(void)memset(buffer, 0, SIZE); // 使用申请的缓冲区大小
```

#### 3.5 指针操作，使用前必须要判空

**【描述】**
解引用空指针会导致程序产生未定义行为，通常会造成程序异常终止。

- 指针变量在使用前，一定要做好初始化的赋值，严禁对空指针进行访问
- 对于指针所代表的地址空间的任何操作，一定要保证空间的有效性
- 指针指向的内存释放后，需要调用者将指针显式置为NULL，防止"野指针"

#### 3.6 确保字符串存储有足够的空间容纳字符数据和null结束符

**【描述】**
将数据复制到不足以容纳数据的缓冲区，会导致缓冲区溢出。

---

### 4. 输入验证

#### 4.1 外部输入数据需要做合法性校验

**【描述】**

- 外部输入数据需要做合法性校验且确保校验范围正确
- 边界接口需要对传入的地址做合法性校验避免任意地址读写
- 需要对入参进行合法性校验避免数组越界
- 需要对地址偏移校验避免任意地址读写
- 外部传入指针需要判空后使用
- 外部入参参与循环、递归条件的运算，必须严格校验边界和终止条件
- 文件路径来自外部数据时，必须对其做合法性校验

#### 4.2 外部输入作为内存操作相关函数的复制长度时，需要校验其合法性

**【描述】**
将数据复制到容量不足以容纳该数据的内存中会导致缓冲区溢出。必须根据目标容量的大小限制被复制的数据大小，或者必须确保目标容量足够大以容纳要复制的数据。

---

### 5. 资源管理

#### 5.1 资源申请后必须判断是否成功

**【描述】**
内存、对象、stream、notify等资源申请分配一旦失败，那么后续的操作会存在未定义的行为风险。

**【正确代码示例】**

```cpp
struct tm *make_tm(int year, int mon, int day, int hour, int min, int sec)
{
    struct tm *tmb = (struct tm *)malloc(sizeof(*tmb));
    if (tmb == NULL) {
        ... // 错误处理
    }
    tmb->year = year;
    ...
    return tmb;
}
```

#### 5.2 资源泄露（内存、句柄、锁等）

**【描述】**

- 资源申请和释放必须匹配，包括：内存类的malloc/free/alloc_page/free_page, 锁lock/unlock、文件open/close等
- 释放结构体/类/数组/各类数据容器指针前，必须先释放成员指针
- 对外接口处理涉及资源申请但未释放，引起资源泄露，导致拒绝服务
- C++捕获异常时确保恢复程序的一致性; 建议使用RAII模式，确保资源在异常发生时自动释放

#### 5.3 new和delete配对使用，new[]和delete[]配对使用

#### 5.4 自定义new/delete操作符需要配对定义，且行为与被替换的操作符一致

#### 5.5 使用恰当的方式处理new操作符的内存分配错误

#### 5.6 避免出现delete this操作

---

### 6. 文件IO安全

#### 6.1 创建文件时必须显式指定合适的文件访问权限

**【描述】**
创建文件时，如果不显式指定合适访问权限，可能会让未经授权的用户访问该文件，造成信息泄露，文件数据被篡改，文件中被注入恶意代码等风险。

#### 6.2 必须对文件路径进行规范化后再使用

**【描述】**
当文件路径来自外部数据时，必须对其做合法性校验。但是禁止直接对其进行校验，正确做法是在校验之前必须对其进行路径规范化处理。

**【正确代码示例】**

```cpp
char *file_name = get_msg_from_remote();
...
sprintf(untrust_path, "/tmp/%s", file_name);
char path[PATH_MAX] = {0};
if (realpath(untrust_path, path) == NULL) {
    ... // 处理错误
}
if (!is_valid_path(path)) { // 检查文件的位置是否正确
    ... // 处理错误
}
char *text = read_file_content(path);
```

#### 6.3 不要在共享目录中创建临时文件

**【描述】**
程序的临时文件应当是程序自身独享的，任何将自身临时文件置于共享目录的做法，将导致其他共享用户获得该程序的额外信息，产生信息泄露。

---

### 7. 并发安全

#### 7.1 访问临界资源需要进行保护

**【描述】**
临界资源的保护是多线程编程中的关键问题，不合理的访问会导致数据不一致、内存踩踏、程序崩溃、死锁等问题，需要使用互斥锁进行保护或使用线程安全函数。以下场景需要尤其注意：

- 多线程共享的全局变量
- 线程与中断都会访问的数据结构
- C语言非线程安全的函数在多线程环境下调用
- 用户态线程与信号处理函数都会访问的变量/数据结构

---

### 8. 安全函数使用

#### 8.1 使用社区提供的安全函数库的安全函数，禁止使用内存操作类危险函数

| 函数类别 | 危险函数 | 安全替代函数 |
|---------|---------|------------|
| 内存拷贝 | memcpy或bcopy | memcpy_s |
| 内存拷贝 | memmove | memmove_s |
| 字符串拷贝 | strcpy | strcpy_s |
| 字符串串接 | strcat | strcat_s |
| 格式化输出 | sprintf | sprintf_s |
| 格式化输出 | snprintf | snprintf_s |
| 格式化输入 | scanf | scanf_s |
| 内存初始化 | memset | memset_s |

#### 8.2 正确设置安全函数中的destMax参数

#### 8.3 禁止封装安全函数

#### 8.4 禁止用宏重命名安全函数

```cpp
// 错误示范
#define XXX_memcpy_s memcpy_s
#define SEC_MEM_CPY memcpy_s
```

#### 8.5 禁止自定义安全函数

#### 8.6 必须检查安全函数返回值，并进行正确的处理

原则上，如果使用了安全函数，需要进行返回值检查。如果返回值!=EOK, 那么本函数一般情况下应该立即返回，不能继续执行。

```cpp
{
    ...
    err = memcpy_s(destBuff, destMax, src, srcLen);
    if (err != EOK) {
        HIXL_LOG("memcpy_s failed, err = %d\n", err);
        return FALSE;
    }
    ...
}
```

---

### 9. 类与对象安全

#### 9.1 delete操作符、移动构造函数、移动赋值操作符、swap函数应该有noexcept声明

#### 9.2 禁止逐位操作非trivially copyable对象

---

### 10. 标准库安全

#### 10.1 禁止从空指针创建std::string


#### 10.2 不要保存std::string类型的 `c_str`和 `data`成员函数返回的指针

#### 10.3 避免使用atoi、atol、atoll、atof函数

#### 10.4 禁止使用std::string存储敏感信息

#### 10.5 调用格式化输入/输出函数时，禁止format参数受外部数据控制

#### 10.6 禁用程序与线程的退出函数和atexit函数

#### 10.9 禁用rand函数产生用于安全用途的伪随机数

C标准库rand()函数生成的是伪随机数，请使用/dev/random生成随机数。

#### 10.10 内存中的敏感信息使用完毕后立即清0

口令、密钥等敏感信息使用完毕后立即清零，避免被攻击者获取。

#### 10.11 对外结构体接口新增字段时必须在结构体最后添加

为了最大程度上在ABI层面的兼容，对外结构体接口添加新字段时必须在结构体最后添加。

#### 10.12 外部接口或数据结构变更必须考虑兼容性

外部接口、接口参数、返回值、数据结构、消息字段等变更会引起版本兼容性问题，非必要不建议变更。

---

### 11. LOG API 安全使用

使用 `HIXL_LOGE` / `HIXL_LOGI` / `HIXL_LOGD` / `HIXL_LOGW` 等格式化 LOG 宏时，若参数使用不当，轻则输出乱码，重则引发段错误（SIGSEGV）。以下 4 条为强制要求。

LOG 宏签名（业务代码标准调用形式）：

```cpp
HIXL_LOGE(ERROR_CODE, "format string %s %ld", arg1, arg2);
HIXL_LOGI("format string  %s %ld", arg1, arg2);
HIXL_LOGD("format string %lu", arg1);
HIXL_LOGW("format string %lu", arg1);
```

---

#### 11.1 LOG API 禁止传入空指针作为字符串参数

**【问题说明】**

`%s` 会解引用传入指针，若指针为 `nullptr`，将访问地址 0（受 OS 保护），导致段错误。

**错误示例**

```cpp
auto channel = channel_manager->GetChannel(type, id);
HIXL_LOGI("transfer_count: %d", channel->GetTransferCount());
// 风险：channel 未判空就调用成员函数
```

**正确示例**

```cpp
auto channel = channel_manager->GetChannel(type, id);
if (channel == nullptr) {
  HIXL_LOGE(FAILED, "channel is nullptr");
  return FAILED;
}
HIXL_LOGI("transfer_count: %d", channel->GetTransferCount());
```

---

#### 11.2 LOG API 参数数量必须与格式化占位符数量一致

**【问题说明】**

参数数量少于占位符时，LOG 宏会从栈上读取垃圾值填充缺失参数。若垃圾值被解释为非法指针（`%s`/`%p`），将触发非法内存访问。

**错误示例**

```cpp
// 参考 hixl_engine.cc 中的多参数日志场景
// 3 个占位符，但只传了 2 个参数
HIXL_LOGI("[HixlEngine] Disconnection started, local_engine:%s, remote_engine:%s, timeout:%d ms",
           local_engine_.c_str(), remote_engine.GetString());  // 缺少 timeout_in_millis，栈数据被错误读取
```

**正确示例**

```cpp
HIXL_LOGI("[HixlEngine] Disconnection started, local_engine:%s, remote_engine:%s, timeout:%d ms",
           local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
```

---

#### 11.3 LOG API 参数类型必须与格式化说明符匹配

**【问题说明】**

类型大小不匹配时，LOG 宏按说明符宽度截断或读取超量字节，导致后续参数全部错位。

**错误示例**

```cpp
HIXL_LOGE(FAILED, "Failed to transfer data1[size = %d], data2[size = %d]", size1, size2);
// 错误：size1 & size2 均为 uint64_t，%d 只读 4 字节，后续参数全部错位
```

**正确示例**

```cpp
// 业务代码正确写法
HIXL_LOGE(FAILED, "Failed to transfer data1[size = %lu], data2[size = %lu]", size1, size2);
```

**HIXL 常见类型与说明符对照**

| 类型 | 正确说明符 | 常见错误 |
|------|---------|---------|
| `int64_t` | `%ld` | `%d` |
| `uint64_t` | `%lu` | `%d`、`%u` |
| `uint32_t` | `%u` | `%d` |
| `bool` | `%s` + 三元表达式 | `%d` |
| `const char*` | `%s` | `%d` |

```cpp
// bool 的正确记录方式
HIXL_LOGI("Channel is disconnecting: %s", channel->IsDisconnecting() ? "true" : "false");
```

---

#### 11.4 LOG API 禁止传入已释放内存的指针

**【问题说明】**

手动管理的堆内存（`new` / `malloc`）释放后若仍传入 `%s`，行为未定义，大概率触发段错误。典型场景：在函数末尾统一释放资源，但 LOG 语句写在释放之后。

**错误示例**

```cpp
char* errMsg = new char[256];
snprintf(errMsg, 256, "call func failed, ret=%ld", ret);
delete[] errMsg;
HIXL_LOGE(FAILED, "error: %s", errMsg);   // 野指针，已释放
```

**正确示例**

```cpp
char* errMsg = new char[256];
snprintf(errMsg, 256, "call func failed, ret=%ld", ret);
HIXL_LOGE(FAILED, "error: %s", errMsg);   // 先记录
delete[] errMsg;
errMsg = nullptr;
```
