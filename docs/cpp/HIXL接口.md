# HIXL接口<a name="ZH-CN_TOPIC_0000002446743577"></a>

## 产品支持情况<a name="section8178181118225"></a>

<a name="table38301303189"></a>
<table><thead align="left"><tr id="row20831180131817"><th class="cellrowborder" valign="top" width="57.99999999999999%" id="mcps1.1.3.1.1"><p id="p1883113061818"><a name="p1883113061818"></a><a name="p1883113061818"></a><span id="ph20833205312295"><a name="ph20833205312295"></a><a name="ph20833205312295"></a>产品</span></p>
</th>
<th class="cellrowborder" align="center" valign="top" width="42%" id="mcps1.1.3.1.2"><p id="p783113012187"><a name="p783113012187"></a><a name="p783113012187"></a>是否支持</p>
</th>
</tr>
</thead>
<tbody><tr id="row220181016240"><td class="cellrowborder" valign="top" width="57.99999999999999%" headers="mcps1.1.3.1.1 "><p id="p48327011813"><a name="p48327011813"></a><a name="p48327011813"></a><span id="ph583230201815"><a name="ph583230201815"></a><a name="ph583230201815"></a><term id="zh-cn_topic_0000001312391781_term1253731311225"><a name="zh-cn_topic_0000001312391781_term1253731311225"></a><a name="zh-cn_topic_0000001312391781_term1253731311225"></a>Atlas A3 训练系列产品/Atlas A3 推理系列产品</term></span></p>
</td>
<td class="cellrowborder" align="center" valign="top" width="42%" headers="mcps1.1.3.1.2 "><p id="p7948163910184"><a name="p7948163910184"></a><a name="p7948163910184"></a>√</p>
</td>
</tr>
<tr id="row173226882415"><td class="cellrowborder" valign="top" width="57.99999999999999%" headers="mcps1.1.3.1.1 "><p id="p14832120181815"><a name="p14832120181815"></a><a name="p14832120181815"></a><span id="ph980713477118"><a name="ph980713477118"></a><a name="ph980713477118"></a><term id="zh-cn_topic_0000001312391781_term454024162214"><a name="zh-cn_topic_0000001312391781_term454024162214"></a><a name="zh-cn_topic_0000001312391781_term454024162214"></a>Atlas A2 训练系列产品/Atlas A2 推理系列产品</p>
</td>
<td class="cellrowborder" align="center" valign="top" width="42%" headers="mcps1.1.3.1.2 "><p id="p578615025316"><a name="p578615025316"></a><a name="p578615025316"></a>√</p>
</td>
</tr>
</tbody>
</table>
说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。

## HIXL构造函数<a name="ZH-CN_TOPIC_0000002446623653"></a>

**函数功能**

创建HIXL对象。

**函数原型**

```
Hixl()
```

**参数说明**

无

**返回值**

无

**异常处理**

无

**约束说明**

无

## \~Hixl\(\)<a name="ZH-CN_TOPIC_0000002413024568"></a>

**函数功能**

HIXL对象析构函数。

**函数原型**

```
~Hixl
```

**参数说明**

无

**返回值**

无

**异常处理**

无

## Initialize<a name="ZH-CN_TOPIC_0000002413184440"></a>

**函数功能**

初始化HIXL，在调用其他接口前需要先调用该接口。

**函数原型**

```
Status Initialize(const AscendString &local_engine, const std::map<AscendString, AscendString> &options)
```

**参数说明**

<a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_table47561922"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_row29169897"><th class="cellrowborder" valign="top" width="27.63%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p13951479"><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p13951479"></a><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p13951479"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="27.900000000000002%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p56327989"><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p56327989"></a><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p56327989"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="44.47%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p66531170"><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p66531170"></a><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p66531170"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1433794520405"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p6337945184013"><a name="p6337945184013"></a><a name="p6337945184013"></a>local_engine</p>
</td>
<td class="cellrowborder" valign="top" width="27.900000000000002%" headers="mcps1.1.4.1.2 "><p id="p433774524012"><a name="p433774524012"></a><a name="p433774524012"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="44.47%" headers="mcps1.1.4.1.3 "><p id="p10528111164114"><a name="p10528111164114"></a><a name="p10528111164114"></a>HIXL标识，在所有参与建链的范围内需要确保唯一。如果是ipv4，格式为host_ip:host_port或host_ip。如果是ipv6，格式为[host_ip]:host_port或[host_ip]。不建议配置为回环IP，在多个HIXL交互场景，回环IP容易冲突。</p>
<p id="p8528111134120"><a name="p8528111134120"></a><a name="p8528111134120"></a>当设置host_port且host_port&gt;0时代表当前HIXL作为Server端，需要对配置端口进行侦听。如果没设置host_port或者host_port&lt;=0代表是Client，不启动侦听。</p>
</td>
</tr>
<tr id="zh-cn_topic_0000001461072801_row17754717144"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="zh-cn_topic_0000001461072801_p16786142181615"><a name="zh-cn_topic_0000001461072801_p16786142181615"></a><a name="zh-cn_topic_0000001461072801_p16786142181615"></a>options</p>
</td>
<td class="cellrowborder" valign="top" width="27.900000000000002%" headers="mcps1.1.4.1.2 "><p id="zh-cn_topic_0000001461072801_p177869428168"><a name="zh-cn_topic_0000001461072801_p177869428168"></a><a name="zh-cn_topic_0000001461072801_p177869428168"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="44.47%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000001461072801_p14787174219161"><a name="zh-cn_topic_0000001461072801_p14787174219161"></a><a name="zh-cn_topic_0000001461072801_p14787174219161"></a>初始化参数值。具体请参考<a href="#table8396338161010">表1</a>。</p>
</td>
</tr>
</tbody>
</table>

**表 1**  options

<a name="table8396338161010"></a>
<table><thead align="left"><tr id="row739693821013"><th class="cellrowborder" valign="top" width="27.500000000000004%" id="mcps1.2.4.1.1"><p id="p18396238171019"><a name="p18396238171019"></a><a name="p18396238171019"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="13.139999999999999%" id="mcps1.2.4.1.2"><p id="p6396163811103"><a name="p6396163811103"></a><a name="p6396163811103"></a>可选/必选</p>
</th>
<th class="cellrowborder" valign="top" width="59.36%" id="mcps1.2.4.1.3"><p id="p153965385101"><a name="p153965385101"></a><a name="p153965385101"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row13383193917510"><td class="cellrowborder" valign="top" width="27.500000000000004%" headers="mcps1.2.4.1.1 "><p id="p102713422516"><a name="p102713422516"></a><a name="p102713422516"></a>OPTION_BUFFER_POOL</p>
</td>
<td class="cellrowborder" valign="top" width="13.139999999999999%" headers="mcps1.2.4.1.2 "><p id="p927042125119"><a name="p927042125119"></a><a name="p927042125119"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p102815424519"><a name="p102815424519"></a><a name="p102815424519"></a>字符串取值"BufferPool"。</p>
<p id="p128154216518"><a name="p128154216518"></a><a name="p128154216518"></a>在需要使用中转buffer进行传输的场景下:</p>
<p id="p172814245116"><a name="p172814245116"></a><a name="p172814245116"></a><span id="ph8287422519"><a name="ph8287422519"></a><a name="ph8287422519"></a>1. 不支持</span>使用HCCS协议进行Host To Host直传传输<span id="ph12819427519"><a name="ph12819427519"></a><a name="ph12819427519"></a>时。</span></p>
<p id="p5281042115119"><a name="p5281042115119"></a><a name="p5281042115119"></a>2. RDMA注册Host内存大小受限时。</p>
<p id="p13281442105110"><a name="p13281442105110"></a><a name="p13281442105110"></a>3. 多个小块内存传输(例如128K)需要使用中转传输提升性能时。</p>
<p id="p128124213518"><a name="p128124213518"></a><a name="p128124213518"></a>可使用此option配置中转内存池的大小，取值格式为"$BUFFER_NUM:$BUFFER_SIZE"，<strong id="b1906113612527"><a name="b1906113612527"></a><a name="b1906113612527"></a>系统默认会配置为"4:8(单位MB)"</strong>，可以通过配置为"0:0"来关闭中转内存池，在有并发的场景下建议增大$BUFFER_NUM个数, 另外，所有使用的地方需要配置相同的值。不支持该参数与"OPTION_ENABLE_USE_FABRIC_MEM"同时配置。 </p>
<p id="p1281242135"><a name="p1281242135"></a><a name="b19061136125"></a><a name="b19061136125"></a>说明：不配置该参数时，存在如下约束。

Atlas A2 训练系列产品/Atlas A2 推理系列产品：仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。该场景下Server采用HCCS传输协议时，仅支持D2D。

Atlas A3 训练系列产品/Atlas A3 推理系列产品：该场景下采用HCCS传输协议时，不支持Host内存作为远端Cache。</p>
</td>
</tr>
<tr id="row24374561993"><td class="cellrowborder" valign="top" width="27.500000000000004%" headers="mcps1.2.4.1.1 "><p id="p19842183617237"><a name="p19842183617237"></a><a name="p19842183617237"></a>OPTION_RDMA_TRAFFIC_CLASS</p>
</td>
<td class="cellrowborder" valign="top" width="13.139999999999999%" headers="mcps1.2.4.1.2 "><p id="p8841103642314"><a name="p8841103642314"></a><a name="p8841103642314"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p613920141278"><a name="p613920141278"></a><a name="p613920141278"></a>字符串取值"RdmaTrafficClass"。</p>
<p id="p1284073872"><a name="p1284073872"></a><a name="p1284073872"></a>用于配置RDMA网卡的traffic class。和环境变量HCCL_RDMA_TC功能，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。</p>
<p id="p544743519334"><a name="p544743519334"></a><a name="p544743519334"></a>取值范围为[0,255]，且需要配置为4的整数倍，默认值为132。</p>
</td>
</tr>
<tr id="row92022108110"><td class="cellrowborder" valign="top" width="27.500000000000004%" headers="mcps1.2.4.1.1 "><p id="p8840153616235"><a name="p8840153616235"></a><a name="p8840153616235"></a>OPTION_RDMA_SERVICE_LEVEL</p>
</td>
<td class="cellrowborder" valign="top" width="13.139999999999999%" headers="mcps1.2.4.1.2 "><p id="p1284014363233"><a name="p1284014363233"></a><a name="p1284014363233"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p1321144310716"><a name="p1321144310716"></a><a name="p1321144310716"></a>字符串取值"RdmaServiceLevel"。</p>
<p id="p141217396266"><a name="p141217396266"></a><a name="p141217396266"></a>用于配置RDMA网卡的service level。和环境变量HCCL_RDMA_SL功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。</p>
<p id="p66533506502"><a name="p66533506502"></a><a name="p66533506502"></a>取值范围为[0, 7]，默认值为4。</p>
</td>
</tr>
<tr id="row92022108110"><td class="cellrowborder" valign="top" width="27.500000000000004%" headers="mcps1.2.4.1.1 "><p id="p8840153616235"><a name="p8840153616235"></a><a name="p8840153616235"></a>OPTION_GLOBAL_RESOURCE_CONFIG</p>
</td>
<td class="cellrowborder" valign="top" width="13.139999999999999%" headers="mcps1.2.4.1.2 "><p id="p1284014363233"><a name="p1284014363233"></a><a name="p1284014363233"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p1321144310716"><a name="p1321144310716"></a><a name="p1321144310716"></a>字符串取值"GlobalResourceConfig"。</p>
<p id="p141217396266"><a name="p141217396266"></a><a name="p141217396266"></a>用于开启并配置全局资源配置。</p>
对于Fabric Mem模式，该参数配置实例如下：</p>

```
{ 
    "fabric_memory.max_capacity": 128, //虚拟内存池的大小。取值范围：(0, 1042]之间的整数，默认值：64，单位TB。
    "fabric_memory.task_stream_num": 1, //单个传输任务使用的流数量。取值范围：[1, 8]之间的整数，默认值：4。
}
```

对于链路池机制。该参数配置示例如下：</p>

```
{ 
    "channel_pool.max_channel": "10", //最大的链路个数。取值范围：(0, 512]之间的整数，默认值：512
    
    "channel_pool.high_waterline": "0.3", //触发链路销毁的高水位，取值范围：（0，1）之间的小数，需要和channel_pool.low_waterline同时配置
    
    "channel_pool.low_waterline": "0.1" //触发链路销毁的低水位，取值范围：（0，1）之间小数，并且小于高水位
 }
```


链路池工作时，实际依据链路个数判断是否进行销毁，如果当前链路个数已经达到高水位对应的链路个数，则选择（当前链路个数-低水位对应的链路个数 ）条链路进行销毁（如存在正在传输的任务，则不会销毁），再建链。相关参数计算公式如下：
 - 高水位线对应的链路个数=max(1,static_cast<int32_t> (channel_pool.max_channel * channel_pool.high_waterline)) 
 - 低水位线对应的链路个数=max(1,static_cast<int32_t> (channel_pool.max_channel * channel_pool.low_waterline)) 
 
 在上述配置示例中，按照计算公式，高水位对应的链路个数=3，低水位对应的链路个数=1。每次建链前会检查当前HIXL内的链路是否达到3，如果已经达到，选择(当前链路个数-1 )条链路进行销毁（如存在正在传输的任务，则不会销毁），再建链。 
 
 当启用链路池机制时，有如下注意事项：
 
 - 集群内的所有Hixl Engine都需要配置OPTION_GLOBAL_RESOURCE_CONFIG。
 - 当调用TransferSync或者TransferAsycn接口时，若不存在相关链路，将执行建链操作。 
 - 会增加传输和建链的额外开销，可能导致性能下降。

</td>
</tr>
<tr id="row92022108110"><td class="cellrowborder" valign="top" width="27.500000000000004%" headers="mcps1.2.4.1.1 "><p id="p8840153616235"><a name="p8840153616235"></a><a name="p8840153616235"></a>OPTION_ENABLE_USE_FABRIC_MEM</p>
</td>
<td class="cellrowborder" valign="top" width="13.139999999999999%" headers="mcps1.2.4.1.2 "><p id="p1284014363233"><a name="p1284014363233"></a><a name="p1284014363233"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p1321144310716"><a name="p1321144310716"></a><a name="p1321144310716"></a>字符串取值"EnableUseFabricMem"。 

- 0：不开启Fabric Mem模式 
- 1：开启Fabric Mem模式 

此option适用于需要使用HCCS进行D2RH、RH2D传输的场景。 

说明：集群场景下，该参数在所有节点需要配置为相同的值。不支持该参数与"OPTION_BUFFER_POOL"同时配置。仅支持Atlas A3 训练系列产品/Atlas A3 推理系列产品。</p>

</td>
</tr>
</tbody>
</table>

如上表格中的环境变量请参考[《环境变量参考》](https://www.hiascend.com/document/redirect/CannCommunityEnvRef)。

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   其他：失败

**异常处理**

无

**约束说明**

1.  需要和Finalize配对使用，初始化成功后，任何退出前都需要先调用Finalize保证资源释放，否则会出现资源释放顺序不符合预期而导致问题。
2.  初始化前需要先调用aclrtSetDevice。

## Finalize<a name="ZH-CN_TOPIC_0000002446743581"></a>

**函数功能**

HIXL资源清理函数。

**函数原型**

```
void Finalize()
```

**参数说明**

无

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

无

**异常处理**

无

**约束说明**

-   需要和Initialize配对使用。
-   建议在调用Finalize前，链路进行断链以及对注册的内存进行解注册。
-   Server需要等所有Client完成断链后调用，如果Server提前退出，Client断链以及数据传输过程会发生报错。
-   当Client需要操作Server端地址进行远端读写，Server端需要等Client完成远端读写之后才调用该接口，否则会出现失败。
-   该接口不能和其他接口并发调用。

## RegisterMem<a name="ZH-CN_TOPIC_0000002446623657"></a>

**函数功能**

注册内存地址。用于TransferSync调用指定本地内存地址和远端内存地址，TransferSync指定的地址可以为注册的地址子集，其中本地内存地址需在当前HIXL进行注册，远端内存地址需要在远端HIXL进行注册。

**函数原型**

```
Status RegisterMem(const MemDesc &mem, MemType type, MemHandle &mem_handle)
```

**参数说明**

<a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_table47561922"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_row29169897"><th class="cellrowborder" valign="top" width="27.63%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p13951479"><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p13951479"></a><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p13951479"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="27.900000000000002%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p56327989"><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p56327989"></a><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p56327989"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="44.47%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p66531170"><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p66531170"></a><a name="zh-cn_topic_0000001461072801_zh-cn_topic_0000001265240866_zh-cn_topic_0182636394_p66531170"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row1492251518437"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p0922141513439"><a name="p0922141513439"></a><a name="p0922141513439"></a>mem</p>
</td>
<td class="cellrowborder" valign="top" width="27.900000000000002%" headers="mcps1.1.4.1.2 "><p id="p59221515164315"><a name="p59221515164315"></a><a name="p59221515164315"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="44.47%" headers="mcps1.1.4.1.3 "><p id="p199225150433"><a name="p199225150433"></a><a name="p199225150433"></a>需要注册的内存的描述信息。类型为MemDesc</a>。</p>
</td>
</tr>
<tr id="row173202214435"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p153213226433"><a name="p153213226433"></a><a name="p153213226433"></a>type</p>
</td>
<td class="cellrowborder" valign="top" width="27.900000000000002%" headers="mcps1.1.4.1.2 "><p id="p132162219439"><a name="p132162219439"></a><a name="p132162219439"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="44.47%" headers="mcps1.1.4.1.3 "><p id="p13327221433"><a name="p13327221433"></a><a name="p13327221433"></a>需要注册的内存的类型。类型为MemType</a>。</p>
</td>
</tr>
<tr id="zh-cn_topic_0000001461072801_row17754717144"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="zh-cn_topic_0000001461072801_p16786142181615"><a name="zh-cn_topic_0000001461072801_p16786142181615"></a><a name="zh-cn_topic_0000001461072801_p16786142181615"></a>mem_handle</p>
</td>
<td class="cellrowborder" valign="top" width="27.900000000000002%" headers="mcps1.1.4.1.2 "><p id="zh-cn_topic_0000001461072801_p177869428168"><a name="zh-cn_topic_0000001461072801_p177869428168"></a><a name="zh-cn_topic_0000001461072801_p177869428168"></a>输出</p>
</td>
<td class="cellrowborder" valign="top" width="44.47%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000001461072801_p14787174219161"><a name="zh-cn_topic_0000001461072801_p14787174219161"></a><a name="zh-cn_topic_0000001461072801_p14787174219161"></a>注册成功返回的内存handle, 可用于内存解注册。类型为MemHandle</a>。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

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

## DeregisterMem<a name="ZH-CN_TOPIC_0000002413024572"></a>

**函数功能**

解注册内存。

**函数原型**

```
Status DeregisterMem(MemHandle mem_handle)
```

参数说明

<a name="table1072194315344"></a>
<table><thead align="left"><tr id="row272174315340"><th class="cellrowborder" valign="top" width="27.63%" id="mcps1.1.4.1.1"><p id="p17721243153415"><a name="p17721243153415"></a><a name="p17721243153415"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="14.84%" id="mcps1.1.4.1.2"><p id="p1973194312342"><a name="p1973194312342"></a><a name="p1973194312342"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="57.53%" id="mcps1.1.4.1.3"><p id="p14731434345"><a name="p14731434345"></a><a name="p14731434345"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row127384363416"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p1547895819449"><a name="p1547895819449"></a><a name="p1547895819449"></a>mem_handle</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p947715874419"><a name="p947715874419"></a><a name="p947715874419"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p747785834413"><a name="p747785834413"></a><a name="p747785834413"></a>调用RegisterMem</a>接口注册内存返回的内存handle。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   其他：失败。

**异常处理**

无

**约束说明**

-   调用该接口前需要先调用Disconnect将所有链路进行断链，确保所有内存不再使用。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。

## Connect<a name="ZH-CN_TOPIC_0000002413184444"></a>

**函数功能**

与远端HIXL进行建链。

**函数原型**

```
Status Connect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

<a name="table1072194315344"></a>
<table><thead align="left"><tr id="row272174315340"><th class="cellrowborder" valign="top" width="27.63%" id="mcps1.1.4.1.1"><p id="p17721243153415"><a name="p17721243153415"></a><a name="p17721243153415"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="14.84%" id="mcps1.1.4.1.2"><p id="p1973194312342"><a name="p1973194312342"></a><a name="p1973194312342"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="57.53%" id="mcps1.1.4.1.3"><p id="p14731434345"><a name="p14731434345"></a><a name="p14731434345"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row127384363416"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p1848294431713"><a name="p1848294431713"></a><a name="p1848294431713"></a>remote_engine</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p54816445176"><a name="p54816445176"></a><a name="p54816445176"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p8481444141719"><a name="p8481444141719"></a><a name="p8481444141719"></a>远端HIXL的唯一标识。remote_engine对应的HIXL需要是同一个Server。</p>
</td>
</tr>
<tr id="row1073144320343"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p548044410177"><a name="p548044410177"></a><a name="p548044410177"></a>timeout_in_millis</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p10480244101717"><a name="p10480244101717"></a><a name="p10480244101717"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p18479114414175"><a name="p18479114414175"></a><a name="p18479114414175"></a>建链的超时时间，单位：ms，默认值：1000。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

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
-   建议超时时间配置200ms以上。
-   调用该接口前需提前注册所有本地以及远端内存，否则建链后注册不支持远端访问。
-   容器场景需在容器内映射“/etc/hccn.conf”文件或者确保默认路径“/usr/local/Ascend/driver/tools”下存在hccn_tool，如果两者都不能满足，则需要用户将hccn_tool所在路径配置到PATH中。配置实例如下，hccn_tool_install_path表示hccn_tool所在路径。

    ```
    export PATH=$PATH:{hccn_tool_install_path}
    ```

-   如果并发建链，建链使用的某个卡是down的状态，可能导致某些链路建链超时。如果需要非down状态的卡建链成功，则需要对所有涉及建链的卡配置如下命令。

    ```
    hccn_tool -i ${device_id} -tls -s enable 1
    ```

-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。

## Disconnect<a name="ZH-CN_TOPIC_0000002446743585"></a>

**函数功能**

与远端HIXL进行断链。

**函数原型**

```
Status Disconnect(const AscendString &remote_engine, int32_t timeout_in_millis = 1000)
```

**参数说明**

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.89%" id="mcps1.1.4.1.2"><p id="p1973194312342"><a name="p1973194312342"></a><a name="p1973194312342"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="41.89%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1848294431713"><a name="p1848294431713"></a><a name="p1848294431713"></a>remote_engine</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p54816445176"><a name="p54816445176"></a><a name="p54816445176"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p8481444141719"><a name="p8481444141719"></a><a name="p8481444141719"></a>远端HIXL的唯一标识。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p548044410177"><a name="p548044410177"></a><a name="p548044410177"></a>timeout_in_millis</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p10480244101717"><a name="p10480244101717"></a><a name="p10480244101717"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p18479114414175"><a name="p18479114414175"></a><a name="p18479114414175"></a>断链的超时时间，单位：ms，默认值：1000。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   NOT\_CONNECTED：没有与对端创建链接
-   其他：失败

**约束说明**

-   调用该接口之前，需要先调用Initialize接口完成初始化。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。

## TransferSync<a name="ZH-CN_TOPIC_0000002446623661"></a>

**函数功能**

与远端HIXL进行内存传输。

**函数原型**

```
Status TransferSync(const AscendString &remote_engine,
                    TransferOp operation,
                    const std::vector<TransferOpDesc> &op_descs,
                    int32_t timeout_in_millis = 1000)
```

**参数说明**

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.89%" id="mcps1.1.4.1.2"><p id="p1973194312342"><a name="p1973194312342"></a><a name="p1973194312342"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="41.89%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p172169417217"><a name="p172169417217"></a><a name="p172169417217"></a>remote_engine</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p18215541182113"><a name="p18215541182113"></a><a name="p18215541182113"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p11215124113212"><a name="p11215124113212"></a><a name="p11215124113212"></a>远端HIXL的唯一标识。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p19214141152111"><a name="p19214141152111"></a><a name="p19214141152111"></a>operation</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p156821517152217"><a name="p156821517152217"></a><a name="p156821517152217"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p1621384113213"><a name="p1621384113213"></a><a name="p1621384113213"></a>将远端内存读到本地或者将本地内存写到远端。</p>
</td>
</tr>
<tr id="row343165825214"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p821384192116"><a name="p821384192116"></a><a name="p821384192116"></a>op_descs</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p268420176224"><a name="p268420176224"></a><a name="p268420176224"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p152125419216"><a name="p152125419216"></a><a name="p152125419216"></a>批量操作的本地以及远端地址。</p>
</td>
</tr>
<tr id="row031215135318"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p72121241182115"><a name="p72121241182115"></a><a name="p72121241182115"></a>timeout_in_millis</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p668661720228"><a name="p668661720228"></a><a name="p668661720228"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p18479114414175"><a name="p18479114414175"></a><a name="p18479114414175"></a>传输的超时时间，单位：ms，默认值：1000。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   NOT\_CONNECTED：没有与对端创建链接
-   TIMEOUT：传输超时
-   RESOURCE_EXHAUSTED：资源耗尽
-   其他：失败

**约束说明**

-   调用该接口之前，需要先调用Connect接口完成与对端的建链或者在HIXL初始化时开启了链路池机制（通过配置options中的OPTION_GLOBAL_RESOURCE_CONFIG参数进行开启）。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
-   系统默认开启中转内存池，如果op\_desc内包含<256K的，则默认使用中转传输模式来提升性能，否则会通过判断是否有未注册的内存来决定走中转还是直传。
-   在开启中转内存池情况下，op\_desc中本地内存和远端内存有一个未注册就会判断为需要走中转传输模式，且没有注册过的内存判断为Host内存，用户需保证地址合法。
-   在中转传输模式下，所有op\_desc的传输类型需要相同，举例：所有的op\_desc都是本地Host内存往远端Host内存写。

## TransferAsync

**函数功能**

与远端HIXL进行批量异步内存传输。

**函数原型**

```
  Status TransferAsync(const AscendString &remote_engine,
                       TransferOp operation,
                       const std::vector<TransferOpDesc> &op_descs,
                       const TransferArgs &optional_args,
                       TransferReq &req)
```

**参数说明**

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.89%" id="mcps1.1.4.1.2"><p id="p1973194312342"><a name="p1973194312342"></a><a name="p1973194312342"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="41.89%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p172169417217"><a name="p172169417217"></a><a name="p172169417217"></a>remote_engine</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p18215541182113"><a name="p18215541182113"></a><a name="p18215541182113"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p11215124113212"><a name="p11215124113212"></a><a name="p11215124113212"></a>远端HIXL的唯一标识。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p19214141152111"><a name="p19214141152111"></a><a name="p19214141152111"></a>operation</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p156821517152217"><a name="p156821517152217"></a><a name="p156821517152217"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p1621384113213"><a name="p1621384113213"></a><a name="p1621384113213"></a>将远端内存读到本地或者将本地内存写到远端。</p>
</td>
</tr>
<tr id="row343165825214"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p821384192116"><a name="p821384192116"></a><a name="p821384192116"></a>op_descs</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p268420176224"><a name="p268420176224"></a><a name="p268420176224"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p152125419216"><a name="p152125419216"></a><a name="p152125419216"></a>批量操作的本地以及远端地址。</p>
</td>
</tr>
<tr id="row343165825214"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p821384192116"><a name="p821384192116"></a><a name="p821384192116"></a>optional_args</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p268420176224"><a name="p268420176224"></a><a name="p268420176224"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p152125419216"><a name="p152125419216"></a><a name="p152125419216"></a>可选参数（预留）。</p>
</td>
</tr>
<tr id="row031215135318"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p72121241182115"><a name="p72121241182115"></a><a name="p72121241182115"></a>req</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p668661720228"><a name="p668661720228"></a><a name="p668661720228"></a>输出</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p18479114414175"><a name="p18479114414175"></a><a name="p18479114414175"></a>请求的句柄，用户查询传输的请求状态。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

```
  //初始化客户端和服务端engine，并完成链接
  client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req);
```

**返回值**

-   SUCCESS：成功
-   NOT\_CONNECTED：没有与对端创建链接
-   RESOURCE_EXHAUSTED：资源耗尽
-   其他：失败

**约束说明**

-   调用该接口之前，需要先调用Connect接口完成与对端的建链或者在HIXL初始化时开启了链路池机制（通过配置options中的OPTION_GLOBAL_RESOURCE_CONFIG参数进行开启）。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
-   当前异步传输仅支持直传，暂不支持中转传输，默认直传。

## GetTransferStatus

**函数功能**

获取异步内存传输的状态。

**函数原型**

```
  Status GetTransferStatus(const TransferReq &req, TransferStatus &status)
```

**参数说明**

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.89%" id="mcps1.1.4.1.2"><p id="p1973194312342"><a name="p1973194312342"></a><a name="p1973194312342"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="41.89%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p172169417217"><a name="p172169417217"></a><a name="p172169417217"></a>req</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p18215541182113"><a name="p18215541182113"></a><a name="p18215541182113"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p11215124113212"><a name="p11215124113212"></a><a name="p11215124113212"></a>请求的句柄，通过调用TransferAsync产生。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p19214141152111"><a name="p19214141152111"></a><a name="p19214141152111"></a>status</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p156821517152217"><a name="p156821517152217"></a><a name="p156821517152217"></a>输出</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p1621384113213"><a name="p1621384113213"></a><a name="p1621384113213"></a>传输状态，枚举值如下。

-   WAITING
-   COMPLETED
-   TIMEOUT（暂不支持）
-   FAILED
</p>


</td>
</tr>
</tbody>
</table>

**调用示例**

```
  //初始化客户端和服务端engine，并完成链接
  Status transfer_status = client_engine.TransferAsync(remote_engine, operation, op_descs, optional_args, req)；
  //req是TransferAsync()的输出值，使用这个请求句柄进行传输状态查询
  Status query_status = GetTransferStatus(req, status);
  //对传输状态进行检查，判断传输是否完成
  ...
```

**返回值**

-   SUCCESS：成功
-   PARAM\_INVALID：参数错误
-   NOT\_CONNECTED：没有与对端创建链接
-   其他：失败

**约束说明**

-   调用该接口之前，需要先调用Connect接口完成与对端的建链。
-   该接口需要和Initialize运行在同一个线程上，如需切换线程调用该接口，需要在Initialize所在线程调用“aclrtGetCurrentContext”获取context，并在新线程调用“aclrtSetCurrentContext”设置context。
-   在调用TransferAsync接口进行异步传输后，需要使用该接口查询对应请求状态，如果查询状态是COMPLETED或FAILED，将释放相关资源。该场景下不支持再次查询。
-   异步传输时，用户自行判断是否超时，如果用户判断任务超时，建议调用Disconnect接口销毁链路，清理相关资源。
-   异步传输任务失败后，调用该接口查询的状态和接口返回状态都是FAILED。


## SendNotify

**函数功能**

向远端engine发送Notify信息。

**函数原型**

```
  Status SendNotify(const AscendString &remote_engine,
                    const NotifyDesc &notify,
                    int32_t timeout_in_millis = 1000)
```

**参数说明**

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.89%" id="mcps1.1.4.1.2"><p id="p1973194312342"><a name="p1973194312342"></a><a name="p1973194312342"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="41.89%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p172169417217"><a name="p172169417217"></a><a name="p172169417217"></a>remote_engine</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p18215541182113"><a name="p18215541182113"></a><a name="p18215541182113"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p11215124113212"><a name="p11215124113212"></a><a name="p11215124113212"></a>远端Hixl的唯一标识</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p19214141152111"><a name="p19214141152111"></a><a name="p19214141152111"></a>timeout_in_millis</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p156821517152217"><a name="p156821517152217"></a><a name="p156821517152217"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p1621384113213"><a name="p1621384113213"></a><a name="p1621384113213"></a>发送超时时间，单位ms。
</p>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p19214141152111"><a name="p19214141152111"></a><a name="p19214141152111"></a>notify</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p156821517152217"><a name="p156821517152217"></a><a name="p156821517152217"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p1621384113213"><a name="p1621384113213"></a><a name="p1621384113213"></a>要发送的Notify内容。内容中的notify_msg和name长度上限均为1024字符。
</p>

</td>
</tr>
</tbody>
</table>

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

获取当前Hixl内所有Server收到的Notify信息，并清空已收到信息。

**函数原型**

```
  Status GetNotifies(std::vector<NotifyDesc> &notifies)
```

**参数说明**

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.89%" id="mcps1.1.4.1.2"><p id="p1973194312342"><a name="p1973194312342"></a><a name="p1973194312342"></a>输入/输出</p>
</th>
<th class="cellrowborder" valign="top" width="41.89%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p172169417217"><a name="p172169417217"></a><a name="p172169417217"></a>notifies</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p18215541182113"><a name="p18215541182113"></a><a name="p18215541182113"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p11215124113212"><a name="p11215124113212"></a><a name="p11215124113212"></a>存放notify信息的vector。</p>
</td>
</tr>

</td>
</tr>
</tbody>
</table>

**调用示例**

无

**返回值**

-   SUCCESS：成功
-   其他：失败

**约束说明**

无。


