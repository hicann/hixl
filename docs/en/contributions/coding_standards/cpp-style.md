# CANN C++ Code Style Specifications

>  **Applicable Scope**: The code style specifications apply to all C++ code to improve code readability and consistency.

## Rule List

| Rule No. | Rule Name | Category | Severity |
|---------|---------|------|---------|
| 1.1 | C++ files use lowercase + underscore naming | Naming | Medium |
| 1.2 | Function naming uses UpperCamelCase | Naming | Medium |
| 1.3 | Type naming uses UpperCamelCase | Naming | Medium |
| 1.4 | Variable naming uses snake_case | Naming | Medium |
| 1.5 | Macros and enum values use all-uppercase with underscores | Naming | Medium |
| 1.6 | Compile-time constants use k-prefix UpperCamelCase | Naming | Medium |
| 2.1 | Line width must not exceed 120 characters | Formatting | Low |
| 2.2 | Use space indentation, 2 spaces per level | Formatting | Medium |
| 2.3 | `&` and `*` follow the variable name | Formatting | Low |
| 2.4 | if statements must use braces | Formatting | Medium |
| 2.5 | for/while loops must use braces | Formatting | Medium |
| 2.6 | Place operators at the end of the line when wrapping expressions | Formatting | Low |
| 2.7 | Use attached (Attach) brace style | Formatting | Medium |
| 2.8 | Multiple variable definitions on one line are not allowed | Formatting | Low |
| 2.9 | Arrange blank lines reasonably to keep code compact | Formatting | Low |
| 3.1 | File header comments must include copyright notice | Comments | Medium |
| 3.2 | Trailing comments use `//`, with a space between code and comment | Comments | Low |
| 3.3 | Do not use TODO/TBD/FIXME comments | Comments | Medium |
| 3.4 | Do not write function header comments with empty formatting | Comments | Low |
| 3.5 | Delete unused code directly; do not comment it out | Comments | Medium |

## Description

These specifications are based on the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html), referencing the MindSpore community and Huawei general coding specifications, and consolidated with industry consensus. Developers participating in CANN open-source community projects must first follow these specifications; all other aspects follow the Google C++ Style Guide.

If there is an objection to a rule, it is recommended to submit an issue with the rationale. After review and approval by the CANN operations team, the change may be accepted and take effect.

## Scope of Application

Code style review of CANN-related open-source repositories.

---

### 1. Naming

#### Naming Styles

**CamelCase**: mixes uppercase and lowercase letters, with words joined together and separated by capitalizing the first letter of each word. Depending on whether the first letter of the joined result is capitalized, it is further divided into: UpperCamelCase and lowerCamelCase.

**snake_case**: all words are lowercase and separated by underscores, for example, `table_name`.

| Type | Naming Style |
| ---------------------------------------- | --------- |
| Class types, struct types, enum types, union types and other type definitions, scope names | UpperCamelCase |
| Functions (including global functions, scope functions, member functions) | UpperCamelCase |
| Compile-time constants (constexpr, or const initialized with literals/compile-time constant expressions, in any scope) | k-prefix UpperCamelCase |
| Global variables (including variables in global and namespace scope, class static variables), local variables, function parameters, member variables of classes, structs, and unions | snake_case |
| Macros, enum values, goto labels | All-uppercase, separated by underscores |

Note:
**Constants** in the table above refer to const/constexpr variables of basic data types, enums, and string types whose values are determined at compile time (constexpr, or const initialized with literals/compile-time constant expressions; in any scope); they do not include arrays and other types of variables. const variables initialized from function calls or runtime data are not constants and are named as ordinary variables.
**Variables** in the table above refer to all variables other than constant definitions, all using the snake_case style.


##### Rule 1.1 C++ files use lowercase + underscore naming, ending with .cpp; header files end with .h

There are other suffix conventions in the industry:
- Header files: .hh, .hpp, .hxx
- cpp files: .cc, .cxx, .c

If your current project team uses a specific suffix, you may continue to use it, but please keep the style consistent.
However, for this document, we use .h and .cpp as the default suffixes.

##### Rule 1.2 Function naming uniformly uses UpperCamelCase, generally adopting a verb or verb-object structure

```cpp
class List {
 public:
  void AddElement(const Element &element);
  Element GetElement(const unsigned int index) const;
  bool IsEmpty() const;
};

namespace Utils {
void DeleteUser();
}
```

Functions implementing interface contracts required by the standard library, third-party libraries, or serialization frameworks (such as `lock`/`try_lock`/`unlock`, `to_json`/`from_json`) may keep the names required by those interfaces and are not bound by this rule.

##### Rule 1.3 Type naming uses UpperCamelCase

All type names—classes, structs, unions, type aliases (using/typedef), enums—use the same convention, for example:
```cpp
// classes, structs and unions
class UrlTable { ...
struct UrlTableProperties { ...
union Packet { ...
// type aliases
using PropertiesMap = std::map<std::string, UrlTableProperties *>;
// enums
enum UrlTableErrors { ...
```

For namespace naming, UpperCamelCase is recommended:
```cpp
// namespace
namespace FileUtils {}
```

##### Rule 1.4 General variable naming uses snake_case, including global variables, function parameters, local variables, and member variables

```cpp
std::string table_name;  // Good: recommended style
std::string tableName;   // Bad: lowerCamelCase is prohibited
std::string tablename;   // Bad: no word separator
std::string path;        // Good: when there is only one word, snake_case is all lowercase
```

Global variables should have a 'g_' prefix; static variable naming does not need a special prefix.
Global variables should be used as little as possible and used with special care, so a prefix is added for visual prominence, prompting developers to be more careful with these variables.
- Global static variables are named the same as global variables.
- Static variables inside functions are named the same as ordinary local variables.
- Static member variables of a class are the same as ordinary member variables.

```cpp
int g_active_connect_count;

void Func() {
  static int packet_count = 0;
  ...
}
```

Member variables of a class are named using snake_case with a trailing underscore; public members of plain-data structs may omit the trailing underscore (consistent with Google style), keeping the style consistent within the same struct.

```cpp
class Foo {
 private:
  std::string file_name_;  // Class member: add a _ suffix
};

struct Point {
  int x;  // Plain-data struct member: suffix may be omitted
  int y;
};
```

##### Rule 1.5 Macros and enum values use all-uppercase with underscores

Macros, enum values, and goto labels use all-uppercase with underscores.

```cpp
// Only an example of macro naming; using macros for such functionality is not recommended
#define MAX(a, b) (((a) < (b)) ? (b) : (a))

enum TintColor {  // Note: the enum type name uses UpperCamelCase; its values below are all-uppercase with underscores
  RED,
  DARK_RED,
  GREEN,
  LIGHT_GREEN
};
```

##### Rule 1.6 Compile-time constants use k-prefix UpperCamelCase

Compile-time constants (constexpr, or const initialized with literals/compile-time constant expressions), regardless of scope (global, namespace, class static members, or function-local), uniformly use the k prefix with UpperCamelCase; const variables initialized from function calls or runtime data are not constants and are named as ordinary variables (Rule 1.4); non-static const member variables of classes follow the member variable naming rule (snake_case with a trailing underscore).

```cpp
int Func(...) {
  constexpr unsigned int kBufferSize = 100;  // Compile-time constant: k-prefix UpperCamelCase
  char *buffer = new char[kBufferSize];
  const int saved_errno = errno;  // Runtime-initialized const: named as an ordinary snake_case variable
  ...
}

namespace Utils {
constexpr unsigned int kDefaultFileSizeKb = 200;  // Global compile-time constant
}
```

---

### 2. Formatting

##### Recommendation 2.1 Line width must not exceed 120 characters

It is recommended that each line not exceed 120 characters. If it exceeds 120 characters, choose a reasonable way to wrap the line.

Exceptions:
- If a comment line contains a command or URL longer than 120 characters, it may be kept on one line for easy copying, pasting, and searching with grep;
- #include statements containing long paths may exceed 120 characters, but should also be avoided as much as possible;
- Error messages in preprocessing may exceed one line.
Preprocessing error messages on one line are easier to read and understand, even if they exceed 120 characters.
```cpp
#ifndef XXX_YYY_ZZZ
#error Header aaaa/bbbb/cccc/abc.h must only be included after xxxx/yyyy/zzzz/xyz.h, because xxxxxxxxxxxxxxxxxxxxxxxxxxxxx
#endif
```

##### Rule 2.2 Use spaces for indentation, 2 spaces per indentation level

Only spaces may be used for indentation, 2 spaces per indentation level. Using Tab characters for indentation is not allowed.
Almost all modern integrated development environments (IDEs) support configuring Tab characters to automatically expand to 2 spaces; please configure your IDE to support using spaces for indentation.
Code inside a namespace is not indented (consistent with `NamespaceIndentation: None` in `.clang-format`).

##### Rule 2.3 When declaring pointer or reference variables or parameters, `&` and `*` follow the variable name, with a space on the other side

```cpp
char *c;
const std::string &str;
```

##### Rule 2.4 if statements must use braces

We require all if statements to use braces, even if there is only one statement.
Reasons:
- Code logic is intuitive and easy to read;
- It is less error-prone when adding new code to existing conditional statements;
- When using function-like macros in if statements, braces provide protection against errors (if braces are omitted in the macro definition).

```cpp
// Even if the if branch has only one line of code, braces must be used
if (cond) {
  single line code;
}
```

> **Note**: clang-format does not automatically add braces for single statements (`AllowShortIfStatementsOnASingleLine: true` permits the single-line if form); this rule relies on code review or clang-tidy (`readability-braces-around-statements`).

##### Rule 2.5 for/while and other loop statements must use braces

Similar to conditional expressions, we require for/while loop statements to use braces, even if the loop body is empty or the loop has only one statement.
```cpp
for (int i = 0; i < some_range; i++) {  // Good: braces used
  DoSomething();
}
```
```cpp
while (condition) {  // Good: the loop body is empty, braces still used
}
```

> **Note**: as in Rule 2.4, clang-format does not automatically add braces for loops (`AllowShortLoopsOnASingleLine: true` permits the single-line loop form); this relies on code review.

##### Rule 2.6 Expression wrapping should maintain consistency; operators go at the end of the line

When a long expression does not meet the line width requirement, it should be wrapped at an appropriate place. Generally, break after a lower-precedence operator or connector, with the operator or connector placed at the end of the line.
Placing the operator or connector at the end of the line indicates "not finished, more to follow."
Example:
// Assume the first line below no longer meets the line width requirement
```cpp
if ((current_value > threshold) &&  // Good: after wrapping, the logical operator is placed at the end of the line
    some_condition) {
  DoSomething();
  ...
}

int result = really_long_variable_name1 +  // Good
             really_long_variable_name2;
```
After wrapping an expression, continuation lines are aligned with the first operand (consistent with `AlignOperands: true` in `.clang-format`). See the example below:

```cpp
int sum = long_variable_name1 + long_variable_name2 + long_variable_name3 + long_variable_name4 + long_variable_name5 +
          long_variable_name6;  // Good: continuation aligned with the first operand
```

##### Rule 2.7 Use attached (Attach) brace style

**Attach style**
All left braces (including functions, classes, structs, and control statements) follow the statement at the end of the line, preceded by 1 space.
The right brace occupies its own line, unless followed by the remainder of the same statement, such as while in a do statement, or else/else if in an if statement, or a comma or semicolon.

For example:
```cpp
struct MyType {  // Follows the statement at the end of the line, preceded by 1 space
  ...
};

int Foo(int a) {  // The function left brace also follows the statement at the end of the line
  if (...) {
    ...
  } else {
    ...
  }
}
```
Reasons for recommending this style:

- Code is more compact;
- Compared to starting on a new line, placing it at the end of the line makes the reading rhythm more continuous;
- It conforms to the habits of later languages and the mainstream industry;
- It is consistent with the repository's `.clang-format` configuration (`BreakBeforeBraces: Attach`), so running clang-format before submission produces no extra diff;
- Modern IDEs have code indentation and alignment display features, so placing braces at the end of the line does not affect the understanding of indentation and scope.


For an empty function body, braces may be placed on the same line:
```cpp
class MyClass {
 public:
  MyClass() : value_(0) {}

 private:
  int value_;
};
```

##### Rule 2.8 Multiple variable definitions and assignment statements on one line are not allowed

A single variable initialization statement per line is easier to read and understand.

##### Rule 2.9 Arrange blank lines reasonably to keep code compact

Reducing unnecessary blank lines can display more code and facilitate code reading. Here are some recommended rules to follow:
- Arrange blank lines reasonably based on the relevance of the content;
- Inside function bodies, type definitions, macros, and initialization expressions, consecutive blank lines should not be used
- Do not use consecutive blank lines; keep at most 1 (consistent with `MaxEmptyLinesToKeep: 1` in `.clang-format`)
- Do not add blank lines before the first line or after the last line of code inside braces, but this does not apply to namespace braces.

```cpp
int Foo() {
  ...
}


int Bar() {  // Bad: keep at most 1 consecutive blank line.
  ...
}

if (...) {
  // Bad: do not add blank lines before the first line of code inside braces
  ...
  // Bad: do not add blank lines after the last line of code inside braces
}

int Foo(...) {
  // Bad: do not add blank lines before the first line of the function body
  ...
}
```

---

### 3. Comments

Generally, strive to improve code readability through clear architectural logic and good symbol naming; use comments to supplement explanations only when needed.
Comments are meant to help readers quickly understand the code, so start from the reader's perspective and **comment as needed**.

Comment content should be concise, clear, and unambiguous, with comprehensive and non-redundant information.

In C++ code, both `/*` `*/` and `//` are acceptable.
Based on the purpose and location of comments, they can be divided into different types, such as file header comments, function header comments, code comments, etc.;
Comments of the same type should maintain a consistent style.

##### Rule 3.1 File header comments must include the copyright notice

As in the following example:

```cpp
/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
```

> Regarding copyright notices, note:
> Files newly created in 2026 should be `Copyright (c) 2026 Huawei Technologies Co., Ltd.`
> Files created in 2025 and modified in 2026 should be `Copyright (c) 2025-2026 Huawei Technologies Co., Ltd.`

##### Rule 3.2 Code comments are placed above or to the right of the corresponding code; there must be 1 space between the comment symbol and the comment content; right-placed comments must have at least 1 space from the preceding code. Right-placed comments use `//` instead of `/**/`; comments placed above the code may use either `//` or `/* */`.

```cpp
// this is multi-
// line comment
int foo;  // this single-line comment
```

##### Rule 3.3 Do not use TODO/TBD/FIXME and similar comments in code; it is recommended to submit an issue for tracking

##### Recommendation 3.4 Do not write function header comments with empty formatting

Not all functions need function header comments. Functions should be self-documenting through their names as much as possible; write function header comments only as needed. Function header comments are needed only for information that cannot be expressed by the function prototype but that you want readers to know.
Do not write useless or information-redundant function headers. Function header comment content is optional but not limited to: functionality description, return values, performance constraints, usage, memory conventions, algorithm implementation, reentrancy requirements, etc.
Example:

```cpp
/*
 * Returns the actual number of bytes written; -1 indicates write failure
 * Note: the memory buf is released by the caller
 */
int WriteString(const char *buf, int len);
```

Bad example:
```cpp
/*
 * Function name: WriteString
 * Function: Write string
 * Parameters:
 * Return value:
 */
int WriteString(const char *buf, int len);
```
Problems in the example above:

- Parameters and return values have formatting but no content
- Function name information is redundant
- The key question of who releases buf is not clarified

##### Recommendation 3.5 Delete unused code segments directly; do not comment them out

Commented-out code cannot be properly maintained; when attempting to restore this code, it is very likely to introduce easily overlooked defects.
The correct approach is to delete unneeded code directly. If needed again, consider porting or rewriting this code.
