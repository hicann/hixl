# 源码构建

## 环境准备
本项目支持源码编译，在源码编译前，需要确保已经安装Ascend-cann-toolkit。执行所有样例前，需要确保已经安装驱动和固件；执行python样例前，还需要确保已经安装Ascend-cann-ops。

软件安装方式请根据如下描述进行选择：

| 安装方式 | 说明 |使用场景|
| :--- | :--- | :--- |
| 使用Docker部署 | Docker镜像是一种CANN高效部署方式，目前仅适用于Atlas A2系列产品，OS仅支持Ubuntu操作系统。|适用有昇腾设备，需要快速搭建环境的开发者。|
| 手动安装 | - |适用有昇腾设备，想体验手动安装CANN包或体验最新master分支能力的开发者。|

### 场景一：使用Docker部署

**1.安装固件和驱动**：请参考[CANN软件安装指南](https://www.hiascend.com/document/redirect/CannCommunityInstWizard)。

**2.安装依赖、CANN Toolkit开发套件包以及CANN ops算子包**，操作步骤如下。


- **配套 X86 构建镜像地址**：`swr.cn-north-4.myhuaweicloud.com/ci_cann/ubuntu20.04.05_x86:lv4_latest`

- **配套 ARM 构建镜像地址**：`swr.cn-north-4.myhuaweicloud.com/ci_cann/ubuntu20.04.05_arm:lv4_latest`

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

  配套构建镜像的安装路径为`/home/jenkins/Ascend`。如需要使用镜像之外的其他CANN版本，请参考如下章节在docker内手工安装CANN包。


### 场景二：手动安装CANN包

**场景1：体验master版本能力或基于master版本进行开发**

如果您想体验**master分支最新能力**，单击[下载链接](https://ascend.devcloud.huaweicloud.com/artifactory/cann-run-mirror/software/master)获取软件包，按照如下步骤进行安装。更多安装指导请参考[CANN软件安装指南](https://www.hiascend.com/document/redirect/CannCommunityInstWizard)。

1. 安装固件和驱动：请参考[CANN软件安装指南](https://www.hiascend.com/document/redirect/CannCommunityInstWizard)。
2. 安装依赖。   
   <br>以下所列为源码编译用到的依赖，请注意版本要求。

 ```shell
  # Ubuntu/Debian操作系统安装命令示例如下，其他操作系统请自行安装
  sudo apt-get install cmake bash ccache
  ```
- GCC >= 7.3.0
- Python 3.9~3.12
- CMake >= 3.16.0  (建议使用3.20.0版本)
- bash >= 5.1.16，由于测试用例开启了地址消毒，代码中执行system函数会触发低版本的bash被地址消毒检查出内存泄露。
- ccache（可选），ccache为编译器缓存优化工具，用于加快二次编译速度。
3. 安装社区版CANN toolkit包。

    ```bash
    # 确保安装包具有可执行权限
    chmod +x Ascend-cann-toolkit_${cann_version}_linux-${arch}.run
    # 安装命令
    ./Ascend-cann-toolkit_${cann_version}_linux-${arch}.run --install --install-path=${install_path}
    ```

4. 安装社区版CANN ops包。

    ```bash
    # 确保安装包具有可执行权限
    chmod +x Ascend-cann-${soc_name}-ops_${cann_version}_linux-${arch}.run
    # 安装命令
    ./Ascend-cann-${soc_name}-ops_${cann_version}_linux-${arch}.run --install --install-path=${install_path}
    ```
    - \$\{cann\_version\}：表示CANN包版本号。
    - \$\{arch\}：表示CPU架构，如aarch64、x86_64。
    - \$\{soc\_name\}：表示NPU型号名称。
    - \$\{install\_path\}：表示指定安装路径，需要与toolkit包安装在相同路径，root用户默认安装在`/usr/local/Ascend`目录。

**场景2：体验已发布版本能力或基于已发布版本进行开发**

如果您想体验**官网正式发布的CANN包**能力，请访问[CANN官网下载中心](https://www.hiascend.com/cann/download)，选择对应版本CANN软件包（仅支持CANN 8.5.0及后续版本）进行安装。


## 环境验证

安装完CANN包后，需验证环境和驱动是否正常。

-   **检查NPU设备**：
    ```bash
    # 运行npu-smi，若能正常显示设备信息，则驱动正常
    npu-smi info
    ```
-   **检查CANN安装**：
    ```bash
    # 查看CANN Toolkit的version字段提供的版本信息（默认路径安装），<arch>表示CPU架构（aarch64或x86_64）。
    cat /usr/local/Ascend/cann/<arch>-linux/ascend_toolkit_install.info
    # 查看CANN ops的version字段提供的版本信息（默认路径安装），<arch>表示CPU架构（aarch64或x86_64）。
    cat /usr/local/Ascend/cann/<arch>-linux/ascend_ops_install.info
    ```

## 环境变量配置

按需选择合适的命令使环境变量生效。
```bash
# 默认路径安装，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/cann/set_env.sh
# 指定路径安装
# source ${install_path}/cann/set_env.sh
```
## 源码编译

### 安装第三方开源依赖

以下所列为源码编译用到的依赖，请注意版本要求。

  ```shell
  # Ubuntu/Debian操作系统安装命令示例如下，其他操作系统请自行安装
  sudo apt-get install cmake bash ccache
  ```
- GCC >= 7.3.0
- Python 3.9~3.12
- CMake >= 3.16.0  (建议使用3.20.0版本)
- bash >= 5.1.16，由于测试用例开启了地址消毒，代码中执行system函数会触发低版本的bash被地址消毒检查出内存泄露。
- ccache（可选），ccache为编译器缓存优化工具，用于加快二次编译速度。

HIXL在编译时，依赖的第三方开源软件列表如下：

| 开源软件 | 版本 | 下载地址 |
|---|---|---|
| googletest | 1.14.0 | [googletest-1.14.0.tar.gz](https://gitcode.com/cann-src-third-party/googletest/releases/download/v1.14.0/googletest-1.14.0.tar.gz) |
| json | 3.11.3 | [include.zip](https://gitcode.com/cann-src-third-party/json/releases/download/v3.11.3/include.zip) |
| makeself | 2.5.0 | [makeself-release-2.5.0-patch1.tar.gz](https://gitcode.com/cann-src-third-party/makeself/releases/download/release-2.5.0-patch1.0/makeself-release-2.5.0-patch1.tar.gz) |
| pybind11 | 2.13.6 | [pybind11-2.13.6.tar.gz](https://gitcode.com/cann-src-third-party/pybind11/releases/download/v2.13.6/pybind11-2.13.6.tar.gz) |

> [!NOTE]注意
> 如果您从其他地址下载，请确保版本号一致。

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
| `--sign-script=<PATH>`<br>`--sign_script=<PATH>` | 设置签名脚本的指定路径 | - |
| `--enable-sign` | 启用签名功能 | - |

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
    # 如果已自行将第三方开源软件压缩包上传至目录{your_3rd_party_path}，可以使用如下命令进行执行：
    bash tests/run_test.sh --cann_3rd_lib_path={your_3rd_party_path}
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