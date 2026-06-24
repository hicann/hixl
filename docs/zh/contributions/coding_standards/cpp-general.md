# CANN C++ 通用编码规范

> **适用场景**：通用编程规范适用于所有 C++ 代码。

## 规范列表

| 规范编号 | 规范名称 | 类别 |
|---------|---------|------|
| 1.1 | 外部数据合法性检查 | 代码设计 |
| 1.2 | 函数结果优先使用返回值 | 代码设计 |
| 1.3 | 删除无效冗余代码 | 代码设计 |
| 2.1 | 使用新的标准 C++ 头文件 | 头文件 |
| 2.2 | 禁止头文件循环依赖 | 头文件 |
| 2.3 | 禁止包含用不到的头文件 | 头文件 |
| 2.4 | 禁止 extern 声明引用外部接口 | 头文件 |
| 2.5 | 禁止在 extern "C" 中包含头文件 | 头文件 |
| 2.6 | 禁止头文件中 using 导入命名空间 | 头文件 |
| 3.1 | 避免滥用 typedef/#define 类型别名 | 数据类型 |
| 3.2 | 使用 using 而非 typedef 定义别名 | 数据类型 |
| 4.1 | 禁止使用宏表示常量 | 常量 |
| 4.2 | 禁止使用魔鬼数字/字符串 | 常量 |
| 4.3 | 每个常量保证单一职责 | 常量 |
| 5.1 | 优先使用命名空间管理全局常量 | 变量 |
| 5.2 | 避免全局变量，谨慎使用单例 | 变量 |
| 5.3 | 禁止变量自增/自减表达式中再次引用 | 变量 |
| 5.4 | 资源释放后指针置新值 | 变量 |
| 5.5 | 禁止使用未经初始化的变量 | 变量 |
| 6.1 | 表达式比较左变右不变 | 表达式 |
| 6.2 | 使用括号明确操作符优先级 | 表达式 |
| 7.1 | 使用 C++ 类型转换而非 C 风格 | 转换 |
| 8.1 | switch 语句要有 default 分支 | 控制语句 |
| 9.1 | 禁止用 memcpy_s/memset_s 初始化非 POD | 声明初始化 |
| 10.1 | 禁止持有 c_str() 返回的指针 | 指针数组 |
| 10.2 | 优先使用 unique_ptr 而非 shared_ptr | 指针数组 |
| 10.3 | 使用 make_shared 而非 new 创建 shared_ptr | 指针数组 |
| 10.4 | 使用智能指针管理对象 | 指针数组 |
| 10.5 | 禁止使用 auto_ptr | 指针数组 |
| 10.6 | 指针/引用形参不修改用 const | 指针数组 |
| 10.7 | 数组参数必须同时传长度 | 指针数组 |
| 11.1 | 字符串存储确保有 '\0' 结束符 | 字符串 |
| 12.1 | 断言不能用于运行期错误处理 | 断言 |
| 13.1 | delete/delete[] 配对使用 | 类和对象 |
| 13.2 | 禁止 std::move 操作 const 对象 | 类和对象 |
| 13.3 | 严格使用 virtual/override/final | 类和对象 |
| 14.1 | 使用 RAII 追踪动态分配 | 函数设计 |
| 14.2 | 非局部 lambda 避免按引用捕获 | 函数设计 |
| 14.3 | 禁止虚函数使用缺省参数值 | 函数设计 |
| 14.4 | 使用强类型参数，避免 void* | 函数设计 |
| 15.1 | 函数传参入参在前出参在后 | 函数使用 |
| 15.2 | 入参用 const T&，出参用 T* | 函数使用 |
| 15.3 | 不涉及所有权用 T* 或 const T& | 函数使用 |
| 15.4 | 传递所有权用 shared_ptr + move | 函数使用 |
| 15.5 | 单参数构造函数用 explicit | 函数使用 |
| 15.6 | 拷贝构造和赋值操作符成对出现 | 函数使用 |
| 15.7 | 禁止保存、delete 指针参数 | 函数使用 |

---

### 1. 代码设计

##### 规则 1.1 对所有外部数据进行合法性检查，包括但不限于：函数入参、外部输入命名行、文件、环境变量、用户数据等

##### 规则 1.2 函数执行结果传递，优先使用返回值，尽量避免使用出参

```cpp
FooBar *Func(const std::string &in);
```

##### 规则 1.3 删除无效、冗余或永不执行的代码

虽然大多数现代编译器在许多情况下可以对无效或从不执行的代码告警，响应告警应识别并清除告警；
应该主动识别无效的语句或表达式，并将其从代码中删除。

##### 规则 1.4 补充C++异常机制的规范

###### 规则 1.4.1 需要指定捕获异常种类，禁止捕获所有异常

```cpp
// 错误示范
try {
  // do something;
} catch (...) {
  // do something;
}
// 正确示范
try {
  // do something;
} catch (const std::bad_alloc &e) {
  // do something;
}
```

---

### 2. 头文件和预处理

##### 规则 2.1 使用新的标准C++头文件

```cpp
// 正确示范
#include <cstdlib>
// 错误示范
#include <stdlib.h>
```

##### 规则 2.2 禁止头文件循环依赖

头文件循环依赖，指a.h包含b.h，b.h包含c.h，c.h包含a.h之类导致任何一个头文件修改，都导致所有包含了a.h/b.h/c.h的代码全部重新编译一遍。
头文件循环依赖直接体现了架构设计上的不合理，可通过优化架构去避免。

##### 规则 2.3 禁止包含用不到的头文件

##### 规则 2.4 禁止通过 extern 声明的方式引用外部函数接口、变量

##### 规则 2.5 禁止在extern "C"中包含头文件

##### 规则 2.6 禁止在头文件中或者#include之前使用using导入命名空间

---

### 3. 数据类型

##### 建议 3.1 避免滥用 typedef或者#define 对基本类型起别名

##### 规则 3.2 使用using 而非typedef定义类型的别名，避免类型变化带来的散弹式修改

```cpp
// 正确示范
using FooBarPtr = std::shared_ptr<FooBar>;
// 错误示范
typedef std::shared_ptr<FooBar> FooBarPtr;
```

---

### 4. 常量

##### 规则 4.1 禁止使用宏表示常量

##### 规则 4.2 禁止使用魔鬼数字\字符串

##### 建议 4.3 建议每个常量保证单一职责

---

### 5. 变量

##### 规则 5.1 优先使用命名空间来管理全局常量，如果和某个class有直接关系的，可以使用静态成员常量

```cpp
namespace foo {
  int kGlobalVar;

  class Bar {
    private:
      static int static_member_var_;
  };
}
```

##### 规则 5.2 尽量避免使用全局变量，谨慎使用单例模式，避免滥用

##### 规则 5.3 禁止在变量自增或自减运算的表达式中再次引用该变量

##### 规则 5.4 指向资源句柄或描述符的指针变量在资源释放后立即赋予新值或置为NULL

##### 规则 5.5 禁止使用未经初始化的变量

---

### 6. 表达式

##### 建议 6.1 表达式的比较遵循左侧倾向于变化、右侧倾向于不变的原则

```cpp
// 正确示范
if (ret != SUCCESS) {
  ...
}

// 错误示范
if (SUCCESS != ret) {
  ...
}
```

##### 规则 6.2 通过使用括号明确操作符的优先级，避免出现低级错误

```cpp
// 正确示范
if (cond1 || (cond2 && cond3)) {
  ...
}

// 错误示范
if (cond1 || cond2 && cond3) {
  ...
}
```

---

### 7. 转换

##### 规则 7.1 使用有C++提供的类型转换，而不是C风格的类型转换，避免使用const_cast和reinterpret_cast

---

### 8. 控制语句

##### 规则 8.1 switch语句要有default分支

---

### 9. 声明与初始化

##### 规则 9.1 禁止用 `memcpy_s`、`memset_s`初始化非POD对象

---

### 10. 指针和数组

##### 规则 10.1 禁止持有std::string的c_str()返回的指针

```cpp
// 错误示范
const char * a = std::to_string(12345).c_str();
```

##### 规则 10.2 优先使用unique_ptr 而不是shared_ptr

##### 规则 10.3 使用std::make_shared 而不是new 创建shared_ptr

```cpp
// 正确示范
std::shared_ptr<FooBar> foo = std::make_shared<FooBar>();
// 错误示范
std::shared_ptr<FooBar> foo(new FooBar());
```

##### 规则 10.4 使用智能指针管理对象，避免使用new/delete

##### 规则 10.5 禁止使用auto_ptr

##### 规则 10.6 对于指针和引用类型的形参，如果是不需要修改的，要求使用const

##### 规则 10.7 数组作为函数参数时，必须同时将其长度作为函数的参数

```cpp
int ParseMsg(BYTE *msg, size_t msgLen) {
  ...
}
```

---

### 11. 字符串

##### 规则 11.1 对字符串进行存储操作，确保字符串有'\0'结束符

---

### 12. 断言

##### 规则 12.1 断言不能用于校验程序在运行期间可能导致的错误，可能发生的运行错误要用错误处理代码来处理

---

### 13. 类和对象

##### 规则 13.1 单个对象释放使用delete，数组对象释放使用delete []

```cpp
const int kSize = 5;
int *number_array = new int[kSize];
int *number = new int();
...
delete[] number_array;
number_array = nullptr;
delete number;
number = nullptr;
```

##### 规则 13.2 禁止使用std::move操作const对象

##### 规则 13.3 严格使用virtual/override/final修饰虚函数

```cpp
class Base {
  public:
    virtual void Func();
};

class Derived : public Base {
  public:
    void Func() override;
};

class FinalDerived : public Derived {
  public:
    void Func() final;
};
```

---

### 14. 函数设计

##### 规则 14.1 使用 RAII 特性来帮助追踪动态分配

```cpp
// 正确示范
{
  std::lock_guard<std::mutex> lock(mutex_);
  ...
}
```

##### 规则 14.2 非局部范围使用lambdas时，避免按引用捕获

```cpp
{
  int local_var = 1;
  auto func = [&]() { ...; std::cout << local_var << std::endl; };
  thread_pool.commit(func);
}
```

##### 规则 14.3 禁止虚函数使用缺省参数值

##### 建议 14.4 使用强类型参数\成员变量，避免使用void*

---

### 15. 函数使用

##### 规则 15.1 函数传参传递，要求入参在前，出参在后

```cpp
bool Func(const std::string &in, FooBar *out1, FooBar *out2);
```

##### 规则 15.2 函数传参传递，要求入参用 `const T &`，出参用 `T *`

```cpp
bool Func(const std::string &in, FooBar *out1, FooBar *out2);
```

##### 规则 15.3 函数传参传递，不涉及所有权的场景，使用T * 或const T & 作为参数，而不是智能指针

```cpp
// 正确示范
  bool Func(const FooBar &in);
  // 错误示范
  bool Func(std::shared_ptr<FooBar> in);
```

##### 规则 15.4 函数传参传递，如需传递所有权，建议使用shared_ptr + move传参

```cpp
class Foo {
  public:
    explicit Foo(shared_ptr<T> x):x_(std::move(x)){}
  private:
    shared_ptr<T> x_;
};
```

##### 规则 15.5 单参数构造函数必须用explicit修饰，多参数构造函数禁止使用explicit修饰

```cpp
explicit Foo(int x);          // good
explicit Foo(int x, int y=0); // good
Foo(int x, int y=0);          // bad
explicit Foo(int x, int y);   // bad
```

##### 规则 15.6 拷贝构造和拷贝赋值操作符应该是成对出现或者禁止

```cpp
class Foo {
  private:
    Foo(const Foo&) = default;
    Foo& operator=(const Foo&) = default;
    Foo(Foo&&) = delete;
    Foo& operator=(Foo&&) = delete;
};
```

##### 规则 15.7 禁止保存、delete指针参数

---

> **说明**：安全相关的编码规范（如内存安全、输入验证、安全函数使用等）请参见[cpp-secure.md](cpp-secure.md)。
