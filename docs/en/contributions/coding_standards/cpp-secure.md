# CANN C++ Secure Coding Specifications

>  **Applicable Scope**: Secure coding red-line specifications that all C++ code must follow 100%.

## Rule List

| Rule No. | Rule Name | Category | Severity |
|---------|---------|------|---------|
| 1.1 | Ensure static type safety | General Principles | High |
| 1.2 | Ensure memory safety | General Principles | High |
| 1.3 | Do not use undefined behavior | General Principles | High |
| 2.1 | Signed integer operations must not overflow | Numeric Safety | High |
| 2.2 | Unsigned integer operations must not wrap around | Numeric Safety | High |
| 2.3 | Division/remainder operations must have divide-by-zero protection | Numeric Safety | High |
| 3.1 | Do not use uninitialized variables | Memory Safety | High |
| 3.2 | Assign a new value to a pointer after resource release | Memory Safety | Medium |
| 3.3 | Array index validation | Memory Safety | High |
| 3.4 | Do not sizeof a pointer | Memory Safety | Medium |
| 3.5 | Null-check pointers before use | Memory Safety | High |
| 3.6 | String storage must have sufficient space | Memory Safety | High |
| 4.1 | Validate external input legitimacy | Input Validation | High |
| 4.2 | Validate memory operation length | Input Validation | High |
| 5.1 | Check whether resource allocation succeeded | Resource Management | High |
| 5.2 | Resource leak protection | Resource Management | High |
| 5.3 | Use new/delete in matching pairs | Resource Management | High |
| 5.4 | Custom new/delete must be defined in pairs | Resource Management | Medium |
| 5.5 | Error handling for the new operator | Resource Management | High |
| 5.6 | Avoid delete this | Resource Management | High |
| 6.1 | Explicitly specify file permissions | File IO | Medium |
| 6.2 | Normalize file paths | File IO | High |
| 6.3 | Do not create temporary files in shared directories | File IO | Medium |
| 7.1 | Protect access to critical resources | Concurrency Safety | High |
| 8.1 | Use secure functions instead of dangerous functions | Secure Functions | High |
| 8.2 | Correctly set the destMax parameter of secure functions | Secure Functions | High |
| 8.3 | Do not wrap secure functions | Secure Functions | Medium |
| 8.4 | Do not rename secure functions with macros | Secure Functions | Medium |
| 8.5 | Do not customize secure functions | Secure Functions | Medium |
| 8.6 | Check return values of secure functions | Secure Functions | High |
| 9.1 | delete/move/swap should have noexcept | Classes & Objects | Medium |
| 9.2 | Do not perform bitwise operations on non-trivially copyable objects | Classes & Objects | Medium |
| 10.1 | Do not create std::string from a null pointer | Standard Library | High |
| 10.2 | Do not save c_str/data pointers | Standard Library | Medium |
| 10.3 | Avoid using atoi/atol/atof | Standard Library | Medium |
| 10.4 | Do not store sensitive information in std::string | Standard Library | High |
| 10.5 | format parameters must not be externally controlled | Standard Library | High |
| 10.6 | Do not use exit functions and atexit | Standard Library | Medium |
| 10.9 | Do not use rand to generate secure random numbers | Standard Library | Medium |
| 10.10 | Zero out sensitive information after use | Standard Library | High |
| 10.11 | Append struct fields at the end | Standard Library | Medium |
| 10.12 | Consider compatibility for interface changes | Standard Library | Medium |
| 11.1 | LOG API must not be passed null pointers | LOG API Safety | High |
| 11.2 | LOG API parameter count and order must match format placeholders | LOG API Safety | High |
| 11.3 | LOG API parameter types must match format specifiers | LOG API Safety | High |
| 11.4 | LOG API must not be passed pointers to released memory | LOG API Safety | High |
| 11.5 | LOG message English should be grammatically correct and clear | LOG API Specification | Low |

---

### 1. General Principles

##### Rule 1.1 Ensure static type safety

C++ should be statically type-safe, which reduces runtime errors and improves code robustness. However, the following features of C++ can undermine static type safety and must be handled carefully:

- Unions
- Type casting
- Narrowing conversions
- Type decay
- Range errors
- void* type pointers

You can constrain the use of these features or use new C++ features such as std::variant (C++17), std::span (C++20), etc. to address these issues and improve the robustness of C++ code.

##### Rule 1.2 Ensure memory safety

In C++, memory is entirely controlled by the programmer, so memory safety must be ensured when operating on memory to prevent memory errors:

- Out-of-bounds memory access
- Accessing memory after it has been released
- Dereferencing null pointers
- Uninitialized memory
- Passing references or pointers to local variables outside the function or to other threads
- Allocated memory or resources not released in a timely manner

It is recommended to use safer C++ features, such as RAII, references, and smart pointers, to improve code robustness.

##### Rule 1.3 Do not use compiler "undefined behavior"

Follow the ISO C++ standard; behaviors undefined by the standard must not be used. Features implemented by compilers or extension features provided by compilers such as GCC should also be used with caution, as these features reduce code portability.

---

### 2. Numeric Operation Safety

#### 2.1 Ensure signed integer operations do not overflow

**[Description]**
Signed integer overflow is undefined behavior. For security reasons, when signed integer values from external data are used in the following scenarios, ensure that the operation does not cause overflow:

- Integer operands of pointer arithmetic (pointer offset values)
- Array indices
- Lengths of variable-length arrays (and length arithmetic expressions)
- Lengths of memory copies
- Parameters of memory allocation functions
- Loop condition checks

When performing operations on integer types with lower precision than int, integer promotion must be considered. Programmers must also master integer conversion rules, including implicit conversion rules, to design safe arithmetic operations.

**Multiplication example (int32_t multiplication overflow):**

```cpp
// Incorrect — two int32_t values multiplied, result may exceed int32_t range
int32_t calcHeightAlign = GetAlignedSize(...);  // aligned height, can reach 65536
int32_t calcWidth = GetWidth(...);              // width, can reach 65536
int32_t size = calcHeightAlign * calcWidth;     // 65536 × 65536 = 4,294,967,296 overflow!

// Correct — promote to int64_t before multiplication
int64_t size = static_cast<int64_t>(calcHeightAlign) * calcWidth;
```

**Negation example (INT64_MIN negation overflow, red-line issue):**

```cpp
// Incorrect — when delta is INT64_MIN, -delta overflows
int64_t delta = input2 - input1;       // may be INT64_MIN = -9223372036854775808
int64_t absDelta = -delta;             // -(-9223372036854775808) = 9223372036854775808 > INT64_MAX!
// Signed integer overflow is undefined behavior (C++ red-line)

// Correct — convert to unsigned type before computing absolute value
uint64_t absDelta = (delta < 0) ? -static_cast<uint64_t>(delta) : static_cast<uint64_t>(delta);
```

**Multi-dimension multiplication example (multi-dim shape consecutive multiplication overflow):**

```cpp
// Incorrect — multi-dim shape multiplied as int32_t, easily overflows
int32_t totalSize = dim0 * dim1 * dim2 * dim3 * dim4;
// dim0=1024, dim1=1024, dim2=128, dim3=64 → product ≈ 8.6 × 10^9 > INT32_MAX

// Correct — use int64_t and promote early
int64_t totalSize = static_cast<int64_t>(dim0) * dim1 * dim2 * dim3 * dim4;
```

#### 2.2 Ensure unsigned integer operations do not wrap around

**[Description]**
Computations involving unsigned operands never overflow, because a result that is outside the representable range of the unsigned integer type is reduced modulo (the maximum value representable by the result type + 1). This behavior is more informally called unsigned integer wraparound.

**Multiplication example (uint32_t multiplication wraparound then cast to uint64_t — value already wrong):**

```cpp
// Incorrect — multiplication done in uint32_t, wraparound occurs before cast to uint64_t, cannot recover
uint32_t blockSize = 65536;    // from external config
uint32_t strideKV = 65536;     // from external config
uint64_t result = blockSize * strideKV;
// blockSize * strideKV computed in uint32_t space: 65536 × 65536 = 4,294,967,296 > UINT32_MAX
// Actual result: (65536 × 65536) mod 2^32 = 0 → wrapped 0 then cast to uint64_t = 0

// Correct — promote at least one operand to uint64_t before multiplication
uint64_t result = static_cast<uint64_t>(blockSize) * strideKV;
```

**Subtraction example (uint32_t subtraction wraparound — result used as array index):**

```cpp
// Incorrect — aivIdx * singleCoreSize may exceed totalOutputSize, subtraction wraps around
uint32_t tailSize = totalOutputSize - aivIdx * singleCoreSize;
// totalOutputSize=100, aivIdx=47, singleCoreSize=3:
//   47 × 3 = 141, 100 - 141 computed as uint32_t = 4294967255 (wraparound)
//   tailSize mistaken as valid size, subsequent copy of 4GB data → out-of-bounds crash

// Correct — check magnitude first, or use int64_t intermediate result
int64_t tailSizeSigned = static_cast<int64_t>(totalOutputSize) -
                         static_cast<int64_t>(aivIdx) * singleCoreSize;
uint32_t tailSize = (tailSizeSigned > 0) ? static_cast<uint32_t>(tailSizeSigned) : 0;
```

**Mixed-type example (size_t and int64_t mixed arithmetic — negative wraps to huge value):**

```cpp
// Incorrect — N_ALIGN is size_t constant (unsigned), numIters is int64_t
// Per C++ integer promotion rules int64_t → size_t, negative becomes huge positive
constexpr size_t N_ALIGN = 128;
int64_t normSize = N_ALIGN * DOUBLE_SIZE * numIters * T * n0;
// If numIters is negative, promoted to size_t wraps to ~2^64-127 level huge value
// All subsequent calculations are wrong

// Correct — unify to signed type
constexpr int64_t N_ALIGN = 128;
int64_t normSize = N_ALIGN * DOUBLE_SIZE * numIters * T * n0;
```

#### 2.3 Ensure division and remainder operations do not cause divide-by-zero errors

**[Description]**
When the second operand of an integer division or remainder operation is 0, the program produces undefined behavior. Therefore, ensure that integer division and remainder operations do not cause divide-by-zero errors.

**[Review Strategy]**

**Step 1 — Identify division/remainder operations**

Scan all `/` and `%` operators (including `CeilDiv`/`CeilDivide` utility function calls) in the code and extract the divisor expression.

**Step 2 — Classify divisor source**

| Divisor Source | Identification | Trust Level |
|---------|---------|---------|
| Compile-time constant | `constexpr` declaration, literal | Non-zero → auto PASS |
| External input | Config parameter, interface argument, external data | Strict: must have guard |
| Runtime computed value | Internal multi-step computed intermediate value | Strict: must have guard |

**Step 3 — Judge by source**

- **Compile-time constant**: Non-zero → PASS. Zero → FAIL.
- **External input**: Must have one of the valid guard patterns → no guard → FAIL.
- **Runtime computed value**: Must have a valid guard, or be traceable to a non-zero compile-time constant → otherwise FAIL.

**[Valid Guard Patterns]**

The following 4 patterns are considered valid zero-value guards (any one present → PASS):

| Pattern | Name | Code Form |
|------|------|---------|
| A | if-guard+return | `if (div == 0) return;` |
| B | std::max floor | `safeDiv = std::max(div, 1U)` |
| C | Ternary operator | `safe = (div > 0) ? div : 1` |
| D | zero-flag+skip | `if (div==0) flag=true; if(!flag) { a/b }` |

> `CeilDiv(a, b)` / `CeilDivide(a, b)` and similar utility functions with standard implementation `(a + b - 1) / b` **do not provide zero-value protection themselves**; their divisor parameter must still be classified and judged per the above sources.

**[Validation Examples]**

```cpp
// ✅ Compile-time constant divisor — auto PASS
constexpr uint32_t BLOCK = 32;
uint32_t blocks = totalSize / BLOCK;  // no guard needed

// ✅ External input divisor — has zero-value guard
if (tileSize == 0) {
    HIXL_LOGE(INVALID_PARAM, "tileSize is 0");
    return FAILED;
}
uint32_t loops = totalSize / tileSize;

// ✅ Runtime computed value divisor — std::max floor
uint32_t safeDivisor = std::max(computedDivisor, 1U);
uint32_t result = totalSize / safeDivisor;

// ❌ External input divisor — no guard
uint32_t loops = totalSize / tileSize;  // tileSize from external input, no zero check
```

---

### 3. Memory and Pointer Safety

#### 3.1 Do not use uninitialized variables

Here, variables refer to local dynamic variables and also include memory blocks allocated on the heap. Because their initial values are unpredictable, reading their values without effective initialization is prohibited.

```cpp
void foo(...)
{
    int data;
    bar(data); // Error: used without initialization
    ...
}
```

#### 3.2 Variables pointing to resource handles or descriptors must be assigned a new value immediately after the resource is released

**[Description]**
Variables pointing to resource handles or descriptors include pointers, file descriptors, socket descriptors, and other variables pointing to resources.

Take pointers as an example. After a pointer successfully allocates a block of memory, if the pointer is not immediately set to NULL or assigned a new object after the memory is released, the pointer becomes a dangling pointer. Operating on a dangling pointer again may cause double-free or access to released memory, creating security vulnerabilities.

**[Correct Code Example]**

```cpp
int foo(void)
{
    SomeStruct *msg = NULL;
    ... // Initialize msg->type, allocate memory for msg->body

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

#### 3.3 When external data is used as an array index, ensure it is within the array size range

**[Scenarios Requiring Validation]**

| Validation Condition | Parameter Source | Code Pattern |
|---------|---------|----------|
| External input as index | Config parameter, interface argument | `if (index >= size) { return; }` |
| Dynamically computed offset | Runtime computed value | Boundary check logic |
| Accumulated diff boundary | `seqLen[bIdx] - seqLen[bIdx-1]` | `if (bIdx > 0) { ... } else { ... }` |

**[Trusted Scenarios]**

The following cases do not require additional validation:

| Scenario | Code Pattern | Trust Reason |
|------|---------|---------|
| Index within loop bounds | `arr[i]` inside `for (i = 0; i < bound; i++)` | Loop condition guarantees index is in range |
| Compile-time constant index | `constexpr` or literal | Fixed range at compile time |

**[Description]**
When external data is used as an array index to access memory, the data size must be strictly validated to ensure the array index is within the valid range; otherwise, serious errors will occur.

**[Correct Code Example]**

```cpp
#define DEV_NUM 10
static Dev devs[DEV_NUM];

int set_dev_id(size_t index, int id)
{
    if (index >= DEV_NUM) {
        ... // error handling
    }
    devs[index].id = id;
    return 0;
}
```

#### 3.4 Do not use sizeof on a pointer variable to obtain the array size

**[Description]**
Using sizeof on a pointer as if it were an array causes the actual result to differ from expectations.

**[Incorrect Code Example]**

```cpp
char path[MAX_PATH];
char *buffer = (char *)malloc(SIZE);
...
(void)memset(path, 0, sizeof(path));
// sizeof does not match expectations; the result is the size of the pointer itself, not the buffer size
(void)memset(buffer, 0, sizeof(buffer));
```

**[Correct Code Example]**

```cpp
char path[MAX_PATH];
char *buffer = (char *)malloc(SIZE);
...
(void)memset(path, 0, sizeof(path));
(void)memset(buffer, 0, SIZE); // Use the allocated buffer size
```

#### 3.5 Pointers must be null-checked before use

**[Description]**
Dereferencing a null pointer causes the program to produce undefined behavior, typically causing the program to terminate abnormally.

- Pointer variables must be properly initialized and assigned a value before use; accessing a null pointer is strictly prohibited
- Any operation on the address space represented by a pointer must ensure the validity of the space
- After the memory pointed to by a pointer is released, the caller must explicitly set the pointer to NULL to prevent "wild pointers"

#### 3.6 Ensure string storage has sufficient space to hold character data and the null terminator

**[Description]**
Copying data into a buffer that is too small to hold it causes a buffer overflow.

---

### 4. Input Validation

#### 4.1 External input data must be validated for legitimacy

**[Validation Key Points]**

- External pointer arguments must be null-checked before use
- External input data must be validated for legitimacy with correct validation range
- Boundary interfaces must validate the legitimacy of passed-in addresses to prevent arbitrary address read/write
- Parameters must be validated for legitimacy to prevent array out-of-bounds access
- Address offsets must be validated to prevent arbitrary address read/write
- External parameters involved in loop and recursion condition computations must be strictly validated for boundaries and termination conditions
- When file paths come from external data, they must be validated for legitimacy

**[Validation Examples]**

```cpp
// Validate pointer is non-null
if (context == nullptr) {
    HIXL_LOGE(INVALID_PARAM, "context is null");
    return FAILED;
}

// Validate parameter range
if (headDim == 0) {
    HIXL_LOGE(INVALID_PARAM, "headDim is 0");
    return FAILED;
}

// Validate parameter combination existence
if (pointer == nullptr) {
    HIXL_LOGE(INVALID_PARAM, "%s should not be null", name.c_str());
    return FAILED;
}
```

#### 4.2 When external input is used as the copy length for memory operation functions, its legitimacy must be validated

**[Description]**
Copying data into memory whose capacity is insufficient to hold the data causes a buffer overflow. The size of the data to be copied must be limited based on the target capacity, or the target capacity must be ensured to be large enough to hold the data to be copied.

---

### 5. Resource Management

#### 5.1 After resource allocation, you must check whether it succeeded

**[Description]**
If the allocation of resources such as memory, objects, streams, and notify fails, subsequent operations carry the risk of undefined behavior.

**[Correct Code Example]**

```cpp
struct tm *make_tm(int year, int mon, int day, int hour, int min, int sec)
{
    struct tm *tmb = (struct tm *)malloc(sizeof(*tmb));
    if (tmb == NULL) {
        ... // error handling
    }
    tmb->year = year;
    ...
    return tmb;
}
```

#### 5.2 Resource leaks (memory, handles, locks, etc.)

**[Description]**

- Resource allocation and release must be matched, including: memory malloc/free/alloc_page/free_page, lock lock/unlock, file open/close, etc.
- Before releasing struct/class/array/various data container pointers, member pointers must be released first
- External interfaces that allocate resources but do not release them cause resource leaks, leading to denial of service
- When C++ catches exceptions, ensure program consistency is restored; the RAII pattern is recommended to ensure resources are automatically released when an exception occurs

#### 5.3 Use new and delete in matching pairs; use new[] and delete[] in matching pairs

#### 5.4 Custom new/delete operators must be defined in pairs, and their behavior must be consistent with the replaced operators

#### 5.5 Handle memory allocation errors of the new operator appropriately

#### 5.6 Avoid delete this operations

---

### 6. File IO Safety

#### 6.1 When creating files, appropriate file access permissions must be explicitly specified

**[Description]**
When creating files, if appropriate access permissions are not explicitly specified, unauthorized users may access the file, causing risks such as information leakage, file data tampering, and injection of malicious code into the file.

#### 6.2 File paths must be normalized before use

**[Description]**
When file paths come from external data, they must be validated for legitimacy. However, direct validation is prohibited; the correct approach is to normalize the path before validation.

**[Correct Code Example]**

```cpp
char *file_name = get_msg_from_remote();
...
sprintf(untrust_path, "/tmp/%s", file_name);
char path[PATH_MAX] = {0};
if (realpath(untrust_path, path) == NULL) {
    ... // handle error
}
if (!is_valid_path(path)) { // Check whether the file location is correct
    ... // handle error
}
char *text = read_file_content(path);
```

#### 6.3 Do not create temporary files in shared directories

**[Description]**
A program's temporary files should be exclusively owned by the program itself. Any practice of placing temporary files in shared directories will allow other shared users to obtain additional information about the program, causing information leakage.

---

### 7. Concurrency Safety

#### 7.1 Access to critical resources must be protected

**[Description]**
Protection of critical resources is a key issue in multi-threaded programming. Unreasonable access can lead to data inconsistency, memory trampling, program crashes, and deadlocks. Mutex locks or thread-safe functions must be used for protection. The following scenarios require particular attention:

- Global variables shared by multiple threads
- Data structures accessed by both threads and interrupts
- C functions that are not thread-safe called in a multi-threaded environment
- Variables/data structures accessed by both user-mode threads and signal handlers

---

### 8. Secure Function Usage

#### 8.1 Use secure functions from the community-provided secure function library; do not use dangerous memory operation functions

| Function Category | Dangerous Function | Secure Replacement |
|---------|---------|------------|
| Memory copy | memcpy or bcopy | memcpy_s |
| Memory copy | memmove | memmove_s |
| String copy | strcpy | strcpy_s |
| String concatenation | strcat | strcat_s |
| Formatted output | sprintf | sprintf_s |
| Formatted output | snprintf | snprintf_s |
| Formatted input | scanf | scanf_s |
| Memory initialization | memset | memset_s |

#### 8.2 Correctly set the destMax parameter in secure functions

#### 8.3 Do not wrap secure functions

#### 8.4 Do not rename secure functions with macros

```cpp
// Incorrect example
#define XXX_memcpy_s memcpy_s
#define SEC_MEM_CPY memcpy_s
```

#### 8.5 Do not customize secure functions

#### 8.6 Return values of secure functions must be checked and handled correctly

In principle, if secure functions are used, their return values must be checked. If the return value != EOK, the function should generally return immediately and must not continue execution.

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

### 9. Class and Object Safety

#### 9.1 The delete operator, move constructor, move assignment operator, and swap function should have noexcept declarations

#### 9.2 Do not perform bitwise operations on non-trivially copyable objects

---

### 10. Standard Library Safety

#### 10.1 Do not create std::string from a null pointer


#### 10.2 Do not save the pointers returned by the `c_str` and `data` member functions of std::string

#### 10.3 Avoid using atoi, atol, atoll, and atof functions

#### 10.4 Do not use std::string to store sensitive information

#### 10.5 When calling formatted input/output functions, the format parameter must not be controlled by external data

#### 10.6 Do not use program and thread exit functions and the atexit function

#### 10.9 Do not use the rand function to generate pseudo-random numbers for security purposes

The C standard library rand() function generates pseudo-random numbers. Please use /dev/random to generate random numbers.

#### 10.10 Sensitive information in memory must be zeroed out immediately after use

Passwords, keys, and other sensitive information must be zeroed out immediately after use to prevent attackers from obtaining them.

#### 10.11 When adding fields to an external struct interface, they must be appended at the end of the struct

To maximize compatibility at the ABI level, when adding new fields to an external struct interface, they must be appended at the end of the struct.

#### 10.12 Changes to external interfaces or data structures must consider compatibility

Changes to external interfaces, interface parameters, return values, data structures, message fields, etc. will cause version compatibility issues. Changes are not recommended unless necessary.

---

### 11. LOG API Safe Usage

When using formatting LOG macros such as `HIXL_LOGE` / `HIXL_LOGI` / `HIXL_LOGD` / `HIXL_LOGW`, improper parameter usage can cause garbled output at best, or segmentation faults (SIGSEGV) at worst. The following 4 rules are mandatory, and 1 is a quality recommendation.

LOG macro signature (standard call form in business code):

```cpp
HIXL_LOGE(ERROR_CODE, "format string %s %ld", arg1, arg2);
HIXL_LOGI("format string  %s %ld", arg1, arg2);
HIXL_LOGD("format string %lu", arg1);
HIXL_LOGW("format string %lu", arg1);
```

---

#### 11.1 LOG API must not be passed null pointers as string parameters

**[Problem Description]**

`%s` dereferences the passed-in pointer. If the pointer is `nullptr`, it will access address 0 (protected by the OS), causing a segmentation fault.

**Incorrect example**

```cpp
auto channel = channel_manager->GetChannel(type, id);
HIXL_LOGI("transfer_count: %d", channel->GetTransferCount());
// Risk: channel is not null-checked before calling a member function
```

**Correct example**

```cpp
auto channel = channel_manager->GetChannel(type, id);
if (channel == nullptr) {
  HIXL_LOGE(FAILED, "channel is nullptr");
  return FAILED;
}
HIXL_LOGI("transfer_count: %d", channel->GetTransferCount());
```

---

#### 11.2 LOG API parameter count and order must match format placeholders

**[Problem Description]**

The format placeholders of LOG macros and the actual parameters must satisfy consistency in two dimensions:

1. **Count consistency**: When the number of parameters is less than the number of placeholders, the LOG macro reads garbage values from the stack to fill the missing parameters. If the garbage values are interpreted as invalid pointers (`%s`/`%p`), illegal memory access is triggered
2. **Order correspondence**: When parameter order does not correspond to format specifier positions (e.g., `%s` position receives an integer), the integer is treated as an address to read a string → **segmentation fault (SIGSEGV)**

> **⚠️ Do not analyze LOG calls by grep alone.** LOG statements often span multiple lines and may be nested inside outer macros. After a grep hit, you **must Read at least 10 lines before and after** to obtain the complete format string and all parameters; otherwise the analysis is based on a truncated incomplete call and the conclusion is invalid. Multi-line string concatenation (`"a" "b"`) must be merged before parsing.

**Count mismatch example**

```cpp
// Refer to the multi-parameter log scenario in hixl_engine.cc
// 3 placeholders, but only 2 parameters passed
HIXL_LOGI("[HixlEngine] Disconnection started, local_engine:%s, remote_engine:%s, timeout:%d ms",
           local_engine_.c_str(), remote_engine.GetString());  // Missing timeout_in_millis; stack data is incorrectly read
```

```cpp
// ✅
HIXL_LOGI("[HixlEngine] Disconnection started, local_engine:%s, remote_engine:%s, timeout:%d ms",
           local_engine_.c_str(), remote_engine.GetString(), timeout_in_millis);
```

**Order mismatch example**

```cpp
// ❌ Count=5, format specifiers=5, but parameters at positions 1 and 3 are swapped
//   Format specifiers: %u(1) %u(2) %s(3) %u(4) %u(5)
//   Parameters:        inputName.c_str()(1) ... d0Size/NUM8(3) ...
//   → Position 1: %u receives const char*, Position 3: %s receives uint → segmentation fault
HIXL_LOGE(FAILED, "...kvCache(%u)...%s(%u)...",
    inputName.c_str(), tempD0/NUM8, d0Size/NUM8, tempD0, d0Size);
// ✅ Parameter order corresponds to format specifiers position by position
HIXL_LOGE(FAILED, "...kvCache(%u)...%s(%u)...",
    tempD0/NUM8, d0Size/NUM8, inputName.c_str(), tempD0, d0Size);
```

---

#### 11.3 LOG API parameter types must match the format specifiers

**[Problem Description]**

When type sizes do not match, the LOG macro truncates or reads excess bytes according to the specifier width, causing all subsequent parameters to be misaligned.

**Incorrect example**

```cpp
HIXL_LOGE(FAILED, "Failed to transfer data1[size = %d], data2[size = %d]", size1, size2);
// Error: size1 & size2 are both uint64_t; %d reads only 4 bytes, causing all subsequent parameters to be misaligned
```

**Correct example**

```cpp
// Correct way to write business code
HIXL_LOGE(FAILED, "Failed to transfer data1[size = %lu], data2[size = %lu]", size1, size2);
```

**HIXL Common Type and Specifier Mapping**

| Type | Correct Specifier | Common Error | Consequence |
|------|---------|---------|------|
| `int64_t` | `%ld` | `%d` | Truncated to 32-bit, subsequent parameters misaligned |
| `uint64_t` | `%lu` | `%d`, `%u` | Truncated to 32-bit, subsequent parameters misaligned |
| `uint32_t` | `%u` | `%d` | Large values displayed as negative |
| `size_t` | `%zu` | `%d`, `%u` | Truncated on 64-bit systems |
| `bool` | `%s` + ternary expression | `%d` | Portability issues |
| `const char*` | `%s` | `%d`, `%x` | Undefined behavior |
| `void*` | `%p` | `%x` | Non-portable |

```cpp
// Correct way to log a bool
HIXL_LOGI("Channel is disconnecting: %s", channel->IsDisconnecting() ? "true" : "false");
```

---

#### 11.4 LOG API must not be passed pointers to released memory

**[Problem Description]**

If manually managed heap memory (`new` / `malloc`) is still passed to `%s` after being released, the behavior is undefined and most likely triggers a segmentation fault. Typical scenario: resources are released uniformly at the end of a function, but the LOG statement is written after the release.

**Incorrect example**

```cpp
char* errMsg = new char[256];
snprintf(errMsg, 256, "call func failed, ret=%ld", ret);
delete[] errMsg;
HIXL_LOGE(FAILED, "error: %s", errMsg);   // Wild pointer, already released
```

**Correct example**

```cpp
char* errMsg = new char[256];
snprintf(errMsg, 256, "call func failed, ret=%ld", ret);
HIXL_LOGE(FAILED, "error: %s", errMsg);   // Log first
delete[] errMsg;
errMsg = nullptr;
```

---

#### Recommendation 11.5 LOG message English should be grammatically correct and clear

**[Problem Description]**

LOG messages are the first-hand clue for troubleshooting. Grammatical errors or ambiguous logs significantly increase the time cost of issue localization.

**Review Key Points**:
- Subject-verb agreement, consistent tense (LOG messages conventionally use simple present or past tense)
- Avoid mixing Chinese and English (except for variable names)
- Avoid meaningless placeholders (e.g., "error error", "fail to fail")
- Key values should be included in the message, not relying solely on format specifiers

**Reminder Examples**

```cpp
// "is not support" → "is not supported" (high-frequency error pattern in the repo)
HIXL_LOGE(op_name, "scale shape is not support");          // → is not supported

// "do not support" → "does not support" (subject-verb disagreement)
HIXL_LOGE(opName_, "key layout do not support PA_BSND.");  // → does not support

// Spelling error
HIXL_LOGE(opName_, "cu_seqlens_q's dtype msut be DT_INT32."); // msut → must

// Missing subject
HIXL_LOGD("Not support BN2S2.");                           // → BN2S2 is not supported
```

> **Review Level**: Mark as SUSPICIOUS only, do not mark as FAIL.
