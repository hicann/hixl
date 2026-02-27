# 源码构建

## 环境准备
本项目支持源码编译，在源码编译前，请根据如下步骤完成相关环境准备。

### 1. **安装依赖**

请根据实际情况选择 **方式一（手动安装依赖）** 或 **方式二（使用Docker容器）** 完成相关环境准备。

#### 方式一: 手动安装

  以下所列为源码编译用到的依赖，请注意版本要求。  

  - GCC >= 7.3.0

  - Python3 3.9/3.11/3.12(当前仅支持这三个版本)

  - CMake >= 3.16.0  (建议使用3.20.0版本)

  - bash >= 5.1.16，由于测试用例开启了地址消毒，代码中执行system函数会触发低版本的bash被地址消毒检查出内存泄露。

  - ccache（可选），ccache为编译器缓存优化工具，用于加快二次编译速度。

  ```shell
  # Ubuntu/Debian操作系统安装命令示例如下，其他操作系统请自行安装
  sudo apt-get install cmake bash ccache
  ```

#### 方式二：使用Docker容器

  **配套 X86 构建镜像地址**：`swr.cn-north-4.myhuaweicloud.com/ci_cann/ubuntu20.04.05_x86:lv4_latest`
  
  **配套 ARM 构建镜像地址**：`swr.cn-north-4.myhuaweicloud.com/ci_cann/ubuntu20.04.05_arm:lv4_latest`

  更多版本镜像，可根据需要在[Ascend-CANN镜像](https://www.hiascend.com/developer/ascendhub/detail/17da20d1c2b6493cb38765adeba85884)自行选择下载。

  以下是推荐的使用方式，可供参考:

  ```bash
  image=${根据本地机器架构类型从上面选择配套的构建镜像地址}

  # 1. 拉取配套构建镜像
  docker pull ${image}
  # 2. 创建容器
  docker run --name env_for_hixl_build --cap-add SYS_PTRACE -d -it ${image} /bin/bash
  # 3. 启动容器
  docker start env_for_hixl_build
  # 4. 进入容器
  docker exec -it env_for_hixl_build /bin/bash
  ```

  > [!NOTE]说明
  > - `--cap-add SYS_PTRACE`：创建Docker容器时添加`SYS_PTRACE`权限，以支持[本地验证](#本地验证tests)时的内存泄漏检测功能。
  > - 更多 docker 选项介绍请通过 `docker --help` 查询。

  配套构建镜像已安装了上述依赖、CANN Toolkit开发套件包以及CANN ops算子包，安装路径为`/home/jenkins/Ascend`。
  完成上述步骤后可直接进入[配置环境变量](#5-配置环境变量)章节。

### 2. **安装驱动与固件（运行样例依赖）**  

  驱动与固件为运行样例时的依赖，且必须安装。若仅编译源码或进行本地验证，可跳过此步骤。

  驱动与固件的安装指导，可详见[《CANN软件安装指南》](https://www.hiascend.com/document/redirect/CannCommunityInstSoftware)。  

### 3. **安装社区版CANN toolkit包**
  本项目编译过程依赖CANN开发套件包（cann-toolkit），请根据环境操作系统架构，从[软件包下载地址](https://www.hiascend.com/developer/download/community/result?module=cann)下载`Ascend-cann-toolkit_${cann_version}_linux-${arch}.run`，参考[昇腾文档中心-CANN软件安装指南](https://www.hiascend.com/document/redirect/CannCommunityInstWizard)进行安装。
  
  - `${cann_version}`表示CANN版本号。
  - `${arch}`表示CPU架构，如aarch64、x86_64。

### 4. **安装社区版CANN ops包（运行样例依赖）**
  由于torch_npu依赖CANN Ops包，运行python样例时需安装本包，若仅编译源码或运行C++样例，可跳过此步骤。

  请根据产品型号和环境架构，从[软件包下载地址](https://www.hiascend.com/developer/download/community/result?module=cann)下载对应的CANN Ops包，参考[昇腾文档中心-CANN软件安装指南](https://www.hiascend.com/document/redirect/CannCommunityInstWizard)进行安装。
  
  - Atlas A2 训练系列产品/Atlas 800I A2 推理产品/A200I A2 Box 异构组件：`Ascend-cann-910b-ops_${cann_version}_linux-${arch}.run`
  - Atlas A3 训练系列产品/Atlas A3 推理系列产品：`Ascend-cann-A3-ops_${cann_version}_linux-${arch}.run`
  - `${cann_version}`表示CANN版本号。
  - `${arch}`表示CPU架构，如aarch64、x86_64。

### 5. **配置环境变量**
	
根据实际场景，选择合适的命令。

 ```bash
# 默认路径安装，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# 指定路径安装
# source ${cann_install_path}/cann/set_env.sh
 ```

## 编译

### 源码下载

开发者可通过如下命令下载本仓源码：
```bash
git clone https://gitcode.com/cann/hixl.git
```

> [!NOTE] 注意
> gitcode平台在使用HTTPS协议的时候要配置并使用个人访问令牌代替登录密码进行克隆，推送等操作。  

若您的编译环境无法访问网络，由于无法通过`git`指令下载代码，须在联网环境中下载源码后，手动上传至目标环境。
- 在联网环境中，进入[本项目主页](https://gitcode.com/cann/hixl), 通过`下载ZIP`或`clone`按钮，根据指导，完成源码下载。
- 连接至离线环境中，上传源码至您指定的目录下。若下载的为源码压缩包，还需进行解压。

### 开源第三方软件依赖

HIXL在编译时，依赖的第三方开源软件列表如下：

| 开源软件 | 版本 | 下载地址 |
|---|---|---|
| googletest | 1.14.0 | [googletest-1.14.0.tar.gz](https://gitcode.com/cann-src-third-party/googletest/releases/download/v1.14.0/googletest-1.14.0.tar.gz) |
| json | 3.11.3 | [include.zip](https://gitcode.com/cann-src-third-party/json/releases/download/v3.11.3/include.zip) |
| makeself | 2.5.0 | [makeself-release-2.5.0-patch1.tar.gz](https://gitcode.com/cann-src-third-party/makeself/releases/download/release-2.5.0-patch1.0/makeself-release-2.5.0-patch1.tar.gz) |
| pybind11 | 2.13.6 | [pybind11-2.13.6.tar.gz](https://gitcode.com/cann-src-third-party/pybind11/releases/download/v2.13.6/pybind11-2.13.6.tar.gz) |

> [!NOTE]注意
> 如果您从其他地址下载，请确保版本号一致。

### 源码编译

若您的编译环境可以访问网络，编译过程中将自动下载上述开源第三方软件，可以使用如下命令进行编译：

```bash
# 默认路径安装，root用户默认路径是/usr/local/Ascend，普通用户默认路径是${HOME}/Ascend
bash build.sh
```

若您的编译环境无法访问网络，您需要在联网环境中下载上述开源软件压缩包，并手动上传至您的编译环境中。

您需要在编译环境中新建一个`{your_3rd_party_path}`目录来存放这些第三方开源软件。

```bash
mkdir -p {your_3rd_party_path}
```

创建好目录后，将下载好的第三方开源软件压缩包上传至目录`{your_3rd_party_path}`后，可以使用如下命令进行编译：
```bash
bash build.sh --cann_3rd_lib_path={your_3rd_party_path}
```

成功编译后会在build_out目录下生成`cann-hixl_${cann_version}_linux-${arch}.run`。
- ${cann_version}表示cann版本号。
- ${arch}表示表示CPU架构，如aarch64、x86_64。
- 如需要进行样例验证或者基准测试Benchmarks验证，编译时候附加指定参数--examples
- 更多执行选项可以用-h查看，或查询下表。
  ```
  bash build.sh -h
  ```

| 参数 | 说明 | 默认值 |
|---|---|---|
| `-h, --help` | 打印帮助信息 | - |
| `-v, --verbose` | 显示详细的编译命令 | - |
| `-j<N>` | 设置编译时使用的线程数 | 8 |
| `--build_type=<Release\|Debug>`<br>`--build-type=<Release\|Debug>` | 设置编译类型 | Release |
| `--cann_3rd_lib_path=<PATH>`<br>`--cann-3rd-lib-path=<PATH>` | 设置第三方依赖包安装路径 | `./third_party` |
| `--output_path=<PATH>`<br>`--output-path=<PATH>` | 设置编译输出路径 | `./build_out` |
| `--pkg` | 构建run包（保留参数） | - |
| `--examples` | 编译样例和基准测试 | OFF |
| `--asan` | 启用地址消毒，用于内存泄漏检测 | OFF |
| `--cov` | 启用代码覆盖率 | OFF |

## 本地验证(tests)
利用tests路径下的测试用例进行本地验证:

- 安装依赖
    ```bash
    # 安装根目录下requirements.txt依赖
    pip3 install -r requirements.txt
    ```
    如果需要本地查看tests覆盖率则需要额外安装coverage，并将Python3的bin路径添加到PATH环境变量中，命令示例如下：

     ```shell
     pip3 install coverage
     # 修改下面的PYTHON3_HOME为实际的PYTHON安装目录
     export PATH=$PATH:$PYTHON3_HOME/bin
     ```

- 执行测试用例：

    ```bash
    # 默认路径安装，root用户默认路径是/usr/local/Ascend/，普通用户默认路径是${HOME}/Ascend
    bash tests/run_test.sh
    ```
  
- 更多执行选项可以用 -h 查看：
  ```
  bash tests/run_test.sh -h
  ```

## 安装

将[编译执行](#编译执行)环节生成的run包进行安装。  
- 说明，此处的安装路径（无论默认还是指定）需与前面安装toolkit包时的路径保持一致。  
```bash
# 如果需要指定安装路径则加上--install-path=${cann_install_path}
./cann-hixl_${cann_version}_linux-${arch}.run --full --quiet --pylocal
```
- --full 全量模式安装。  
- --quiet 静默安装，跳过人机交互环节。  
- --pylocal 安装HIXL软件包时，是否将.whl安装到HIXL安装路径。  
  - 若选择该参数，则.whl安装在${cann_install_path}/cann/python/site-packages路径。
  - 若不选择该参数，则.whl安装在本地python路径，例如/usr/local/python3.7.5/lib/python3.7/site-packages。
- 更多安装选项请用--help选项查看。  

**安装完成后可参考[样例运行](../examples/README.md)尝试运行样例，也可参考[基准测试Benchmarks](../benchmarks/README.md)尝试运行基准测试**。