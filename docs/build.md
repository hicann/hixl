# 构建

## 编译

### 环境准备
本项目支持源码编译，在源码编译前，请根据如下步骤完成相关环境准备。

1. **安装依赖**

   以下所列为源码编译用到的依赖，请注意版本要求。  

   - GCC >= 7.3.0

   - Python3 3.9或3.11(当前仅支持这两个版本)

   - CMake >= 3.16.0  (建议使用3.20.0版本)
     ```shell
     # Ubuntu/Debian操作系统安装命令示例如下，其他操作系统请自行安装
     sudo apt-get install cmake
     ```

   - bash >= 5.1.16

     由于测试用例开启了地址消毒，代码中执行system函数会触发低版本的bash被地址消毒检查出内存泄露。

     ```shell
     # Ubuntu/Debian操作系统安装命令示例如下，其他操作系统请自行安装
     sudo apt-get install bash
     ```

   - ccache（可选）

     compile cache为编译器缓存优化工具，用于加快二次编译速度。

     ```shell
     # Ubuntu/Debian操作系统安装命令示例如下，其他操作系统请自行安装
     sudo apt-get install ccache
     ```

2. **安装驱动与固件（运行态依赖）**  

    运行样例时必须安装驱动与固件，安装指导详见[《CANN软件安装指南》](https://www.hiascend.com/document/redirect/CannCommunityInstSoftware)。  

3. **安装社区版CANN toolkit包**

    根据实际环境，下载对应`Ascend-cann-toolkit_${cann_version}_linux-${arch}.run`包，下载链接为[CANN包社区版资源下载](https://www.hiascend.com/developer/download/community/result?module=cann&cann=8.5.0.alpha001)。
    
    ```bash
    # 确保安装包具有可执行权限
    chmod +x Ascend-cann-toolkit_${cann_version}_linux-${arch}.run
    # 安装命令(其中--install-path为可选)
    ./Ascend-cann-toolkit_${cann_version}_linux-${arch}.run --full --force --install-path=${cann_install_path}
    ```
    - \$\{cann\_version\}：表示CANN包版本号。
    - \$\{arch\}：表示CPU架构，如aarch64、x86_64。
    - \$\{cann\_install\_path\}：表示指定安装路径，可选，默认安装在`/usr/local/Ascend`目录，指定路径安装时，指定的路径权限需设置为755。

4. **安装社区版CANN kernels包（可选）**

   运行python样例时需安装本包，若仅编译源码或运行C++样例，则跳过本操作。

    根据产品型号和环境架构，下载对应CANN kernels包，下载链接为[CANN包社区版资源下载](https://www.hiascend.com/developer/download/community/result?module=cann&cann=8.5.0.alpha001)：

    - Atlas A2 训练系列产品/Atlas 800I A2 推理产品/A200I A2 Box 异构组件：`Ascend-cann-kernels-910b_${cann_version}_linux-${arch}.run`
    - Atlas A3 训练系列产品/Atlas A3 推理系列产品：`Atlas-A3-cann-kernels_${cann_version}_linux-${arch}.run`

    ```bash
    # 确保安装包具有可执行权限
    # Atlas A2 训练系列产品/Atlas 800I A2 推理产品/A200I A2 Box 异构组件
    chmod +x Ascend-cann-kernels-910b_${cann_version}_linux-${arch}.run
    # Atlas A3 训练系列产品/Atlas A3 推理系列产品
    chmod +x Atlas-A3-cann-kernels_${cann_version}_linux-${arch}.run

    # 安装命令
    # Atlas A2 训练系列产品/Atlas 800I A2 推理产品/A200I A2 Box 异构组件
    ./Ascend-cann-kernels-910b_${cann_version}_linux-${arch}.run --full --install-path=${cann_install_path}
    # Atlas A3 训练系列产品/Atlas A3 推理系列产品
    ./Atlas-A3-cann-kernels_${cann_version}_linux-${arch}.run --full --install-path=${cann_install_path}
    ```

    - \$\{cann\_install\_path\}：表示指定安装路径，需要与toolkit包安装在相同路径，默认安装在`/usr/local/Ascend`目录。
   

### 源码下载

开发者可通过如下命令下载本仓源码：
```bash
git clone https://gitcode.com/cann/hixl.git
```
- 注意：gitcode平台在使用HTTPS协议的时候要配置并使用个人访问令牌代替登录密码进行克隆，推送等操作。  

### 配置环境变量
	
根据实际场景，选择合适的命令。

 ```bash
# 默认路径安装，以root用户为例（非root用户，将/usr/local替换为${HOME}）
source /usr/local/Ascend/set_env.sh
# 指定路径安装
# source ${cann_install_path}/set_env.sh
 ```

### 编译执行

```bash
# 默认路径安装，root用户默认路径是/usr/local/Ascend，普通用户默认路径是${HOME}/Ascend
bash build.sh 
```
成功编译后会在build_out目录下生成`cann-hixl_${cann_version}_linux-${arch}.run`，同时会将C++用例一同编译，在build/examples/cpp路径下生成编译后的二进制文件。  
- ${cann_version}表示cann版本号。
- ${arch}表示表示CPU架构，如aarch64、x86_64。 
- 更多执行选项可以用-h查看。  
  ```
  bash build.sh -h
  ```


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
  - 若选择该参数，则.whl安装在${cann_install_path}/latest/python/site-packages路径。
  - 若不选择该参数，则.whl安装在本地python路径，例如/usr/local/python3.7.5/lib/python3.7/site-packages。
- 更多安装选项请用--help选项查看。  

**安装完成后可参考[样例运行](../examples/README.md)尝试运行样例**。  