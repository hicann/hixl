# CANN C++ General Coding Specifications

>  **Applicable Scope**: The general programming specifications apply to all C++ code.

## Rule List

| Rule No. | Rule Name | Category |
|---------|---------|------|
| 1.1 | Validate external data legitimacy | Code Design |
| 1.2 | Prefer return values for function results | Code Design |
| 1.3 | Clean up invalid and redundant code (Recommendation) | Code Design |
| 2.1 | Use new standard C++ headers | Headers |
| 2.2 | Circular header dependency is prohibited | Headers |
| 2.3 | Avoid including unused headers (Recommendation) | Headers |
| 2.4 | Do not reference external interfaces via extern declarations | Headers |
| 2.5 | Do not include headers within extern "C" | Headers |
| 2.6 | Avoid using using to import namespaces in headers (Recommendation) | Headers |
| 3.1 | Avoid abusing typedef/#define type aliases | Data Types |
| 3.2 | Use using instead of typedef to define aliases | Data Types |
| 4.1 | Do not use macros to represent constants | Constants |
| 4.2 | Do not use magic numbers/strings | Constants |
| 4.3 | Each constant should have a single responsibility | Constants |
| 5.1 | Prefer namespaces to manage global constants | Variables |
| 5.2 | Avoid global variables; use singletons with caution | Variables |
| 5.3 | Do not reference a variable again within its own increment/decrement expression | Variables |
| 5.4 | Assign a new value to a pointer after releasing the resource | Variables |
| 5.5 | Do not use uninitialized variables | Variables |
| 6.1 | Variable on the left, constant on the right in comparisons | Expressions |
| 6.2 | Use parentheses to clarify operator precedence | Expressions |
| 7.1 | Use C++ casts instead of C-style casts | Casting |
| 8.1 | switch statements must have a default branch | Control Statements |
| 9.1 | Do not use memcpy_s/memset_s to initialize non-POD objects | Declaration & Initialization |
| 10.1 | Do not hold the pointer returned by c_str() | Pointers & Arrays |
| 10.2 | Prefer unique_ptr over shared_ptr | Pointers & Arrays |
| 10.3 | Use make_shared instead of new to create shared_ptr | Pointers & Arrays |
| 10.4 | Use smart pointers to manage objects | Pointers & Arrays |
| 10.5 | Do not use auto_ptr | Pointers & Arrays |
| 10.6 | Use const for pointer/reference parameters that are not modified | Pointers & Arrays |
| 10.7 | Array parameters must be passed together with their length | Pointers & Arrays |
| 11.1 | Ensure string storage has a '\0' terminator | Strings |
| 12.1 | Assertions must not be used for runtime error handling | Assertions |
| 13.1 | Use delete/delete[] in matching pairs | Classes & Objects |
| 13.2 | Do not use std::move on const objects | Classes & Objects |
| 13.3 | Strictly use virtual/override/final | Classes & Objects |
| 14.1 | Use RAII to track dynamic allocations | Function Design |
| 14.2 | Non-local lambdas should avoid capture by reference | Function Design |
| 14.3 | Virtual functions must not use default parameter values | Function Design |
| 14.4 | Use strongly-typed parameters; avoid void* | Function Design |
| 15.1 | Keep parameter order consistent within the same file | Function Usage |
| 15.2 | Use const T& for input, T& or T* for output | Function Usage |
| 15.3 | Use T* or const T& when ownership is not involved | Function Usage |
| 15.4 | Use shared_ptr + move to transfer ownership | Function Usage |
| 15.5 | Single-argument constructors must use explicit | Function Usage |
| 15.6 | Copy constructor and assignment operator must appear in pairs | Function Usage |
| 15.7 | Do not store or delete pointer parameters | Function Usage |

---

### 1. Code Design

##### Rule 1.1 Validate all external data for legitimacy, including but not limited to: function parameters, external input command lines, files, environment variables, user data, etc.

##### Rule 1.2 Prefer return values to pass function execution results; avoid using output parameters

```cpp
FooBar *Func(const std::string &in);
```

##### Recommendation 1.3 Clean up invalid, redundant, or never-executed code

Although most modern compilers can warn about invalid or never-executed code in many cases, you should respond to warnings by identifying and clearing them; you should proactively identify invalid statements or expressions and remove them from the code.

> **Note**: Business code often pre-defines reserved parameters for specific scenarios (e.g., reserved function parameters, reserved struct fields). These parameters may currently be unreferenced but are reasonable engineering reservations and should not be marked as FAIL during review.
>
> The following cases may be marked as SUSPICIOUS for developer reference:
> - Obvious dead code (e.g., code blocks permanently excluded by conditional compilation)
> - Large blocks of commented-out code (git should be used to manage history)
> - Variables or expressions that are clearly invalid and have no reserved intent

##### Rule 1.4 Supplementary specifications for the C++ exception mechanism

###### Rule 1.4.1 Specify the exception type to catch; do not catch all exceptions

```cpp
// Incorrect example
try {
  // do something;
} catch (...) {
  // do something;
}
// Correct example
try {
  // do something;
} catch (const std::bad_alloc &e) {
  // do something;
}
```

---

### 2. Headers and Preprocessing

##### Rule 2.1 Use new standard C++ headers

```cpp
// Correct example
#include <cstdlib>
// Incorrect example
#include <stdlib.h>
```

##### Rule 2.2 Circular header dependency is prohibited

Circular header dependency means that a.h includes b.h, b.h includes c.h, and c.h includes a.h, which causes any modification to any one header file to trigger recompilation of all code that includes a.h/b.h/c.h. Circular header dependency directly reflects unreasonable architectural design and can be avoided by optimizing the architecture.

##### Recommendation 2.3 Avoid including headers that are not used

> **Note**: Unused headers increase compilation dependencies and compilation time. However, some headers may be reserved for future feature expansion or used to provide forward declarations. During review, these should only be noted as reminders, not forced to FAIL.

##### Rule 2.4 Do not reference external function interfaces or variables through extern declarations

##### Rule 2.5 Do not include headers within extern "C"

##### Recommendation 2.6 Avoid using `using` to import namespaces in header files

The propagation scope of `using namespace` in a header file depends on its scope:
- **file-scope** (outside any namespace): propagates to all translation units that include the header
- **namespace-scoped** (inside `namespace X {}`): only propagates to code that reopens the same namespace

> **Project-internal namespace exemption**: Importing project-internal namespaces (controlled by the project, low collision risk) is not flagged.

**FAIL conditions** (any one triggers FAIL, but harm analysis must be done first before grading):

1. **file-scope `using namespace` appears before subsequent `#include` in the same file** — subsequent includes are compiled in that namespace context
2. **Shared header paths** (e.g., `include/` and other public directories) import large namespaces (`std`, etc.) at file-scope

> **Before grading, the propagation chain must be traced to assess actual harm; structural violation ≠ substantive harm:**
>
> 1. **Confirm using scope**: If inside a `namespace X {}` block, it is only visible to X and does not leak to the includer's file-scope — **not file-scope pollution**
> 2. **Trace include chain**: If the include guard of the polluted header was already activated by an earlier header before the using, then **pollution did not actually occur**
> 3. **Check sub-header autonomy**: If the sub-header has its own identical `using namespace`, the external pollution is **redundant** with no added risk
> 4. **Grade**: Real new pollution → FAIL; structural violation without substantive harm → SUSPICIOUS, note the reason

**SUSPICIOUS conditions** (flag as reminder, do not force FAIL):

- Non-shared headers importing large namespaces (`std`, etc.) at file-scope
- Public API headers (`include/` directory) importing non-project namespaces (included by external callers)
- Shared headers importing large namespaces **inside** a namespace (not file-scope)

---

### 3. Data Types

##### Recommendation 3.1 Avoid abusing typedef or #define to alias basic types

##### Rule 3.2 Use using instead of typedef to define type aliases to avoid shotgun modifications caused by type changes

```cpp
// Correct example
using FooBarPtr = std::shared_ptr<FooBar>;
// Incorrect example
typedef std::shared_ptr<FooBar> FooBarPtr;
```

---

### 4. Constants

##### Rule 4.1 Do not use macros to represent constants

##### Rule 4.2 Do not use magic numbers\strings

##### Recommendation 4.3 Each constant should have a single responsibility

---

### 5. Variables

##### Rule 5.1 Prefer namespaces to manage global constants. If there is a direct relationship with a class, static member constants may be used

```cpp
namespace foo {
  int kGlobalVar;

  class Bar {
    private:
      static int static_member_var_;
  };
}
```

##### Rule 5.2 Avoid using global variables; use the singleton pattern with caution and avoid abuse

##### Rule 5.3 Do not reference a variable again within an expression that contains its own increment or decrement operation

##### Rule 5.4 Pointer variables pointing to resource handles or descriptors must be assigned a new value or set to NULL immediately after the resource is released

##### Rule 5.5 Do not use uninitialized variables

---

### 6. Expressions

##### Recommendation 6.1 Comparisons in expressions should follow the principle that the left side tends to change and the right side tends to remain constant

```cpp
// Correct example
if (ret != SUCCESS) {
  ...
}

// Incorrect example
if (SUCCESS != ret) {
  ...
}
```

##### Rule 6.2 Use parentheses to clarify operator precedence and avoid low-level errors

```cpp
// Correct example
if (cond1 || (cond2 && cond3)) {
  ...
}

// Incorrect example
if (cond1 || cond2 && cond3) {
  ...
}
```

---

### 7. Casting

##### Rule 7.1 Use the type casts provided by C++ instead of C-style casts; avoid using const_cast and reinterpret_cast

---

### 8. Control Statements

##### Rule 8.1 switch statements must have a default branch

---

### 9. Declaration and Initialization

##### Rule 9.1 Do not use `memcpy_s` or `memset_s` to initialize non-POD objects

---

### 10. Pointers and Arrays

##### Rule 10.1 Do not hold the pointer returned by std::string's c_str()

```cpp
// Incorrect example
const char * a = std::to_string(12345).c_str();
```

##### Rule 10.2 Prefer unique_ptr over shared_ptr

##### Rule 10.3 Use std::make_shared instead of new to create shared_ptr

```cpp
// Correct example
std::shared_ptr<FooBar> foo = std::make_shared<FooBar>();
// Incorrect example
std::shared_ptr<FooBar> foo(new FooBar());
```

##### Rule 10.4 Use smart pointers to manage objects; avoid using new/delete

##### Rule 10.5 Do not use auto_ptr

##### Rule 10.6 For pointer and reference parameters, if they do not need to be modified, const must be used

##### Rule 10.7 When an array is used as a function parameter, its length must also be passed as a function parameter

```cpp
int ParseMsg(BYTE *msg, size_t msgLen) {
  ...
}
```

---

### 11. Strings

##### Rule 11.1 When performing storage operations on strings, ensure the string has a '\0' terminator

---

### 12. Assertions

##### Rule 12.1 Assertions must not be used to check errors that may occur during runtime; such runtime errors must be handled with error-handling code

---

### 13. Classes and Objects

##### Rule 13.1 Use delete to release a single object; use delete[] to release an array object

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

##### Rule 13.2 Do not use std::move on const objects

##### Rule 13.3 Strictly use virtual/override/final to modify virtual functions

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

### 14. Function Design

##### Rule 14.1 Use RAII to help track dynamic allocations

```cpp
// Correct example
{
  std::lock_guard<std::mutex> lock(mutex_);
  ...
}
```

##### Rule 14.2 When using lambdas in a non-local scope, avoid capture by reference

```cpp
{
  int local_var = 1;
  auto func = [&]() { ...; std::cout << local_var << std::endl; };
  thread_pool.commit(func);
}
```

##### Rule 14.3 Virtual functions must not use default parameter values

##### Recommendation 14.4 Use strongly-typed parameters\member variables; avoid using void*

---

### 15. Function Usage

##### Recommendation 15.1 Keep function parameter order consistent within the same file (or module)

> **Note**: "Input parameters first, output parameters last" is not enforced. As long as the parameter order style is uniform within the same file, it is acceptable (e.g., consistently using input-first, or consistently using output-first). During review, use the style of the majority of functions in the file as the baseline and only flag clearly inconsistent cases.

```cpp
// ✅ Consistent style: all input-first
bool FuncA(const std::string &in, FooBar *out1, FooBar *out2);
bool FuncB(int val, Result *out);

// ✅ Consistent style: all output-first
bool FuncC(FooBar *out1, FooBar *out2, const std::string &in);
bool FuncD(Result *out, int val);

// ❌ Inconsistent: mixed within the same file
bool FuncE(const std::string &in, FooBar *out);   // input-first
bool FuncF(Result *out, int val);           // output-first → style inconsistency
```

##### Recommendation 15.2 When passing function parameters, use `const T &` for input and `T &` or `T *` for output

> **Note**: Output parameters may use references (`T &`) or pointers (`T *`). In practice, `T &` is the more common output parameter style (especially for scalar outputs and structs), while `T *` is more common when nullable semantics are needed. Reviews should not require output parameters to be pointers, but the style should be consistent within the same file.

```cpp
// ✅ Output via reference (common style)
bool Func(const std::string &in, FooBar &out1, FooBar &out2);

// ✅ Output via pointer (nullable semantics style)
bool Func(const std::string &in, FooBar *out1, FooBar *out2);

// ❌ Mixed within the same file (style inconsistency)
void FuncA(const Input &in, Output &out);       // output via reference
void FuncB(const Input &in, Output *out);       // output via pointer → style inconsistency within the same file
```

##### Rule 15.3 When passing function parameters in scenarios that do not involve ownership, use T * or const T & as parameters instead of smart pointers

```cpp
// Correct example
  bool Func(const FooBar &in);
  // Incorrect example
  bool Func(std::shared_ptr<FooBar> in);
```

##### Rule 15.4 When passing function parameters, if ownership needs to be transferred, it is recommended to use shared_ptr + move

```cpp
class Foo {
  public:
    explicit Foo(shared_ptr<T> x):x_(std::move(x)){}
  private:
    shared_ptr<T> x_;
};
```

##### Rule 15.5 Single-argument constructors must use explicit; multi-argument constructors must not use explicit

```cpp
explicit Foo(int x);          // good
explicit Foo(int x, int y=0); // good
Foo(int x, int y=0);          // bad
explicit Foo(int x, int y);   // bad
```

##### Rule 15.6 Copy constructors and copy assignment operators should appear in pairs or be prohibited

```cpp
class Foo {
  private:
    Foo(const Foo&) = default;
    Foo& operator=(const Foo&) = default;
    Foo(Foo&&) = delete;
    Foo& operator=(Foo&&) = delete;
};
```

##### Rule 15.7 Do not store or delete pointer parameters

---

> **Note**: For security-related coding specifications (such as memory safety, input validation, and secure function usage), see [cpp-secure.md](cpp-secure.md).
