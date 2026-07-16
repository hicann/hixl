# 头文件和库文件说明

安装固件、驱动及CANN软件包后，编译、运行应用程序时才能引用到hixl接口的头文件、库文件。

hixl接口的头文件在`${INSTALL_DIR}/include/`目录下，库文件在`${INSTALL_DIR}/lib64/`目录下。${INSTALL_DIR}请替换为CANN软件安装后文件存储路径。以root用户安装为例，安装后文件默认存储路径为：/usr/local/Ascend/cann。

您需要根据实际使用的hixl接口来include依赖的文件，各头文件的用途如下表所示。

**表 1**  头文件列表

| 定义接口的头文件 | 用途 | 对应的库文件 |
| --- | --- | --- |
| llm\_datadist/llm\_datadist.h | 面向大模型KV Cache传输的场景，提供带有KV Cache语义的接口能力。支持角色管理、链路管理、KV Cache注册、连续KV Cache传输、KV Block传输等功能。 | libllm\_datadist.so |
| hixl/hixl.h | 面向点对点数据传输的场景，提供极致易用的单边零拷贝数据直传能力。支持初始化/去初始化、内存注册/解注册、建链/断链、数据传输等功能。 | libcann\_hixl.so |
| cs/hixl\_cs.h | 面向使用Client-Server模式的场景，提供Client-Server模式集成单边零拷贝数据直传能力。支持Client端的创建/销毁、建链、内存注册/注销、获取远端内存描述、传输以及Sever端的创建/销毁、内存注册/注销等功能。 | libcann\_hixl.so |
