# LLM-DataDist接口<a name="ZH-CN_TOPIC_0000002408009645"></a>
## 产品支持情况<a name="section8178181118225"></a>

<a name="table38301303189"></a>
<table><thead align="left"><tr id="row20831180131817"><th class="cellrowborder" valign="top" width="57.99999999999999%" id="mcps1.1.3.1.1"><p id="p1883113061818"><a name="p1883113061818"></a><a name="p1883113061818"></a><span id="ph20833205312295"><a name="ph20833205312295"></a><a name="ph20833205312295"></a>产品</span></p>
</th>
<th class="cellrowborder" align="center" valign="top" width="42%" id="mcps1.1.3.1.2"><p id="p783113012187"><a name="p783113012187"></a><a name="p783113012187"></a>是否支持</p>
</th>
</tr>
</thead>
<tbody><tr id="row220181016240"><td class="cellrowborder" valign="top" width="57.99999999999999%" headers="mcps1.1.3.1.1 "><p id="p48327011813"><a name="p48327011813"></a><a name="p48327011813"></a><span id="ph583230201815"><a name="ph583230201815"></a><a name="ph583230201815"></a><term id="zh-cn_topic_0000001312391781_term1253731311225"><a name="zh-cn_topic_0000001312391781_term1253731311225"></a><a name="zh-cn_topic_0000001312391781_term1253731311225"></a>Ascend 910C</term></span></p>
</td>
<td class="cellrowborder" align="center" valign="top" width="42%" headers="mcps1.1.3.1.2 "><p id="p7948163910184"><a name="p7948163910184"></a><a name="p7948163910184"></a>√</p>
</td>
</tr>
<tr id="row173226882415"><td class="cellrowborder" valign="top" width="57.99999999999999%" headers="mcps1.1.3.1.1 "><p id="p14832120181815"><a name="p14832120181815"></a><a name="p14832120181815"></a><span id="ph980713477118"><a name="ph980713477118"></a><a name="ph980713477118"></a><term id="zh-cn_topic_0000001312391781_term454024162214"><a name="zh-cn_topic_0000001312391781_term454024162214"></a><a name="zh-cn_topic_0000001312391781_term454024162214"></a>Ascend 910B</p>
</td>
<td class="cellrowborder" align="center" valign="top" width="42%" headers="mcps1.1.3.1.2 "><p id="p578615025316"><a name="p578615025316"></a><a name="p578615025316"></a>√</p>
</td>
</tr>
</tbody>
</table>
说明：针对Ascend 910B，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。


## LlmDataDist构造函数<a name="ZH-CN_TOPIC_0000002374409960"></a>
**函数功能**

创建LLM-DataDist对象。

**函数原型**

```
LlmDataDist(uint64_t cluster_id, LlmRole role)
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
<tbody><tr id="row127384363416"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p573184319342"><a name="p573184319342"></a><a name="p573184319342"></a>cluster_id</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p97316437346"><a name="p97316437346"></a><a name="p97316437346"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p157364353411"><a name="p157364353411"></a><a name="p157364353411"></a>集群ID。LlmDataDist标识，在所有参与建链的范围内需要确保唯一。</p>
</td>
</tr>
<tr id="row287273317215"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p7873123392118"><a name="p7873123392118"></a><a name="p7873123392118"></a>role</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p178731033142118"><a name="p178731033142118"></a><a name="p178731033142118"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p887323392114"><a name="p887323392114"></a><a name="p887323392114"></a>类型是LlmRole</a>，该参数只用于标识当前角色，对传输过程无影响。</p>
</td>
</tr>
</tbody>
</table>

**返回值**

无

**异常处理**

无

**约束说明**

无
## \~LlmDataDist\(\)<a name="ZH-CN_TOPIC_0000002374409940"></a>
**函数功能**

LLM-DataDist对象析构函数。

**函数原型**

```
~LlmDataDist()
```

**参数说明**

无

**返回值**

无

**异常处理**

无

**约束说明**

无
## Initialize<a name="ZH-CN_TOPIC_0000002408009617"></a>
**函数功能**

初始化LLM-DataDist。

**函数原型**

```
Status Initialize(const std::map<AscendString, AscendString> &options)
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
<tbody><tr id="zh-cn_topic_0000001461072801_row17754717144"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="zh-cn_topic_0000001461072801_p16786142181615"><a name="zh-cn_topic_0000001461072801_p16786142181615"></a><a name="zh-cn_topic_0000001461072801_p16786142181615"></a>options</p>
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
<table><thead align="left"><tr id="row739693821013"><th class="cellrowborder" valign="top" width="27.54%" id="mcps1.2.4.1.1"><p id="p18396238171019"><a name="p18396238171019"></a><a name="p18396238171019"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="13.100000000000001%" id="mcps1.2.4.1.2"><p id="p6396163811103"><a name="p6396163811103"></a><a name="p6396163811103"></a>可选/必选</p>
</th>
<th class="cellrowborder" valign="top" width="59.36%" id="mcps1.2.4.1.3"><p id="p153965385101"><a name="p153965385101"></a><a name="p153965385101"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row24374561993"><td class="cellrowborder" valign="top" width="27.54%" headers="mcps1.2.4.1.1 "><p id="p134383568918"><a name="p134383568918"></a><a name="p134383568918"></a>OPTION_LISTEN_IP_INFO</p>
</td>
<td class="cellrowborder" valign="top" width="13.100000000000001%" headers="mcps1.2.4.1.2 "><p id="p16438125619917"><a name="p16438125619917"></a><a name="p16438125619917"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p1697422413310"><a name="p1697422413310"></a><a name="p1697422413310"></a>配置当前option表示LLM-DataDist是Server，不配置表示Client。</p>
<p id="p321613269222"><a name="p321613269222"></a><a name="p321613269222"></a>当LLM-DataDist是Server时，需配置Host侧的IP地址和端口。</p>
<p id="p4913155192718"><a name="p4913155192718"></a><a name="p4913155192718"></a>配置示例：如"192.168.1.1:26000"，不支持传入多个IP地址和端口。</p>
</td>
</tr>
<tr id="row92022108110"><td class="cellrowborder" valign="top" width="27.54%" headers="mcps1.2.4.1.1 "><p id="p33441120414"><a name="p33441120414"></a><a name="p33441120414"></a>OPTION_DEVICE_ID</p>
</td>
<td class="cellrowborder" valign="top" width="13.100000000000001%" headers="mcps1.2.4.1.2 "><p id="p882672716113"><a name="p882672716113"></a><a name="p882672716113"></a>必选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p1082619275117"><a name="p1082619275117"></a><a name="p1082619275117"></a>设置当前进程的Device ID，如"0"，不支持单进程多卡场景。</p>
</td>
</tr>
<tr id="row171171314519"><td class="cellrowborder" valign="top" width="27.54%" headers="mcps1.2.4.1.1 "><p id="p6344132018110"><a name="p6344132018110"></a><a name="p6344132018110"></a>OPTION_SYNC_CACHE_WAIT_TIME</p>
</td>
<td class="cellrowborder" valign="top" width="13.100000000000001%" headers="mcps1.2.4.1.2 "><p id="p7826327710"><a name="p7826327710"></a><a name="p7826327710"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p1982682716111"><a name="p1982682716111"></a><a name="p1982682716111"></a>kv相关操作的超时时间，单位：ms。不配置默认为1000ms。相关接口如下。</p>
<a name="ul024317191379"></a><a name="ul024317191379"></a><ul id="ul024317191379"><li>PullKvCache</li><li>PullKvBlocks</a></li><li>PushKvCache</a></li><li>PushKvBlocks</li></ul>
</td>
</tr>
<tr id="row3779142413711"><td class="cellrowborder" valign="top" width="27.54%" headers="mcps1.2.4.1.1 "><p id="p1569211573813"><a name="p1569211573813"></a><a name="p1569211573813"></a>OPTION_LOCAL_COMM_RES</p>
</td>
<td class="cellrowborder" valign="top" width="13.100000000000001%" headers="mcps1.2.4.1.2 "><p id="p1078042453719"><a name="p1078042453719"></a><a name="p1078042453719"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="59.36%" headers="mcps1.2.4.1.3 "><p id="p197801724143716"><a name="p197801724143716"></a><a name="p197801724143716"></a>配置本地通信资源信息，格式是json格式的字符串。仅需配置ranktable中当前llm datadist所使用Device信息，无需配置ranktable中的server_count和rank_id字段。</p>
<div class="note" id="note45401946203"><a name="note45401946203"></a><a name="note45401946203"></a><span class="notetitle"> 说明： </span><div class="notebody"><p id="p1753920416207"><a name="p1753920416207"></a><a name="p1753920416207"></a>该option可以不配置或配置为空串，为空将自动生成相关信息。</p>
</div></div>
</td>
</tr>
</tbody>
</table>

如上表格中ranktable具体信息请参见[《HCCL集合通信库用户指南》](https://www.hiascend.com/document/redirect/CannCommunityHcclUg)。

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：成功
-   LLM\_PARAM\_INVALID：参数错误
-   其他：失败

**异常处理**

无

**约束说明**

需要和Finalize配对使用，初始化成功后，任何退出前都需要调用Finalize保证资源释放，否则会出现资源释放顺序不符合预期而导致问题。
## Finalize<a name="ZH-CN_TOPIC_0000002374250052"></a>
**函数功能**
LLM-DataDist的资源释放函数。

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
-   当增量集群从全量集群拉取KV的时候，需要保证全量集群在增量集群完成同步KV数据之后才调用该接口，否则会出现失败。
-   当全量集群在同步KV数据到增量集群的过程中，需要保证增量集群在全量集群完成同步KV数据之后调用该接口，否则会出现失败。
-   该接口不能和其他接口并发调用。

## SetRole<a name="ZH-CN_TOPIC_0000002374250080"></a>
**函数功能**

设置LLM-DataDist的角色。

**函数原型**

```
Status SetRole(LlmRole role, const std::map<AscendString, AscendString> &options = {})
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
<tbody><tr id="row114441755111717"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p18444115515178"><a name="p18444115515178"></a><a name="p18444115515178"></a>role</p>
</td>
<td class="cellrowborder" valign="top" width="27.900000000000002%" headers="mcps1.1.4.1.2 "><p id="p24441755151713"><a name="p24441755151713"></a><a name="p24441755151713"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="44.47%" headers="mcps1.1.4.1.3 "><p id="p744475516171"><a name="p744475516171"></a><a name="p744475516171"></a>角色类型，类型为LlmRole</a>。</p>
</td>
</tr>
<tr id="zh-cn_topic_0000001461072801_row17754717144"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="zh-cn_topic_0000001461072801_p16786142181615"><a name="zh-cn_topic_0000001461072801_p16786142181615"></a><a name="zh-cn_topic_0000001461072801_p16786142181615"></a>options</p>
</td>
<td class="cellrowborder" valign="top" width="27.900000000000002%" headers="mcps1.1.4.1.2 "><p id="zh-cn_topic_0000001461072801_p177869428168"><a name="zh-cn_topic_0000001461072801_p177869428168"></a><a name="zh-cn_topic_0000001461072801_p177869428168"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="44.47%" headers="mcps1.1.4.1.3 "><p id="p1747474145718"><a name="p1747474145718"></a><a name="p1747474145718"></a>设置角色的参数，当前支持的参数请参见<a href="#table1987921348">表1</a>。</p>
</td>
</tr>
</tbody>
</table>

**表 1**  配置项

<a name="table1987921348"></a>
<table><thead align="left"><tr id="row29871121843"><th class="cellrowborder" valign="top" width="33.333333333333336%" id="mcps1.2.4.1.1"><p id="p1945121135220"><a name="p1945121135220"></a><a name="p1945121135220"></a>配置项</p>
</th>
<th class="cellrowborder" valign="top" width="33.29332933293329%" id="mcps1.2.4.1.2"><p id="p5451611105216"><a name="p5451611105216"></a><a name="p5451611105216"></a>可选/必选</p>
</th>
<th class="cellrowborder" valign="top" width="33.373337333733375%" id="mcps1.2.4.1.3"><p id="p1645111175210"><a name="p1645111175210"></a><a name="p1645111175210"></a>描述</p>
</th>
</tr>
</thead>
<tbody><tr id="row79878211049"><td class="cellrowborder" valign="top" width="33.333333333333336%" headers="mcps1.2.4.1.1 "><p id="p134383568918"><a name="p134383568918"></a><a name="p134383568918"></a>OPTION_LISTEN_IP_INFO</p>
</td>
<td class="cellrowborder" valign="top" width="33.29332933293329%" headers="mcps1.2.4.1.2 "><p id="p16438125619917"><a name="p16438125619917"></a><a name="p16438125619917"></a>可选</p>
</td>
<td class="cellrowborder" valign="top" width="33.373337333733375%" headers="mcps1.2.4.1.3 "><a name="ul551253217325"></a><a name="ul551253217325"></a><ul id="ul551253217325"><li>当LLM-DataDist初始化是Client时，如果需要切换为Server，则配置该option为侦听的Host的IP地址和端口，配置示例："192.168.1.1:26000"。否则不需要配置。</li><li>当LLM-DataDist初始化是Server时，若不配置该option，则表示切换为Client。若配置了该opiton，则表示当前仍是Server，如果配置的端口号与初始化不一致，以当前配置为准。配置示例："192.168.1.1:26001"。</li></ul>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：设置角色成功
-   LLM\_PARAM\_INVALID：参数错误
-   LLM\_EXIST\_LINK：存在残留链路资源
-   其他：失败

**异常处理**

无

**约束说明**

使用前需与当前DataDist的链路进行断链。
## LinkLlmClusters<a name="ZH-CN_TOPIC_0000002374409976"></a>
**函数功能**

在LlmDataDist之间执行建链，Client端调用。

**函数原型**

```
Status LinkLlmClusters(const std::vector<ClusterInfo> &clusters, std::vector<Status> &rets, int32_t timeout_in_millis = 1000)
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
<tbody><tr id="row127384363416"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p573184319342"><a name="p573184319342"></a><a name="p573184319342"></a>clusters</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p97316437346"><a name="p97316437346"></a><a name="p97316437346"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p157364353411"><a name="p157364353411"></a><a name="p157364353411"></a>需要建链的集群信息。类型为ClusterInfo</a>。需要配置ClusterInfo</a>中的remote_cluster_id和remote_ip_infos信息，若Initialize(LlmDataDist)</a>未指定OPTION_LOCAL_COMM_RES, 需额外配置local_ip_infos，每个ClusterInfo</a>的local_ip_infos和remote_ip_infos仅支持配置一个ip_info。</p>
</td>
</tr>
<tr id="row1073144320343"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p10731643143410"><a name="p10731643143410"></a><a name="p10731643143410"></a>rets</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p1673144303415"><a name="p1673144303415"></a><a name="p1673144303415"></a>输出</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p1573184323418"><a name="p1573184323418"></a><a name="p1573184323418"></a>每个cluster建链结果。</p>
</td>
</tr>
<tr id="row1047310453312"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p137315438340"><a name="p137315438340"></a><a name="p137315438340"></a>timeout_in_millis</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p15732043133419"><a name="p15732043133419"></a><a name="p15732043133419"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p4731943153418"><a name="p4731943153418"></a><a name="p4731943153418"></a>建链超时时间，单位ms。默认1000ms。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：只有所有clusters建链成功，接口才会返回成功。
-   其他：建链失败，需要查看rets每个cluster的建链结果。

**异常处理**

-   LLM\_ALREADY\_LINK：当前的cluster已经和远端cluster建立了链接。
-   LLM\_LINK\_FAILED：建链失败。

**约束说明**

-   调用该接口前，需要先在Client和Server调用Initialize接口完成初始化。
-   允许创建的最大通信数量=512，建链数量过多存在内存OOM及KV Cache传输的性能风险。
-   建议超时时间配置200ms以上。
-   调用该接口前Client和Server需提前注册所有内存，否则建链后注册不支持远端访问。
-   容器场景若未配置OPTION\_LOCAL\_COMM\_RES或配置为空，需在容器内映射“/etc/hccn.conf”文件或者确保默认路径“/usr/local/Ascend/driver/tools”下存在hccn_tool，如果两者都不能满足，则需要用户将hccn_tool所在路径配置到PATH中。配置实例如下，hccn_tool_install_path表示hccn_tool所在路径。
    ```
    export PATH=$PATH:{hccn_tool_install_path}
    ```
-   如果并发建链，建链使用的某个卡是down的状态，可能导致某些链路建链超时。如果需要非down状态的卡建链成功，则需要对所有涉及建链的卡配置如下命令。

    ```
    hccn_tool -i ${device_id} -tls -s enable 1
    ```
## UnlinkLlmClusters<a name="ZH-CN_TOPIC_0000002374409932"></a>
**函数功能**

在LlmDataDist之间执行断链。

**函数原型**

```
Status UnlinkLlmClusters(const std::vector<ClusterInfo> &clusters, std::vector<Status> &rets, int32_t timeout_in_millis = 1000, bool force_flag = false)
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
<tbody><tr id="row127384363416"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p573184319342"><a name="p573184319342"></a><a name="p573184319342"></a>clusters</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p97316437346"><a name="p97316437346"></a><a name="p97316437346"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p157364353411"><a name="p157364353411"></a><a name="p157364353411"></a>需要断链的cluster信息。类型为ClusterInfo</a>。</p>
</td>
</tr>
<tr id="row1073144320343"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p10731643143410"><a name="p10731643143410"></a><a name="p10731643143410"></a>rets</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p1673144303415"><a name="p1673144303415"></a><a name="p1673144303415"></a>输出</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p1573184323418"><a name="p1573184323418"></a><a name="p1573184323418"></a>每个cluster断链结果。</p>
</td>
</tr>
<tr id="row3607205573311"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p137315438340"><a name="p137315438340"></a><a name="p137315438340"></a>timeout_in_millis</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p15732043133419"><a name="p15732043133419"></a><a name="p15732043133419"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p4731943153418"><a name="p4731943153418"></a><a name="p4731943153418"></a>断链超时时间，单位ms。</p>
</td>
</tr>
<tr id="row279116234114"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p1379292313118"><a name="p1379292313118"></a><a name="p1379292313118"></a>force_flag</p>
</td>
<td class="cellrowborder" valign="top" width="14.84%" headers="mcps1.1.4.1.2 "><p id="p1679282381114"><a name="p1679282381114"></a><a name="p1679282381114"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="57.53%" headers="mcps1.1.4.1.3 "><p id="p17922236112"><a name="p17922236112"></a><a name="p17922236112"></a>是否为强制断链。默认为否。</p>
<p id="p0175144618255"><a name="p0175144618255"></a><a name="p0175144618255"></a>强制断链仅强制拆除本端链接，所以两端都要调用。</p>
<p id="p6750185819225"><a name="p6750185819225"></a><a name="p6750185819225"></a>非强制断链在Client端发起，不存在故障时两端链路都会拆除。但在存在链路故障时还需要在Server端发起调用强制断链，耗时长。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   SUCCESS：只有所有clusters断链成功，接口才会返回成功。
-   其他：执行断链失败，需要查看rets每个cluster的断链结果。

**异常处理**

LLM\_UNLINK\_FAILED：断链失败。

**约束说明**

调用该接口前，需要先调用Initialize接口完成初始化。
## PullKvCache<a name="ZH-CN_TOPIC_0000002408009661"></a>
**函数功能**

从远端节点拉取Cache到本地Cache。

**函数原型**
```
Status PullKvCache(const CacheIndex &src_cache_index,
                   const Cache &dst_cache,
                   uint32_t batch_index = 0U,
                   int64_t size = -1,
                   const KvCacheExtParam &ext_param = {})
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
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>src_cache_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>远端源Cache的索引。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p155319520521"><a name="p155319520521"></a><a name="p155319520521"></a>dst_cache</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p1753155219528"><a name="p1753155219528"></a><a name="p1753155219528"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7531125205210"><a name="p7531125205210"></a><a name="p7531125205210"></a>本地目的Cache。仅需指定调用RegisterKvCache</a>返回的cache_id。</p>
</td>
</tr>
<tr id="row343165825214"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143111585528"><a name="p143111585528"></a><a name="p143111585528"></a>batch_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p14431205885212"><a name="p14431205885212"></a><a name="p14431205885212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p18431125825212"><a name="p18431125825212"></a><a name="p18431125825212"></a>本地目的batch的下标。</p>
</td>
</tr>
<tr id="row031215135318"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p53128115533"><a name="p53128115533"></a><a name="p53128115533"></a>size</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p6313917531"><a name="p6313917531"></a><a name="p6313917531"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p37063585311"><a name="p37063585311"></a><a name="p37063585311"></a>设置为&gt;0的整数，表示要拉取的大小，单位字节。</p>
<p id="p136582910548"><a name="p136582910548"></a><a name="p136582910548"></a>或设置为-1，表示完整拉取。</p>
<p id="p7374185016429"><a name="p7374185016429"></a><a name="p7374185016429"></a>默认为-1。</p>
</td>
</tr>
<tr id="row17174135010526"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1017414502528"><a name="p1017414502528"></a><a name="p1017414502528"></a>ext_param</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p161741250195212"><a name="p161741250195212"></a><a name="p161741250195212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p201241217175212"><a name="p201241217175212"></a><a name="p201241217175212"></a>当前支持ext_param中src_layer_range的second与first的差值和dst_layer_range的second与first的差值一致。src_layer_range和dst_layer_range的first和second默认值都是-1，表示全部的层。取值范围都是[0, 最大可用层索引]，且first小于等于second。 最大可用层索引值的计算公式如下。</p>
<p id="p86211532018"><a name="p86211532018"></a><a name="p86211532018"></a><strong id="b7861234132013"><a name="b7861234132013"></a><a name="b7861234132013"></a>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1</strong></p>
<p id="p2046117207558"><a name="p2046117207558"></a><a name="p2046117207558"></a>当前支持tensor_num_per_layer取值范围是[1, 当前cache的tensor总数]，默认值为2。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：成功
-   LLM\_PARAM\_INVALID：参数错误
-   LLM\_NOT\_YET\_LINK：与远端cluster没有建链
-   LLM\_TIMEOUT：拉取超时
-   LLM\_KV\_CACHE\_NOT\_EXIST：本地或远端KV Cache不存在
-   其他：失败

**约束说明**

该接口调用之前，需要先调用LinkLlmClusters接口完成初始化。
## PullKvBlocks<a name="ZH-CN_TOPIC_0000002408009609"></a>
**函数功能**

通过block列表的方式，从远端节点拉取Cache到本地Cache。

**函数原型**

```
Status PullKvBlocks(const CacheIndex &src_cache_index,
                    const Cache &dst_cache,
                    const std::vector<uint64_t> &src_blocks,
                    const std::vector<uint64_t> &dst_blocks,
                    const KvCacheExtParam &ext_param = {})
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
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>src_cache_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>远端源Cache的索引。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p155319520521"><a name="p155319520521"></a><a name="p155319520521"></a>dst_cache</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p1753155219528"><a name="p1753155219528"></a><a name="p1753155219528"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7531125205210"><a name="p7531125205210"></a><a name="p7531125205210"></a>本地目的Cache。仅需指定调用RegisterKvCache</a>返回的cache_id。</p>
</td>
</tr>
<tr id="row343165825214"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143111585528"><a name="p143111585528"></a><a name="p143111585528"></a>src_blocks</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p14431205885212"><a name="p14431205885212"></a><a name="p14431205885212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p18431125825212"><a name="p18431125825212"></a><a name="p18431125825212"></a>远端源Cache的block index列表。</p>
</td>
</tr>
<tr id="row031215135318"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p53128115533"><a name="p53128115533"></a><a name="p53128115533"></a>dst_blocks</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p6313917531"><a name="p6313917531"></a><a name="p6313917531"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p158806301569"><a name="p158806301569"></a><a name="p158806301569"></a>本地目的Cache的block index列表。</p>
</td>
</tr>
<tr id="row17174135010526"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1017414502528"><a name="p1017414502528"></a><a name="p1017414502528"></a>ext_param</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p161741250195212"><a name="p161741250195212"></a><a name="p161741250195212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000002408009661_p201241217175212"><a name="zh-cn_topic_0000002408009661_p201241217175212"></a><a name="zh-cn_topic_0000002408009661_p201241217175212"></a>当前支持ext_param中src_layer_range的second与first的差值和dst_layer_range的second与first的差值一致。src_layer_range和dst_layer_range的first和second默认值都是-1，表示全部的层。取值范围都是[0, 最大可用层索引]，且first小于等于second。 最大可用层索引值的计算公式如下。</p>
<p id="zh-cn_topic_0000002408009661_p86211532018"><a name="zh-cn_topic_0000002408009661_p86211532018"></a><a name="zh-cn_topic_0000002408009661_p86211532018"></a><strong id="zh-cn_topic_0000002408009661_b7861234132013"><a name="zh-cn_topic_0000002408009661_b7861234132013"></a><a name="zh-cn_topic_0000002408009661_b7861234132013"></a>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1</strong></p>
<p id="zh-cn_topic_0000002408009661_p2046117207558"><a name="zh-cn_topic_0000002408009661_p2046117207558"></a><a name="zh-cn_topic_0000002408009661_p2046117207558"></a>当前支持tensor_num_per_layer取值范围是[1, 当前cache的tensor总数]，默认值为2。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：成功
-   LLM\_PARAM\_INVALID：参数错误
-   LLM\_NOT\_YET\_LINK：与远端cluster没有建链
-   LLM\_TIMEOUT：拉取超时
-   LLM\_KV\_CACHE\_NOT\_EXIST：远端KV Cache不存在
-   其他：失败

**约束说明**

该接口调用之前，需要先调用LinkLlmClusters接口完成初始化。

## PushKvCache<a name="ZH-CN_TOPIC_0000002374409924"></a>
**函数功能**

推送Cache到远端节点。

**函数原型**

```
Status PushKvCache(const Cache &src_cache,
                   const CacheIndex &dst_cache_index,
                   uint32_t src_batch_index = 0U,
                   int64_t size = -1,
                   const KvCacheExtParam &ext_param = {});
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
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>src_cache</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p10164174973619"><a name="p10164174973619"></a><a name="p10164174973619"></a>本地目的Cache。仅需指定调用RegisterKvCache</a>返回的cache_id。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p155319520521"><a name="p155319520521"></a><a name="p155319520521"></a>dst_cache_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p1753155219528"><a name="p1753155219528"></a><a name="p1753155219528"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7531125205210"><a name="p7531125205210"></a><a name="p7531125205210"></a>远端目的Cache的索引。</p>
</td>
</tr>
<tr id="row343165825214"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143111585528"><a name="p143111585528"></a><a name="p143111585528"></a>src_batch_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p14431205885212"><a name="p14431205885212"></a><a name="p14431205885212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p18431125825212"><a name="p18431125825212"></a><a name="p18431125825212"></a>本地源batch的下标。</p>
</td>
</tr>
<tr id="row031215135318"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p53128115533"><a name="p53128115533"></a><a name="p53128115533"></a>size</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p6313917531"><a name="p6313917531"></a><a name="p6313917531"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p136582910548"><a name="p136582910548"></a><a name="p136582910548"></a>当前只支持默认值-1。</p>
</td>
</tr>
<tr id="row17174135010526"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1017414502528"><a name="p1017414502528"></a><a name="p1017414502528"></a>ext_param</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p161741250195212"><a name="p161741250195212"></a><a name="p161741250195212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000002408009661_p201241217175212"><a name="zh-cn_topic_0000002408009661_p201241217175212"></a><a name="zh-cn_topic_0000002408009661_p201241217175212"></a>当前支持ext_param中src_layer_range的second与first的差值和dst_layer_range的second与first的差值一致。src_layer_range和dst_layer_range的first和second默认值都是-1，表示全部的层。取值范围都是[0, 最大可用层索引]，且first小于等于second。 最大可用层索引值的计算公式如下。</p>
<p id="zh-cn_topic_0000002408009661_p86211532018"><a name="zh-cn_topic_0000002408009661_p86211532018"></a><a name="zh-cn_topic_0000002408009661_p86211532018"></a><strong id="zh-cn_topic_0000002408009661_b7861234132013"><a name="zh-cn_topic_0000002408009661_b7861234132013"></a><a name="zh-cn_topic_0000002408009661_b7861234132013"></a>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1</strong></p>
<p id="zh-cn_topic_0000002408009661_p2046117207558"><a name="zh-cn_topic_0000002408009661_p2046117207558"></a><a name="zh-cn_topic_0000002408009661_p2046117207558"></a>当前支持tensor_num_per_layer取值范围是[1, 当前cache的tensor总数]，默认值为2。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：成功
-   LLM\_PARAM\_INVALID：参数错误
-   LLM\_NOT\_YET\_LINK：与远端cluster没有建链
-   LLM\_TIMEOUT：推送超时
-   LLM\_KV\_CACHE\_NOT\_EXIST：本地或远端KV Cache不存在
-   其他：失败

**约束说明**

该接口调用之前，需要先调用LinkLlmClusters接口完成初始化。


## PushKvBlocks<a name="ZH-CN_TOPIC_0000002374250060"></a>
**函数功能**

通过block列表的方式，推送Cache到远端节点。

**函数原型**

```
Status PushKvBlocks(const Cache &src_cache,
                    const CacheIndex &dst_cache_index,
                    const std::vector<uint64_t> &src_blocks,
                    const std::vector<uint64_t> &dst_blocks,
                    const KvCacheExtParam &ext_param = {});
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
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>src_cache</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p6471853113618"><a name="p6471853113618"></a><a name="p6471853113618"></a>本地目的Cache。仅需指定调用RegisterKvCache</a>返回的cache_id。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p155319520521"><a name="p155319520521"></a><a name="p155319520521"></a>dst_cache_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p1753155219528"><a name="p1753155219528"></a><a name="p1753155219528"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7531125205210"><a name="p7531125205210"></a><a name="p7531125205210"></a>远端目的Cache的索引。</p>
</td>
</tr>
<tr id="row343165825214"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143111585528"><a name="p143111585528"></a><a name="p143111585528"></a>src_blocks</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p14431205885212"><a name="p14431205885212"></a><a name="p14431205885212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p18431125825212"><a name="p18431125825212"></a><a name="p18431125825212"></a>源Cache的block index列表。</p>
</td>
</tr>
<tr id="row031215135318"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p53128115533"><a name="p53128115533"></a><a name="p53128115533"></a>dst_blocks</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p6313917531"><a name="p6313917531"></a><a name="p6313917531"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p158806301569"><a name="p158806301569"></a><a name="p158806301569"></a>目的Cache的block index列表。</p>
</td>
</tr>
<tr id="row17174135010526"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1017414502528"><a name="p1017414502528"></a><a name="p1017414502528"></a>ext_param</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p161741250195212"><a name="p161741250195212"></a><a name="p161741250195212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000002408009661_p201241217175212"><a name="zh-cn_topic_0000002408009661_p201241217175212"></a><a name="zh-cn_topic_0000002408009661_p201241217175212"></a>当前支持ext_param中src_layer_range的second与first的差值和dst_layer_range的second与first的差值一致。src_layer_range和dst_layer_range的first和second默认值都是-1，表示全部的层。取值范围都是[0, 最大可用层索引]，且first小于等于second。 最大可用层索引值的计算公式如下。</p>
<p id="zh-cn_topic_0000002408009661_p86211532018"><a name="zh-cn_topic_0000002408009661_p86211532018"></a><a name="zh-cn_topic_0000002408009661_p86211532018"></a><strong id="zh-cn_topic_0000002408009661_b7861234132013"><a name="zh-cn_topic_0000002408009661_b7861234132013"></a><a name="zh-cn_topic_0000002408009661_b7861234132013"></a>(CacheDesc::num_tensors / KvCacheExtParam::tensor_num_per_layer) - 1</strong></p>
<p id="zh-cn_topic_0000002408009661_p2046117207558"><a name="zh-cn_topic_0000002408009661_p2046117207558"></a><a name="zh-cn_topic_0000002408009661_p2046117207558"></a>当前支持tensor_num_per_layer取值范围是[1, 当前cache的tensor总数]，默认值为2。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：成功
-   LLM\_PARAM\_INVALID：参数错误
-   LLM\_NOT\_YET\_LINK：与远端cluster没有建链
-   LLM\_TIMEOUT：推送超时
-   LLM\_KV\_CACHE\_NOT\_EXIST：本地或远端KV Cache不存在
-   其他：失败

**约束说明**

该接口调用之前，需要先调用LinkLlmClusters接口完成初始化。
## RegisterKvCache<a name="ZH-CN_TOPIC_0000002408009653"></a>
**函数功能**

注册本地KV Cache内存。

**函数原型**

```
Status RegisterKvCache(const CacheDesc &cache_desc,
                       const std::vector<uint64_t> &addrs,
                       const RegisterCfg &cfg,
                       int64_t &cache_id);
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
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>cache_desc</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>本地Cache的描述信息。</p>
</td>
</tr>
<tr id="row4531052105216"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p155319520521"><a name="p155319520521"></a><a name="p155319520521"></a>addrs</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p1753155219528"><a name="p1753155219528"></a><a name="p1753155219528"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7531125205210"><a name="p7531125205210"></a><a name="p7531125205210"></a>本地Cache的地址。地址个数不超过240。</p>
</td>
</tr>
<tr id="row343165825214"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143111585528"><a name="p143111585528"></a><a name="p143111585528"></a>cfg</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p14431205885212"><a name="p14431205885212"></a><a name="p14431205885212"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p18431125825212"><a name="p18431125825212"></a><a name="p18431125825212"></a>预留参数。</p>
</td>
</tr>
<tr id="row031215135318"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p53128115533"><a name="p53128115533"></a><a name="p53128115533"></a>cache_id</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p6313917531"><a name="p6313917531"></a><a name="p6313917531"></a>输出</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p158806301569"><a name="p158806301569"></a><a name="p158806301569"></a>注册的Cache的ID。可用于后续调用传输kv接口时构造Cache。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：成功
-   其他：失败

**约束说明**

需要在Initialize接口初始化完成后调用。

最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越多。
## UnregisterKvCache<a name="ZH-CN_TOPIC_0000002407889505"></a>
**函数功能**

解除注册本地KV Cache内存。

**函数原型**

```
Status UnregisterKvCache(int64_t cache_id);
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
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>cache_id</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>输入</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>本地注册生成的cache ID。若指定的cache_id不存在默认返回成功。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

请参考[样例运行](../../examples/cpp/README.md)。

**返回值**

-   LLM\_SUCCESS：成功
-   其他：失败

**约束说明**

调用该接口前，需要先调用Initialize接口完成初始化。cache\_id必须为RegisterKvCache接口返回的值。












