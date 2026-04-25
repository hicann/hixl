## 简介

本项目提供了C++和Python的调用样例，请开发者根据实际情况参考对应实现。

## 目录说明
```
├── examples                       
│   ├── cpp                # C++样例
│   ├── python             # Python样例
│   ├── third_parties      # 对接三方库样例
│   ├── README.md          # 样例说明文档
```

## 开发样例

|  **样例名称**  |  **样例介绍**  |  **开发语言**  |
|---|---|---|
| [cppSamples](cpp) | C++样例 | C++ |
| [pythonSamples](python) | Python样例 | Python |
| [thirdPartiesSamples](third_parties) | Hixl对接其他社区样例 | C++/Python |


## 环境要求

### 1. 硬件和软件准备
-   芯片：Atlas A3 训练/推理系列产品、Atlas 800I A2 推理产品/A200I A2 Box 异构组件、Ascend 950PR/Ascend 950DT
-   参考 [环境准备](../docs/build.md#环境准备) 完成昇腾AI软件栈在运行环境上的部署

### 2. Device连通性检查
在执行样例前，请先使用驱动包提供的 [hccn_tool工具](https://support.huawei.com/enterprise/zh/ascend-computing/ascend-hdk-pid-252764743?category=developer-documents&subcategory=interface-reference) 检查**两个device之间的连通性**。以A2场景为例，检查示例如下：
> 若hccn_tool命令找不到，可在CANN驱动包安装目录下搜索hccn_tool可执行文件(默认路径为`/usr/local/Ascend/driver/tools/hccn_tool`)，并通过`ln -s /usr/local/Ascend/driver/tools/hccn_tool /usr/bin/hccn_tool`手动建立软链。

- step1：查询所需device的ip信息，以8卡为例：
```shell
for i in {0..7}; do hccn_tool -i $i -ip -g; done
```
- step2：检查两个device之间的连通性，以设备a和b连通性检查为例：
```shell
# 检查设备a是否能ping通设备b
hccn_tool -i ${device_id_a} -ping -g address ${ip_address_b}
# 检查设备b是否能ping通设备a
hccn_tool -i ${device_id_b} -ping -g address ${ip_address_a}
```
其中`device_id`为设备id，可通过`npu-smi info`查询；`ip_address`为上一步查询的设备ip地址。

若返回recv time out seq字样，说明两个设备之间不连通，请尝试其他设备。

**注意：** A3环境单卡双die之间不互通，如0号和1号device不通，2号和3号device不通，以此类推，在A3环境执行样例时，请注意传入的device id是否满足连通要求。

- step3：检查设备之间TLS证书配置的一致性
```shell
# 检查设备的TLS状态
for i in {0..7}; do hccn_tool -i $i -tls -g; done | grep switch

# TLS使能的设备和TLS不使能的设备无法建链，建议统一保持TLS关闭
for i in {0..7}; do hccn_tool -i $i -tls -s enable 0; done
```
**注意：** 如果执行上述命令出现`hccn_tool is busy, please try again`，请确保没有其他进行并发执行该命令，然后重试。
