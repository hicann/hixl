# pre-commit使用指导书

[TOC]
---

## 1 使用背景

本指导书主要用于指导如何在本地使用代码仓中部署的pre-commit能力（主要包括代码格式化、单词拼写检查及OAT扫描能力），更多使用方式请参考 [pre-commit官方文档](https://pre-commit.com/)。

## 2 功能概述

1、安装pre-commit后，git提交代码前会自动进行代码格式化处理、单词拼写扫描及触发OAT检查。

2、合规性问题会阻止提交并提示修改，阻止并非强制修改，可以忽略修改。

## 3 社区贡献者使用pre-commit能力

### 3.1 pre-commit安装步骤

步骤 1: 安装 pre-commit 框架（请确保已安装 python 和 pip）

```bash
# 使用 pip（推荐）
pip install pre-commit

# 验证安装
pre-commit --version
# 输出: pre-commit 3.x.x
```

步骤 2: 进入项目目录

```bash
cd /path/to/your/project

# 例如
cd d:\complianceRepo\CANN
```

步骤 3: 安装 Git Hooks

```bash
# 在项目根目录运行
pre-commit install
```

步骤 4: 验证安装（可选）

```bash
# 测试 hook（不会真正提交）
git commit --allow-empty -m "test pre-commit"
```

后续在提交代码前会自动进行代码格式化处理及触发OAT检查。

### 3.2 OAT使用指导

**OAT（Open Source Audit Tool）** 是一个开源合规性检查工具，自动集成到 Git 提交流程中。

#### 3.2.1 检查内容

**文件类型检查** - 禁止提交二进制文件（.so, .dll, .exe 等）
**许可证头检查** - 验证源代码文件包含合规的许可证声明

#### 3.2.2 核心特点

-  **增量检查** - 仅检查待提交文件，速度快（< 5 秒）
-  **自动触发** - 每次 `git commit` 自动运行
-  **详细报告** - 自动生成 `result.txt` 摘要和完整报告
-  **零配置** - Java 和 Maven 自动安装（Linux/macOS）
-  **跨平台** - Windows/Linux/macOS 全支持

#### 3.2.3 必需软件

| 软件 | 版本要求 | 用途 | 安装方式 |
|------|---------|------|----------|
| **Java** | JRE 8+ | 运行 OAT |  **自动安装**（Linux/macOS）<br> 手动安装（Windows）|
| **Maven** | 3.5+ | 打包 OAT |  **自动安装**（Linux/macOS）<br> 手动安装（Windows）|
| **Git** | 2.0+ | 版本控制 | 通常已安装 |
| **pre-commit** | 2.0+ | Hook 框架 | `pip install pre-commit` |

#### 3.2.4 自动安装支持

| 平台 | Java | Maven | 包管理器 | 首次安装时间 |
|------|------|-------|---------|-------------|
| **Linux (Ubuntu/Debian)** | 自动 |  自动 | apt | 5-8 分钟 |
| **Linux (CentOS/RHEL)** |  自动 |  自动 | yum | 5-8 分钟 |
| **macOS** |  自动 |  自动 | Homebrew | 8-10 分钟 |
| **Windows** |  手动 |  手动 | - | 需手动安装 |

#### 3.2.5 重要提示：环境问题自动跳过

**友好的设计**：如果无法安装 Java/Maven 或遇到环境问题，OAT 检查会**自动跳过**，提交仍会继续。

**会自动跳过的场景**

| 场景 | 行为 | 提示 |
|------|------|------|
| Java/Maven 未安装（Windows） |  跳过检查，允许提交 | 提供手动安装指引 |
| Java/Maven 自动安装失败 |  跳过检查，允许提交 | 提示手动安装方法 |
| Maven 打包失败 |  跳过检查，允许提交 | 提供解决方案 |
| OAT 扫描执行失败 |  跳过检查，允许提交 | 提示重新打包 |

** 仍会阻止提交的场景**

| 场景 | 行为 | 原因 |
|------|------|------|
| **发现二进制文件** |  阻止提交 | 真正的合规性问题 |
| **许可证头缺失/错误** | 阻止提交 | 真正的合规性问题 |

**跳过检查的提示示例**

```
[OAT] Windows 系统无法自动安装 Java
[OAT] 请手动下载并安装：
  ...（安装步骤）...

[OAT] 跳过 OAT 检查，继续提交...
[OAT] 建议安装 Java 后再次运行检查
```

**后续手动运行检查**

配置好环境后，可以手动运行检查：

```bash
# 推荐方式
pre-commit run oat-check

# 或直接运行脚本
bash scripts/oat_check.sh
```

#### 3.2.6 合规性问题（阻止提交）

**重要**: 以下问题会**阻止提交**，必须修复。

**1) 发现无效文件类型**

**场景**: 尝试提交二进制文件（.so, .dll, .exe 等）。

**输出**:
```
====================================================================
  发现合规性问题
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

**行为**:**阻止提交，必须修复**

**查看详情**:
```bash
cat oat_reports/single/result.txt
```

**报告内容示例**:
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

**解决方案**:
```bash
# 方法 1: 移除二进制文件
git reset HEAD lib/libtest.so

# 方法 2: 将二进制文件添加到 .gitignore
echo "*.so" >> .gitignore
echo "*.dll" >> .gitignore
echo "*.exe" >> .gitignore

# 重新提交
git add .gitignore
git commit -m "update: add binary files to gitignore"
```

**2) 许可证头无效**

**场景**: 源代码文件缺少或许可证头格式不正确。

**输出**:
```
====================================================================
  发现合规性问题
====================================================================

[OAT] Found 2 compliance issue(s):
  - Invalid File Type: 0
  - License Header Invalid: 2

[OAT] Details saved to: oat_reports/single/result.txt
```

**行为**: **阻止提交，必须修复**

**查看详情**:
```bash
cat oat_reports/single/result.txt
```

**报告内容示例**:
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

**解决方案**:

在文件顶部添加许可证头，例如 CANN-2.0：

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

**重新提交**:
```bash
git add src/main.cpp src/utils.cpp
git commit -m "fix: add license headers"
```

---

#### 3.2.7 报告查看

**报告文件位置**

| 报告类型 | 文件路径 | 内容 |
|---------|---------|------|
| **摘要报告** | `oat_reports/single/result.txt` | 关键问题汇总  |

**查看命令**

```bash
# 查看报告
cat oat_reports/single/result.txt

# 使用编辑器查看
code oat_reports/single/result.txt
vim oat_reports/single/result.txt
```

**摘要报告内容**

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

#### 3.2.8 环境问题

**1) Java 未安装（Linux/macOS 自动安装）**

**场景**: 首次提交，系统未安装 Java。

**输出**:
```
====================================================================
  Java 未安装 - 正在尝试自动安装
====================================================================

[OAT] 检测到系统未安装 Java，开始自动安装...
[OAT] 使用 apt 安装 OpenJDK 11...
[OAT] [OK] OpenJDK 11 安装成功
```

**处理**: 自动安装，可能需要输入 sudo 密码。

---

**2) Java 未安装（Windows 手动安装）**

**场景**: Windows 系统无法自动安装 Java。

**输出**:
```
[OAT] Windows 系统无法自动安装 Java
[OAT] 请手动下载并安装：

  1. 访问: https://adoptium.net/
  2. 下载: Eclipse Temurin JRE 11 (x64)
  3. 安装后重启 Git Bash
  4. 验证: java -version

[OAT] 跳过 OAT 检查，继续提交...
[OAT] 建议安装 Java 后再次运行检查
```

**行为**: **跳过检查，允许提交**

**后续操作**:
1. 按提示手动安装 Java
2. 重启终端
3. 运行 `pre-commit run oat-check` 验证环境

---

**3)  Java 自动安装失败**

**场景**: Linux/macOS 自动安装 Java 失败。

**输出**:
```
[OAT] [ERROR] 自动安装失败

[OAT] 自动安装失败，跳过 OAT 检查

手动安装方法:
  Linux:   sudo apt install openjdk-11-jre
  macOS:   brew install openjdk@11
  Windows: https://adoptium.net/

[OAT] 继续提交（未进行合规性检查）...
[OAT] 建议安装 Java 后再次运行: pre-commit run oat-check
```

**行为**: **跳过检查，允许提交**

**可能原因**:
- 网络连接问题
- 包管理器未配置
- 权限不足
- macOS 未安装 Homebrew

**解决方案**:
```bash
# Linux
sudo apt update
sudo apt install openjdk-11-jre

# macOS - 先安装 Homebrew
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
brew install openjdk@11

# 验证
java -version

# 手动运行检查
pre-commit run oat-check
```

---

**4) Maven 未安装（Linux/macOS 自动安装）**

**场景**: 首次提交，系统未安装 Maven。

**输出**:
```
====================================================================
  Maven 未安装 - 正在尝试自动安装
====================================================================

[OAT] 使用 apt 安装 Maven...
[OAT] [OK] Maven 安装成功
```

**处理**: 自动安装，可能需要输入 sudo 密码。

---

**5) Maven 未安装（Windows 手动安装）**

**场景**: Windows 系统无法自动安装 Maven。

**输出**:
```
[OAT] Windows 系统无法自动安装 Maven
[OAT] 请手动下载并安装：

  1. 访问: https://maven.apache.org/download.cgi
  2. 下载: apache-maven-3.x.x-bin.zip
  3. 解压到 C:\Program Files\apache-maven-3.x.x
  4. 添加到系统 PATH
  5. 重启 Git Bash
  6. 验证: mvn -version

[OAT] 跳过 OAT 检查，继续提交...
[OAT] 建议安装 Maven 后再次运行检查
```

**行为**: **跳过检查，允许提交**

**后续操作**: 按提示手动安装 Maven，然后运行 `pre-commit run oat-check`

---

**6) Maven 打包失败**

**场景**: Maven 打包 OAT JAR 失败。

**输出**:
```
====================================================================
  Maven 打包失败
====================================================================

[OAT] 无法打包 OAT JAR，跳过 OAT 检查

可能原因:
  1. Maven 配置问题
  2. 网络连接问题（无法下载依赖）
  3. pom.xml 配置错误

建议解决方案:
  1. 手动打包：
     cd ../tools_oat
     mvn clean package -DskipTests

  2. 配置 Maven 镜像（国内网络）：
     编辑 ~/.m2/settings.xml 添加阿里云镜像

[OAT] 继续提交（未进行合规性检查）...
[OAT] 建议修复打包问题后运行: pre-commit run oat-check
```

**行为**:  **跳过检查，允许提交**

**解决方案**:

**方法 1: 手动打包**
```bash
cd ../tools_oat
mvn clean package -DskipTests

# 查看输出，应该看到 BUILD SUCCESS
```

**方法 2: 配置阿里云镜像（国内网络）**
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

# 重新打包
cd ../tools_oat
mvn clean package -DskipTests
```

**方法 3: 从团队获取 JAR**
```bash
# 如果团队已有编译好的 JAR，直接复制
# 将 JAR 文件复制到 ../tools_oat/target/ 目录
```

**验证修复**:
```bash
pre-commit run oat-check
```

---

**7) tools_oat 克隆失败**

**输出**:
```
[OAT] tools_oat not found. Cloning...
[OAT] [ERROR] Failed to clone tools_oat.
[OAT] You can manually clone from: https://gitcode.com/openharmony-sig/tools_oat.git
```

**原因**: 网络连接问题。

**解决方案**:
```bash
# 方法 1: 检查网络
ping gitcode.com

# 方法 2: 手动克隆
cd ..
git clone https://gitcode.com/openharmony-sig/tools_oat.git

# 方法 3: 配置代理
git config --global http.proxy http://proxy.example.com:8080

# 方法 4: 从团队成员复制
# 让已克隆的同事打包 tools_oat 文件夹给你
```

---

**8) OAT 扫描执行失败**

**场景**: OAT JAR 运行失败。

**输出**:
```
====================================================================
  OAT 扫描执行失败
====================================================================

[OAT] 扫描失败，跳过 OAT 检查

可能原因:
  1. JAR 文件损坏
  2. Java 版本不兼容
  3. OAT 配置问题

建议解决方案:
  1. 删除并重新打包 JAR：
     rm ../tools_oat/target/ohos_ossaudittool-*.jar
     cd ../tools_oat && mvn clean package -DskipTests

  2. 检查 Java 版本（需要 Java 8+）：
     java -version

[OAT] 继续提交（未进行合规性检查）...
[OAT] 建议修复扫描问题后运行: pre-commit run oat-check
```

**行为**: **跳过检查，允许提交**

**解决方案**:
```bash
# 步骤 1: 删除旧 JAR
rm ../tools_oat/target/ohos_ossaudittool-*.jar

# 步骤 2: 重新打包
cd ../tools_oat
mvn clean package -DskipTests

# 步骤 3: 验证 JAR
ls -lh target/ohos_ossaudittool-*.jar

# 步骤 4: 运行检查
cd -
pre-commit run oat-check
```
