# CANN C++ Code Style Specifications

>  **Applicable Scope**: The code style specifications apply to all C++ code to improve code readability and consistency.

## Rule List

| Rule No. | Rule Name | Category | Severity |
|---------|---------|------|---------|
| 1.1 | C++ files use lowercase + underscore naming | Naming | Medium |
| 1.2 | Function naming uses UpperCamelCase | Naming | Medium |
| 1.3 | Type naming uses UpperCamelCase | Naming | Medium |
| 1.4 | Variable naming uses lowerCamelCase | Naming | Medium |
| 1.5 | Macros and enum values use all-uppercase with underscores | Naming | Medium |
| 2.1 | Line width must not exceed 120 characters | Formatting | Low |
| 2.2 | Use space indentation, 4 spaces per level | Formatting | Medium |
| 2.3 | `&` and `*` follow the variable name | Formatting | Low |
| 2.4 | if statements must use braces | Formatting | Medium |
| 2.5 | for/while loops must use braces | Formatting | Medium |
| 2.6 | Place operators at the end of the line when wrapping expressions | Formatting | Low |
| 2.7 | Use K&R indentation style | Formatting | Medium |
| 2.8 | Multiple variable definitions on one line are not allowed | Formatting | Low |
| 2.9 | Arrange blank lines reasonably to keep code compact | Formatting | Low |
| 3.1 | File header comments must include copyright notice | Comments | Medium |
| 3.2 | Comments use `//` with a space between the comment and code | Comments | Low |
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

#### CamelCase

CamelCase mixes uppercase and lowercase letters, with words joined together and separated by capitalizing the first letter of each word. Depending on whether the first letter of the joined result is capitalized, it is further divided into: UpperCamelCase and lowerCamelCase.

| Type | Naming Style |
| ---------------------------------------- | --------- |
| Class types, struct types, enum types, union types and other type definitions, scope names | UpperCamelCase |
| Functions (including global functions, scope functions, member functions) | UpperCamelCase |
| Global variables (including variables in global and namespace scope, class static variables), local variables, function parameters, member variables of classes, structs, and unions | lowerCamelCase |
| Macros, constants (const), enum values, goto labels | All-uppercase, separated by underscores |

Note:
**Constants** in the table above refer to variables of basic data types, enums, and string types modified by const or constexpr in the global scope, namespace scope, and class static member scope; they do not include arrays and other types of variables.
**Variables** in the table above refer to all variables other than constant definitions, all using the lowerCamelCase style.


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
	void AddElement(const Element& element);
	Element GetElement(const unsigned int index) const;
	bool IsEmpty() const;
};

namespace Utils {
    void DeleteUser();
}
```

##### Rule 1.3 Type naming uses UpperCamelCase

All type names—classes, structs, unions, type definitions (typedef), enums—use the same convention, for example:
```cpp
// classes, structs and unions
class UrlTable { ...
struct UrlTableProperties { ...
union Packet { ...
// typedefs
typedef std::map<std::string, UrlTableProperties*> PropertiesMap;
// enums
enum UrlTableErrors { ...
```

For namespace naming, UpperCamelCase is recommended:
```cpp
// namespace
namespace FileUtils {
}
```

##### Rule 1.4 General variable naming uses lowerCamelCase, including global variables, function parameters, local variables, and member variables

```cpp
std::string tableName;  // Good: recommended style
std::string tablename;  // Bad: prohibited style
std::string path;       // Good: when there is only one word, lowerCamelCase is all lowercase
```

Global variables should have a 'g_' prefix; static variable naming does not need a special prefix.
Global variables should be used as little as possible and used with special care, so a prefix is added for visual prominence, prompting developers to be more careful with these variables.
- Global static variables are named the same as global variables.
- Static variables inside functions are named the same as ordinary local variables.
- Static member variables of a class are the same as ordinary member variables.

```cpp
int g_activeConnectCount;

void Func()
{
    static int packetCount = 0;
    ...
}
```

Member variables of a class are named using lowerCamelCase with a trailing underscore

```cpp
class Foo {
private:
    std::string fileName_;   // Add a _ suffix, similar to K&R naming style
};
```

##### Rule 1.5 Macros and enum values use all-uppercase with underscores

Global scope const constants, const constants in named and anonymous namespaces, and static member constants of classes use all-uppercase with underscores; function-local const constants and ordinary const member variables of classes use the lowerCamelCase naming style.

```cpp
#define MAX(a, b)   (((a) < (b)) ? (b) : (a)) // Only an example of macro naming; using macros for such functionality is not recommended

enum TintColor {    // Note: the enum type name uses UpperCamelCase; its values below are all-uppercase with underscores
    RED,
    DARK_RED,
    GREEN,
    LIGHT_GREEN
};

int Func(...)
{
    const unsigned int bufferSize = 100;    // Function-local constant
    char *p = new char[bufferSize];
    ...
}

namespace Utils {
	const unsigned int DEFAULT_FILE_SIZE_KB = 200;        // Global constant
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

##### Rule 2.2 Use spaces for indentation, 4 spaces per indentation level

Only spaces may be used for indentation, 4 spaces per indentation level. Using Tab characters for indentation is not allowed.
Almost all modern integrated development environments (IDEs) support configuring Tab characters to automatically expand to 4 spaces; please configure your IDE to support using spaces for indentation.

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

##### Rule 2.5 for/while and other loop statements must use braces

Similar to conditional expressions, we require for/while loop statements to use braces, even if the loop body is empty or the loop has only one statement.
```cpp
for (int i = 0; i < someRange; i++) {   // Good: braces used
    DoSomething();
}
```
```cpp
while (condition) { }   // Good: the loop body is empty, braces used
```

##### Rule 2.6 Expression wrapping should maintain consistency; operators go at the end of the line

When a long expression does not meet the line width requirement, it should be wrapped at an appropriate place. Generally, break after a lower-precedence operator or connector, with the operator or connector placed at the end of the line.
Placing the operator or connector at the end of the line indicates "not finished, more to follow."
Example:
// Assume the first line below no longer meets the line width requirement
```cpp
if ((currentValue > threshold) &&  // Good: after wrapping, the logical operator is placed at the end of the line
    someCondition) {
    DoSomething();
    ...
}

int result = reallyReallyLongVariableName1 +    // Good
             reallyReallyLongVariableName2;
```
After wrapping an expression, pay attention to maintaining reasonable alignment or 4-space indentation. See the example below:

```cpp
int sum = longVariableName1 + longVariableName2 + longVariableName3 +
    longVariableName4 + longVariableName5 + longVariableName6;         // Good: 4-space indentation

int sum = longVariableName1 + longVariableName2 + longVariableName3 +
          longVariableName4 + longVariableName5 + longVariableName6;   // Good: maintain alignment
```

##### Rule 2.7 Use K&R indentation style

**K&R style**
When wrapping, the left brace of a function (excluding lambda expressions) starts on a new line at the beginning of the line and occupies its own line; all other left braces follow the statement at the end of the line.
The right brace occupies its own line, unless followed by the remainder of the same statement, such as while in a do statement, or else/else if in an if statement, or a comma or semicolon.

For example:
```cpp
struct MyType {     // Follows the statement at the end of the line, preceded by 1 space
    ...
};

int Foo(int a)
{                   // Function left brace occupies its own line, at the beginning of the line
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
- Do not use 3 or more consecutive blank lines
- Do not add blank lines before the first line or after the last line of code inside braces, but this does not apply to namespace braces.

```cpp
int Foo()
{
    ...
}



int Bar()  // Bad: at most 2 consecutive blank lines.
{
    ...
}


if (...) {
        // Bad: do not add blank lines before the first line of code inside braces
    ...
        // Bad: do not add blank lines after the last line of code inside braces
}

int Foo(...)
{
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

##### Rule 3.2 Code comments are placed above or to the right of the corresponding code; there must be 1 space between the comment symbol and the comment content; right-placed comments must have at least 1 space from the preceding code; use `//` instead of `/**/`

```cpp
// this is multi-
// line comment
int foo; // this single-line comment
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
