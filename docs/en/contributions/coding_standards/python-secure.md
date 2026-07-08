# CANN Python Secure Coding Specifications

>  **Applicable Scope**: The Python secure coding specifications apply to all Python code, including source code, test scripts, and tool scripts.

## Rule List

| Rule No. | Rule Name | Category | Severity |
|---------|---------|------|---------|
| 1.1 | File header comments must include COPYRIGHT and LICENSE declarations | File Header | Medium |
| 2.1 | Division and modulo operations must have divide-by-zero protection | Numeric Safety | High |
| 3.1 | Do not leak sensitive data through exceptions | Exception Handling | High |
| 4.1 | Product code must not contain debug entry points | Debugging & Privacy | Medium |
| 4.2 | Released code must not contain personal privacy information | Debugging & Privacy | Medium |
| 4.3 | Do not include unpublished public network addresses | Debugging & Privacy | Medium |
| 4.4 | Do not expose unnecessary information in interfaces and logs | Debugging & Privacy | Medium |
| 5.1 | Specify appropriate permissions when creating files | File Operations | Medium |
| 5.2 | External file paths must be validated and normalized | File Operations | High |
| 5.3 | Do not use tempfile.mktemp | File Operations | High |
| 5.4 | Delete temporary files promptly after use | File Operations | Medium |
| 5.5 | Decompressing files must perform security checks | File Operations | High |
| 6.1 | Do not use pickle.load to load external data | Serialization | High |
| 6.2 | Do not serialize unencrypted sensitive data | Serialization | High |
| 6.3 | Do not use the yaml.load function | Serialization | High |
| 6.4 | Do not use the jsonpickle module | Serialization | High |
| 7.1 | Do not use eval and exec to execute untrusted code | Command Execution | High |
| 7.2 | Do not use OS command parsers to invoke system commands | Command Execution | High |
| 7.3 | Avoid using wildcards in command parsers | Command Execution | Medium |
| 7.4 | Do not use shell=True with subprocess | Command Execution | High |
| 8.1 | Do not use .format() to format external data | Formatting | High |
| 8.2 | Do not directly concatenate XML with external data | XML Safety | High |
| 8.3 | Do not parse untrusted XML entities | XML Safety | High |
| 9.1 | Do not log external data directly | Log Safety | Medium |
| 9.2 | Do not use assert in production code | Log Safety | Medium |
| 10.1 | Sign and encrypt sensitive objects before sending them out of the trust zone | Cryptography | High |
| 10.2 | Security scenarios must use cryptographically secure random numbers | Cryptography | High |
| 10.3 | Must use SSLSocket for secure data exchange | Network Safety | High |

## Description

These specifications are the Python secure coding specifications of the CANN open-source community, covering numeric operation safety, input and data validation, file and system operation safety, encryption and communication safety, specific format processing safety, privacy protection, and more.

## Scope of Application

Python code security review of CANN-related open-source repositories.

---

### 1. File Header Comments

##### Rule 1.1 File header comments should include COPYRIGHT and LICENSE declarations

**[Description]** Add COPYRIGHT and LICENSE declarations at the beginning of each source file to ensure the legality and traceability of the code. This helps protect the intellectual property of the code and clarify the terms of use.

Example:

```python
#
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
#
```

---

### 2. Numeric Operation Safety

##### Rule 2.1 Provide protection for divide-by-zero in division and modulo operations

**[Description]** When performing division or modulo operations, check whether the divisor is 0 first to prevent the program from crashing due to a divide-by-zero error. This can be implemented through conditional statements or exception handling.

---

### 3. Exception Handling

##### Rule 3.1 Do not leak sensitive data through exceptions

**[Description]** When catching and handling exceptions, sensitive data (such as passwords, keys, etc.) should not be included in exception messages to prevent this information from being accidentally leaked.

---

### 4. Debugging and Privacy

##### Rule 4.1 Product code must not contain any debug entry points

**[Description]** Released product code should not contain debug entry points or special features to ensure the security and stability of the code. Debug code should be managed separately in the development environment.

##### Rule 4.2 Released code and comment content must be legal and compliant and must not contain personal privacy information

**[Description]** Released code and comment content must be legal and compliant; they should not contain developers' personal privacy information (such as ID card numbers, phone numbers, etc.) to protect personal privacy.

##### Rule 4.3 Code must not contain public network addresses that are not visible in the user interface or not publicly documented

**[Description]** Code should not hardcode public network addresses to prevent problems caused by address changes or doubts caused by public network addresses. Public network addresses should be stored in configuration files or databases.

**[Exception]** Scenarios where public network addresses must be specified in standard protocols are exceptions.

##### Rule 4.4 Do not expose unnecessary information in user interfaces and logs

**[Description]** Unnecessary information, especially sensitive information, should not be exposed in user interfaces and logs to prevent information leakage and abuse.

---

### 5. File Operation Safety

##### Rule 5.1 When creating files in a multi-user system, appropriate permissions should be specified as needed

**[Description]** When creating files in a multi-user system, appropriate file permissions should be set according to actual requirements to ensure file security and access control.

##### Rule 5.2 File paths constructed from external data must be validated before use, and the file path must be normalized before validation

**[Description]** Before generating a file path from external data, the path should be normalized (such as removing redundant path separators) and validated to prevent path traversal attacks.

##### Rule 5.3 Do not use tempfile.mktemp to create temporary files

**[Description]** The `tempfile.mktemp` function has security risks because it does not create a file; it only returns a path, which may lead to race conditions. Use `tempfile.TemporaryFile` or `tempfile.NamedTemporaryFile` to create temporary files.

##### Rule 5.4 Temporary files should be deleted promptly after use

**[Description]** Temporary files should be deleted immediately after use to release system resources and prevent data leakage.

##### Rule 5.5 Decompressing files must perform security checks

**[Description]** Before decompressing a file, the compressed file should be security-checked to ensure it does not contain malicious files or path traversal attacks.

---

### 6. Serialization and Deserialization

##### Rule 6.1 Do not use pickle.load, _pickle.load, and the shelve module to load external data

**[Description]** The `pickle` and `shelve` modules have security risks because they can execute arbitrary code. Use safer serialization methods, such as `json`.

##### Rule 6.2 Do not serialize unencrypted sensitive data

**[Description]** When serializing sensitive data, use an encryption algorithm to encrypt it to prevent data from being stolen during transmission.

##### Rule 6.3 Do not use the load function of the yaml module

**[Description]** The `yaml.load` function has security risks because it can execute arbitrary code. Use `yaml.safe_load` as a replacement.

##### Rule 6.4 Do not use the encode/decode or dumps/loads functions of the jsonpickle module

**[Description]** The `jsonpickle` module has security risks because it can execute arbitrary code. Use safer serialization methods, such as `json`.

---

### 7. Command Execution Safety

##### Rule 7.1 Do not use eval and exec functions to execute untrusted code

**[Description]** The `eval` and `exec` functions can execute arbitrary code and pose serious security risks. Avoid using these functions to process untrusted data.

##### Rule 7.2 Do not use OS command parsers or "dangerous functions" to invoke system commands

**[Description]** Avoid using functions such as `os.system` and `os.popen` to directly invoke system commands because they carry injection attack risks. Use safer methods of the `subprocess` module.

##### Rule 7.3 Avoid using wildcards in command parsers

**[Description]** Using wildcards (such as `*`) in command parsers may lead to unexpected behavior and security issues. Avoid using wildcards as much as possible and explicitly specify file names or paths.

##### Rule 7.4 Do not use the shell=True option in the subprocess module

**[Description]** The `shell=True` option of the `subprocess` module executes commands through the system shell, which carries injection attack risks. Use `shell=False` and pass a command list.

---

### 8. Formatting and XML Safety

##### Rule 8.1 Untrusted external data must not be formatted using .format()

**[Description]** When using the `str.format` method, avoid using untrusted external data to prevent format string injection attacks. You can use the `f-string` or `str.format_map` method.

##### Rule 8.2 Do not directly concatenate XML with external data

**[Description]** Directly concatenating XML with external data may lead to XML injection attacks. Use the secure methods provided by XML libraries to build XML documents.

##### Rule 8.3 Do not parse untrusted entities when processing XML data

**[Description]** When processing XML data, disable external entity parsing to prevent XXE (XML External Entity) attacks.

---

### 9. Log Safety

##### Rule 9.1 Do not log external data directly

**[Description]** Logging external data directly may leak sensitive information. External data should be filtered and sanitized before being logged.

##### Rule 9.2 Do not use assert in production business code

**[Description]** `assert` statements are ignored in production environments and should not be relied upon for error handling. Use conditional statements and exception handling to ensure code robustness.

---

### 10. Cryptography and Network Safety

##### Rule 10.1 Sign and encrypt sensitive objects before sending them out of the trust zone

**[Description]** Before sending sensitive objects to untrusted zones (such as network transmission), they should be digitally signed and encrypted to ensure data integrity and confidentiality.

##### Rule 10.2 Cryptographically secure random numbers must be used in security scenarios

**[Description]** In scenarios involving security (such as generating keys, tokens, etc.), use a cryptographically secure random number generator to prevent prediction and attacks.

##### Rule 10.3 Must use ssl.SSLSocket instead of socket.Socket for secure data exchange

**[Description]** When performing network communication, use `ssl.SSLSocket` instead of ordinary `socket.Socket` to ensure the security of data transmission.
