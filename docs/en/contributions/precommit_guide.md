# pre-commit User Guide

[TOC]
---

## 1 Background

This guide describes how to use the pre-commit capabilities deployed in the local code repository. These capabilities mainly include code formatting, spell check, and Open Source Audit Tool (OAT) scan. For more usage details, see the [official pre-commit documentation](https://pre-commit.com/).

## 2 Feature Overview

1. After pre-commit is installed, code formatting, spell check, and OAT check are automatically triggered before code is committed with Git.

2. Compliance issues block the commit and prompt you to make changes. Blocking does not force a change. You can ignore the change if needed.

## 3 Use pre-commit as a Community Contributor

### 3.1 Install pre-commit

Step 1: Install the pre-commit framework. Make sure Python and pip are installed.

```bash
# Use pip (recommended)
pip install pre-commit

# Verify the installation
pre-commit --version
# Output: pre-commit 3.x.x
```

Step 2: Enter the project directory.

```bash
cd /path/to/your/project

# Example
cd d:\complianceRepo\CANN
```

Step 3: Install Git Hooks.

```bash
# Run this command in the project root directory
pre-commit install
```

Step 4: Verify the installation (optional).

```bash
# Test the hook. This command does not create a real commit.
git commit --allow-empty -m "test pre-commit"
```

After this setup, code formatting and OAT check are automatically triggered before you commit code.

### 3.2 OAT User Guide

**Open Source Audit Tool (OAT)** is an open-source compliance check tool. It is automatically integrated into the Git commit workflow.

#### 3.2.1 Check Items

**File type check** - Blocks binary files such as .so, .dll, and .exe.
**License header check** - Verifies that source code files contain compliant license statements.

#### 3.2.2 Core Features

- **Incremental check** - Checks only staged files and completes quickly (< 5 seconds).
- **Automatic trigger** - Runs automatically for every `git commit`.
- **Detailed report** - Automatically generates a `result.txt` summary and a full report.
- **Zero configuration** - Automatically installs Java and Maven on Linux/macOS.
- **Cross-platform support** - Fully supports Windows, Linux, and macOS.

#### 3.2.3 Required Software

| Software | Version Requirement | Purpose | Installation Method |
|------|---------|------|----------|
| **Java** | JRE 8+ | Run OAT | **Automatic installation** (Linux/macOS)<br>Manual installation (Windows) |
| **Maven** | 3.5+ | Package OAT | **Automatic installation** (Linux/macOS)<br>Manual installation (Windows) |
| **Git** | 2.0+ | Version control | Usually already installed |
| **pre-commit** | 2.0+ | Hook framework | `pip install pre-commit` |

#### 3.2.4 Automatic Installation Support

| Platform | Java | Maven | Package Manager | First Installation Time |
|------|------|-------|---------|-------------|
| **Linux (Ubuntu/Debian)** | Automatic | Automatic | apt | 5-8 minutes |
| **Linux (CentOS/RHEL)** | Automatic | Automatic | yum | 5-8 minutes |
| **macOS** | Automatic | Automatic | Homebrew | 8-10 minutes |
| **Windows** | Manual | Manual | - | Manual installation required |

#### 3.2.5 Important Note: Automatic Skip for Environment Issues

**Friendly design**: If Java/Maven cannot be installed or an environment issue occurs, the OAT check is **automatically skipped**, and the commit continues.

**Scenarios That Are Automatically Skipped**

| Scenario | Behavior | Prompt |
|------|------|------|
| Java/Maven is not installed (Windows) | Skip the check and allow the commit | Provide manual installation instructions |
| Automatic Java/Maven installation fails | Skip the check and allow the commit | Prompt the manual installation method |
| Maven packaging fails | Skip the check and allow the commit | Provide a solution |
| OAT scan execution fails | Skip the check and allow the commit | Prompt repackaging |

**Scenarios That Still Block the Commit**

| Scenario | Behavior | Reason |
|------|------|------|
| **Binary file found** | Block the commit | Real compliance issue |
| **License header missing/incorrect** | Block the commit | Real compliance issue |

**Example Prompt for a Skipped Check**

```
[OAT] Windows cannot automatically install Java
[OAT] Please manually download and install it:
  ... (installation steps) ...

[OAT] Skip the OAT check and continue the commit...
[OAT] We recommend installing Java and running the check again
```

**Run the Check Manually Later**

After the environment is configured, you can run the check manually:

```bash
# Recommended method
pre-commit run oat-check

# Or run the script directly
bash scripts/oat_check.sh
```

#### 3.2.6 Compliance Issues (Block the Commit)

**Important**: The following issues **block the commit** and must be fixed.

**1) Invalid File Type Found**

**Scenario**: You try to commit a binary file such as .so, .dll, or .exe.

**Output**:
```
====================================================================
  Compliance Issues Found
====================================================================

[OAT] Found 1 compliance issue(s):
  - Invalid File Type: 1
  - License Header Invalid: 0

[OAT] Details saved to: oat_reports/single/result.txt
[OAT] Please check the report and fix the issues.

To view the summary:
  cat oat_reports/single/result.txt

To skip this check temporarily:
  git commit --no-verify
```

**Behavior**: **Block the commit. You must fix the issue.**

**View Details**:
```bash
cat oat_reports/single/result.txt
```

**Report Content Example**:
```
===================================
OAT Scan Result Summary
===================================
Scan Time: 2026-03-25 14:30:15
Project: CANN
Files Checked: 1

-----------------------------------
Invalid File Type Total Count: 1
lib/libtest.so: BINARY_FILE_TYPE

-----------------------------------
License Header Invalid Total Count: 0

===================================
Full report: oat_reports/single/PlainReport_CANN.txt
===================================
```

**Solution**:
```bash
# Method 1: Remove the binary file
git reset HEAD lib/libtest.so

# Method 2: Add binary files to .gitignore
echo "*.so" >> .gitignore
echo "*.dll" >> .gitignore
echo "*.exe" >> .gitignore

# Commit again
git add .gitignore
git commit -m "update: add binary files to gitignore"
```

**2) Invalid License Header**

**Scenario**: A source code file is missing a license header or has an incorrectly formatted license header.

**Output**:
```
====================================================================
  Compliance Issues Found
====================================================================

[OAT] Found 2 compliance issue(s):
  - Invalid File Type: 0
  - License Header Invalid: 2

[OAT] Details saved to: oat_reports/single/result.txt
```

**Behavior**: **Block the commit. You must fix the issue.**

**View Details**:
```bash
cat oat_reports/single/result.txt
```

**Report Content Example**:
```
===================================
OAT Scan Result Summary
===================================

-----------------------------------
Invalid File Type Total Count: 0

-----------------------------------
License Header Invalid Total Count: 2
src/main.cpp: MISSING_LICENSE_HEADER
src/utils.cpp: MISSING_LICENSE_HEADER

===================================
```

**Solution**:

Add a license header at the top of the file, for example CANN-2.0:

```cpp
/**
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

```

**Commit Again**:
```bash
git add src/main.cpp src/utils.cpp
git commit -m "fix: add license headers"
```

---

#### 3.2.7 View Reports

**Report File Location**

| Report Type | File Path | Content |
|---------|---------|------|
| **Summary report** | `oat_reports/single/result.txt` | Key issue summary  |

**View Commands**

```bash
# View the report
cat oat_reports/single/result.txt

# View the report with an editor
code oat_reports/single/result.txt
vim oat_reports/single/result.txt
```

**Summary Report Content**

```
===================================
OAT Scan Result Summary
===================================
Scan Time: 2026-03-25 14:30:15
Project: CANN
Files Checked: 3

-----------------------------------
Invalid File Type Total Count: 0

-----------------------------------
License Header Invalid Total Count: 0

===================================
Full report: oat_reports/single/PlainReport_CANN.txt
===================================
```

#### 3.2.8 Environment Issues

**1) Java Is Not Installed (Automatic Installation on Linux/macOS)**

**Scenario**: This is the first commit, and Java is not installed on the system.

**Output**:
```
====================================================================
  Java Is Not Installed - Trying Automatic Installation
====================================================================

[OAT] Java is not installed on the system. Start automatic installation...
[OAT] Install OpenJDK 11 with apt...
[OAT] [OK] OpenJDK 11 installed successfully
```

**Handling**: Automatic installation. You may need to enter the sudo password.

---

**2) Java Is Not Installed (Manual Installation on Windows)**

**Scenario**: Windows cannot automatically install Java.

**Output**:
```
[OAT] Windows cannot automatically install Java
[OAT] Please manually download and install it:

  1. Visit: https://adoptium.net/
  2. Download: Eclipse Temurin JRE 11 (x64)
  3. Restart Git Bash after installation
  4. Verify: java -version

[OAT] Skip the OAT check and continue the commit...
[OAT] We recommend installing Java and running the check again
```

**Behavior**: **Skip the check and allow the commit**

**Next Steps**:
1. Manually install Java as prompted.
2. Restart the terminal.
3. Run `pre-commit run oat-check` to verify the environment.

---

**3) Automatic Java Installation Fails**

**Scenario**: Automatic Java installation fails on Linux/macOS.

**Output**:
```
[OAT] [ERROR] Automatic installation failed

[OAT] Automatic installation failed. Skip the OAT check

Manual installation methods:
  Linux:   sudo apt install openjdk-11-jre
  macOS:   brew install openjdk@11
  Windows: https://adoptium.net/

[OAT] Continue the commit (compliance check was not performed)...
[OAT] We recommend installing Java and running again: pre-commit run oat-check
```

**Behavior**: **Skip the check and allow the commit**

**Possible Causes**:
- Network connection issue
- Package manager is not configured
- Insufficient permissions
- Homebrew is not installed on macOS

**Solution**:
```bash
# Linux
sudo apt update
sudo apt install openjdk-11-jre

# macOS - Install Homebrew first
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install openjdk@11

# Verify
java -version

# Run the check manually
pre-commit run oat-check
```

---

**4) Maven Is Not Installed (Automatic Installation on Linux/macOS)**

**Scenario**: This is the first commit, and Maven is not installed on the system.

**Output**:
```
====================================================================
  Maven Is Not Installed - Trying Automatic Installation
====================================================================

[OAT] Install Maven with apt...
[OAT] [OK] Maven installed successfully
```

**Handling**: Automatic installation. You may need to enter the sudo password.

---

**5) Maven Is Not Installed (Manual Installation on Windows)**

**Scenario**: Windows cannot automatically install Maven.

**Output**:
```
[OAT] Windows cannot automatically install Maven
[OAT] Please manually download and install it:

  1. Visit: https://maven.apache.org/download.cgi
  2. Download: apache-maven-3.x.x-bin.zip
  3. Extract it to C:\Program Files\apache-maven-3.x.x
  4. Add it to the system PATH
  5. Restart Git Bash
  6. Verify: mvn -version

[OAT] Skip the OAT check and continue the commit...
[OAT] We recommend installing Maven and running the check again
```

**Behavior**: **Skip the check and allow the commit**

**Next Steps**: Manually install Maven as prompted, and then run `pre-commit run oat-check`.

---

**6) Maven Packaging Fails**

**Scenario**: Maven fails to package the OAT JAR.

**Output**:
```
====================================================================
  Maven Packaging Failed
====================================================================

[OAT] Failed to package the OAT JAR. Skip the OAT check

Possible causes:
  1. Maven configuration issue
  2. Network connection issue (dependencies cannot be downloaded)
  3. pom.xml configuration error

Recommended solutions:
  1. Package manually:
     cd ../tools_oat
     mvn clean package -DskipTests

  2. Configure a Maven mirror (for networks in China):
     Edit ~/.m2/settings.xml and add the Alibaba Cloud mirror

[OAT] Continue the commit (compliance check was not performed)...
[OAT] We recommend fixing the packaging issue and running: pre-commit run oat-check
```

**Behavior**: **Skip the check and allow the commit**

**Solution**:

**Method 1: Package Manually**
```bash
cd ../tools_oat
mvn clean package -DskipTests

# View the output. BUILD SUCCESS should be displayed.
```

**Method 2: Configure the Alibaba Cloud Mirror (for Networks in China)**
```bash
mkdir -p ~/.m2
cat > ~/.m2/settings.xml <<'EOF'
<settings>
  <mirrors>
    <mirror>
      <id>aliyun</id>
      <mirrorOf>central</mirrorOf>
      <name>Aliyun Maven Mirror</name>
      <url>https://maven.aliyun.com/repository/public</url>
    </mirror>
  </mirrors>
</settings>
EOF

# Package again
cd ../tools_oat
mvn clean package -DskipTests
```

**Method 3: Obtain the JAR from the Team**
```bash
# If the team already has a compiled JAR, copy it directly.
# Copy the JAR file to the ../tools_oat/target/ directory.
```

**Verify the Fix**:
```bash
pre-commit run oat-check
```

---

**7) tools_oat Clone Fails**

**Output**:
```
[OAT] tools_oat not found. Cloning...
[OAT] [ERROR] Failed to clone tools_oat.
[OAT] You can manually clone from: https://gitcode.com/openharmony-sig/tools_oat.git
```

**Cause**: Network connection issue.

**Solution**:
```bash
# Method 1: Check the network
ping gitcode.com

# Method 2: Clone manually
cd ..
git clone https://gitcode.com/openharmony-sig/tools_oat.git

# Method 3: Configure a proxy
git config --global http.proxy http://proxy.example.com:8080

# Method 4: Copy it from a team member
# Ask a colleague who has cloned the repository to package the tools_oat folder for you.
```

---

**8) OAT Scan Execution Fails**

**Scenario**: The OAT JAR fails to run.

**Output**:
```
====================================================================
  OAT Scan Execution Failed
====================================================================

[OAT] Scan failed. Skip the OAT check

Possible causes:
  1. The JAR file is corrupted
  2. The Java version is incompatible
  3. OAT configuration issue

Recommended solutions:
  1. Delete and repackage the JAR:
     rm ../tools_oat/target/ohos_ossaudittool-*.jar
     cd ../tools_oat && mvn clean package -DskipTests

  2. Check the Java version (Java 8+ is required):
     java -version

[OAT] Continue the commit (compliance check was not performed)...
[OAT] We recommend fixing the scan issue and running: pre-commit run oat-check
```

**Behavior**: **Skip the check and allow the commit**

**Solution**:
```bash
# Step 1: Delete the old JAR
rm ../tools_oat/target/ohos_ossaudittool-*.jar

# Step 2: Package again
cd ../tools_oat
mvn clean package -DskipTests

# Step 3: Verify the JAR
ls -lh target/ohos_ossaudittool-*.jar

# Step 4: Run the check
cd -
pre-commit run oat-check
```
