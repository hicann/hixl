# 待废弃_ADXL接口

## 产品支持情况

|产品|是否支持|
|--|:-:|
|Ascend 950PR/Ascend 950DT|√|
|Atlas A3 训练系列产品/Atlas A3 推理系列产品|√|
|Atlas A2 训练系列产品/Atlas A2 推理系列产品|√|

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。

## AdxlEngine构造函数

**函数功能**

创建AdxlEngine对象。

**函数原型**

```
AdxlEngine()
```

**参数说明**

无

**返回值**

无

**异常处理**

无

**约束说明**

无


## \~AdxlEngine\(\)

**函数功能**

AdxlEngine对象析构函数。

**函数原型**

```
~AdxlEngine()
```

**参数说明**

无

**返回值**

无

**异常处理**

无

**约束说明**

无


## Initialize

**函数功能**

初始化AdxlEngine，在调用其他接口前需要先调用该接口。

**函数原型**

```
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options)
```

**参数说明**

|参数名|输入/输出|描述|
|--|--|--|
|local_engine|输入|AdxlEngine标识，在所有参与建链的范围内需要确保唯一。格式为host_ip:host_port或host_ip，不建议配置为回环IP，在多个AdxlEngine交互场景，回环IP容易冲突。当设置host_port且host_port>0时， 则当前AdxlEngine作为Server端需要对配置端口进行侦听。如果没设置host_port或者host_port<=0代表是Client，不启动侦听。|
|options|输入|初始化参数值。具体请参考表1。|


**表 1**  options

|参数名|可选/必选|描述|
|--|--|--|
|OPTION_BUFFER_POOL|可选|字符串取值"BufferPool"。在需要使用中转buffer进行传输的场景下:不支持使用HCCS协议进行Host To Host直传传输时。RDMA注册Host内存大小受限时。多个小块内存传输(例如128K)需要使用中转传输提升性能时。可使用此option配置中转内存池的大小，取值格式为"$BUFFER_NUM:$BUFFER_SIZE"，**系统默认会配置为"4:8(单位MB)"**，可以通过配置为"0:0"来关闭中转内存池，在有并发的场景下建议增大$BUFFER_NUM个数, 另外，所有使用的地方需要配置相同的值。|
|OPTION_RDMA_TRAFFIC_CLASS|可选|字符串取值"RdmaTrafficClass"。用于配置RDMA网卡的traffic class。和环境变量HCCL_RDMA_TC功能，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。取值范围为[0,255]，且需要配置为4的整数倍，默认值为132。更多信息请参考《环境变量参考》。|
|OPTION_RDMA_SERVICE_LEVEL|可选|字符串取值"RdmaServiceLevel"。用于配置RDMA网卡的service level。和环境变量HCCL_RDMA_SL功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。取值范围为[0, 7]，默认值为4。更多信息请参考《环境变量参考》。|


**调用示例**

单击[Gitee](https://gitee.com/ascend/samples/tags)，根据“标签名”下载配套版本的sample包，从“cplusplus/level1\_single\_api/12\_adxl”目录中获取样例。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   其他：失败

**异常处理**

无

**约束说明**

1.  需要和Finalize配对使用，初始化成功后，任何退出前都需要先调用Finalize保证资源释放，否则会出现资源释放顺序不符合预期而导致问题。
2.  初始化前需要先调用“aclrtSetDevice”。


## Finalize

**函数功能**

AdxlEngine资源清理函数。

**函数原型**

```
void Finalize()
```

**参数说明**

无

**调用示例**

单击[Gitee](https://gitee.com/ascend/samples/tags)，根据“标签名”下载配套版本的sample包，从“cplusplus/level1\_single\_api/12\_adxl”目录中获取样例。

**返回值**

无

**异常处理**

无

**约束说明**

-   需要和Initialize配对使用。
-   建议在调用Finalize前，链路进行断链以及对注册的内存进行解注册。
-   Server需要等所有Client完成断链后调用该接口，如果Server提前退出，Client断链以及数据传输过程会发生报错。
-   当Client需要操作Server端地址进行远端读写，Server端需要等Client完成远端读写之后才调用该接口；否则会出现失败。
-   该接口不能和其他接口并发调用。


## RegisterMem

**函数功能**

注册内存地址。用于TransferSync调用指定本地内存地址和远端内存地址，TransferSync指定的地址可以为注册的地址子集，其中本地内存地址需在当前AdxlEngine进行注册，远端内存地址需要在远端AdxlEngine进行注册。

**函数原型**

```
Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle)
```

**参数说明**

|参数名|输入/输出|描述|
|--|--|--|
|mem|输入|需要注册的内存的描述信息。类型为MemDesc。|
|type|输入|需要注册的内存的类型。类型为MemType。|
|mem_handle|输出|注册成功返回的内存handle, 可用于内存解注册。类型为MemHandle。|


**调用示例**

单击[Gitee](https://gitee.com/ascend/samples/tags)，根据“标签名”下载配套版本的sample包，从“cplusplus/level1\_single\_api/12\_adxl”目录中获取样例。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   其他：失败

**异常处理**

无

**约束说明**

-   在调用Connect与对端建链之前需要完成所有local内存的注册。
-   单进程支持注册的内存个数上限是256。
-   最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越多。
-   注册Host内存需使用“aclrtMallocHost”进行申请，该接口申请的内存地址自动对齐。
-   注册Device内存使用“aclrtMalloc”进行申请，如通过HCCS传输，则内存分配规则需配置为ACL\_MEM\_MALLOC\_HUGE\_ONLY。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。


## DeregisterMem

**函数功能**

解注册内存。

**函数原型**

```
Status DeregisterMem(MemHandle mem_handle)
```

**参数说明**

|参数名|输入/输出|描述|
|--|--|--|
|mem_handle|输入|调用RegisterMem接口注册内存返回的内存handle。|


**调用示例**

单击[Gitee](https://gitee.com/ascend/samples/tags)，根据“标签名”下载配套版本的sample包，从“cplusplus/level1\_single\_api/12\_adxl”目录中获取样例。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   其他：失败。

**异常处理**

无

**约束说明**

-   调用该接口前需要先调用Disconnect将所有链路进行断链，确保所有内存不再使用。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。


## Connect

**函数功能**

与远端AdxlEngine进行建链。

**函数原型**

```
Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

|参数名|输入/输出|描述|
|--|--|--|
|remote_engine|输入|远端AdxlEngine的唯一标识。remote_engine对应的AdxlEngine需要是同一个Server。|
|timeout_in_millis|输入|建链的超时时间，单位：ms，默认值：1000。|


**调用示例**

单击[Gitee](https://gitee.com/ascend/samples/tags)，根据“标签名”下载配套版本的sample包，从“cplusplus/level1\_single\_api/12\_adxl”目录中获取样例。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   TIMEOUT：建链超时
-   ALREADY\_CONNECTED：重复建链
-   其他：失败

**异常处理**

无。

**约束说明**

-   需要在Client和Server的Initialize接口初始化完成后调用。
-   允许创建的最大通信数量=512，建链数量过多存在内存OOM及KV Cache传输的性能风险。
-   建议超时时间配置为200ms以上。如果TLS处于开启状态，建议超时时间配置为2000ms以上。查询TLS状态可以使用如下命令：

    hccn\_tool \[-i %d\] -tls -g \[host\]

-   调用该接口前需提前注册所有本地以及远端内存，否则建链后注册不支持远端访问。
-   容器场景需在容器内映射“/etc/hccn.conf”文件或者确保默认路径“/usr/local/Ascend/driver/tools”下存在hccn\_tool，如果两者都不能满足，则需要用户将hccn\_tool所在路径配置到PATH中。配置示例如下，hccn\_tool\_install\_path表示hccn\_tool所在路径。

    ```
    export PATH=$PATH:${hccn_tool_install_path}
    ```

-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。


## Disconnect

**函数功能**

与远端AdxlEngine进行断链。

**函数原型**

```
Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

|**参数名称**|输入/输出|**取值说明**|
|--|--|--|
|remote_engine|输入|远端AdxlEngine的唯一标识。|
|timeout_in_millis|输入|断链的超时时间，单位：ms，默认值：1000。|


**调用示例**

单击[Gitee](https://gitee.com/ascend/samples/tags)，根据“标签名”下载配套版本的sample包，从“cplusplus/level1\_single\_api/12\_adxl”目录中获取样例。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   NOT\_CONNECTED：没有与对端创建链接
-   其他：失败

**约束说明**

-   调用该接口之前，需要先调用Initialize接口完成初始化。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。


## TransferSync

**函数功能**

与远端AdxlEngine进行内存传输。

**函数原型**

```
Status TransferSync(const AscendString &remote_engine,
                    TransferOp operation,
                    const std::vector<TransferOpDesc> &op_descs,
                    int32_t timeout_in_millis = 1000)
```

**参数说明**

|**参数名称**|输入/输出|**取值说明**|
|--|--|--|
|remote_engine|输入|远端AdxlEngine的唯一标识。|
|operation|输入|将远端内存读到本地或者将本地内存写到远端。|
|op_descs|输入|批量操作的本地以及远端地址。|
|timeout_in_millis|输入|断链的超时时间，单位：ms，默认值：1000。|


**调用示例**

单击[Gitee](https://gitee.com/ascend/samples/tags)，根据“标签名”下载配套版本的sample包，从“cplusplus/level1\_single\_api/12\_adxl”目录中获取样例。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   NOT\_CONNECTED：没有与对端创建链接
-   TIMEOUT：传输超时
-   其他：失败

**约束说明**

-   调用该接口之前，需要先调用Connect接口完成与对端的建链。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
-   系统默认开启中转内存池，如果op\_descs中存在<256K的数据，则默认使用中转传输模式来提升性能，否则会通过判断是否有未注册的内存来决定走中转还是直传。
-   在开启中转内存池情况下，op\_descs中本地内存和远端内存如有一个未注册就会判断为需要采用中转传输模式，且没有注册过的内存判断为Host内存，用户需保证地址合法。
-   在中转传输模式下，所有op\_descs的传输类型需要相同，举例：所有的op\_descs都是本地Host内存往远端Host内存写。


## SendNotify

**函数功能**

向远端engine发送Notify信息。

**函数原型**

```
Status SendNotify(const AscendString &remote_engine, const NotifyDesc &notify, int32_t timeout_in_millis = 1000)
```

**参数说明**

|**参数名称**|输入/输出|**取值说明**|
|--|--|--|
|remote_engine|输入|远端AdxlEngine的唯一标识|
|notify|输入|需要发送的Notify内容。内容中的name和notify_msg长度上限均为1024字符。|
|timeout_in_millis|输入|发送超时时间，单位ms。缺省值：1000|


**调用示例**

无

**返回值**

-   SUCCESS：成功
-   其他：失败

**约束说明**

-   调用该接口之前，需要先调用Connect接口完成与对端的建链。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。


## GetNotifies

**函数功能**

获取当前AdxlEngine内所有Server收到的Notify信息，并清空已收到信息。

**函数原型**

```
Status GetNotifies(std::vector<NotifyDesc> &notifies)
```

**参数说明**

|**参数名称**|输入/输出|**取值说明**|
|--|--|--|
|notifies|输入|存放Notify信息的vector。|


**调用示例**

无

**返回值**

-   SUCCESS：成功
-   其他：失败

**约束说明**

-   调用该接口之前，需要先调用Connect接口完成与对端的建链。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。


## MallocMem

**函数功能**

申请内存。

**函数原型**

```
Status MallocMem(MemType type, size_t size, void **ptr);
```

**参数说明**

|**参数名称**|输入/输出|**取值说明**|
|--|--|--|
|type|输入|内存类型。|
|size|输入|内存大小。|
|ptr|输出|申请的内存指针。|


**调用示例**

无

**返回值**

-   SUCCESS：成功
-   其他：失败

**约束说明**

只支持申请Host内存。


## FreeMem

**函数功能**

释放内存。

**函数原型**

```
Status FreeMem(void *ptr)
```

**参数说明**

|**参数名称**|输入/输出|**取值说明**|
|--|--|--|
|ptr|输入|要释放的内存指针。|


**调用示例**

无

**返回值**

-   SUCCESS：成功
-   其他：失败

**约束说明**

无。


