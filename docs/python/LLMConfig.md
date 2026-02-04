# LLMConfig<a name="ZH-CN_TOPIC_0000002407891573"></a>

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


## LLMConfig构造函数<a name="ZH-CN_TOPIC_0000002407891513"></a>

**函数功能**

构造LLMConfig，调用init接口需要传入一个配置项字典，为了简化配置，可以通过此类来构造该配置项字典。

Decode和Prompt可以双向拉取Cache。该模式下的相关接口包括：enable\_cache\_manager、mem\_pool\_cfg和listen\_ip\_info。

**函数原型**

```
__init__()
```

**参数说明**

无

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
```

**返回值**

返回LLMConfig的实例。

**约束说明**

无

## generate\_options<a name="ZH-CN_TOPIC_0000002407891445"></a>

**函数功能**

生成配置项字典。

**函数原型**

```
generate_options()
```

**参数说明**

无

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
...
engine_options = llm_config.generate_options()
```

**返回值**

返回配置项字典。

**约束说明**

无

## device\_id<a name="ZH-CN_TOPIC_0000002408011669"></a>

**函数功能**

设置当前进程Device ID，对应底层ge.exec.deviceId配置项。

**函数原型**

```
device_id(device_id)
```

**参数说明**

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="31.740000000000002%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="46.04%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>device_id</p>
</td>
<td class="cellrowborder" valign="top" width="31.740000000000002%" headers="mcps1.1.4.1.2 "><p id="p8892194145216"><a name="p8892194145216"></a><a name="p8892194145216"></a>Union[int, List[int], Tuple[int]]</p>
</td>
<td class="cellrowborder" valign="top" width="46.04%" headers="mcps1.1.4.1.3 "><p id="p12594101817295"><a name="p12594101817295"></a><a name="p12594101817295"></a>设置当前进程的Device ID。当前只支持配置一个</p>
</td>
</tr>
</tbody>
</table>

**调用示例**

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.device_id = 0
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section9605173416486"></a>

无

## sync\_kv\_timeout<a name="ZH-CN_TOPIC_0000002407891577"></a>

**函数功能**

配置pull\_cache、pull\_blocks、push\_blocks、push\_cache接口的超时时间，对应底层llm.SyncKvCacheWaitTime配置项。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
sync_kv_timeout(sync_kv_timeout)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.06%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.72%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>sync_kv_timeout</p>
</td>
<td class="cellrowborder" valign="top" width="15.06%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>int或str</p>
</td>
<td class="cellrowborder" valign="top" width="62.72%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>同步kv超时时间，单位：ms。不配置默认为1000ms。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.sync_kv_timeout = 1000
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section9605173416486"></a>

无

## ge\_options<a name="ZH-CN_TOPIC_0000002408011641"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

配置额外的GE配置项。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
ge_options(ge_options)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.06%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.72%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>ge_options</p>
</td>
<td class="cellrowborder" valign="top" width="15.06%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>dict[str, str]</p>
</td>
<td class="cellrowborder" valign="top" width="62.72%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>配置GE配置项。</p>
<p id="p19907288261"><a name="p19907288261"></a><a name="p19907288261"></a>其中ge.flowGraphMemMaxSize比较重要，表示所有KV cache占用的最大内存，如果设置的过大，会压缩模型的可用内存，需根据实际情况指定。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
ge_options = {
    "ge.flowGraphMemMaxSize": "4106127360"
}
llm_config = LLMConfig()
llm_config.ge_options = ge_options
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section4233173018"></a>

无

## listen\_ip\_info<a name="ZH-CN_TOPIC_0000002374252184"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

设置集群侦听信息，对应底层llm.listenIpInfo配置项。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
listen_ip_info(listen_ip_info)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.03%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.74999999999999%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>listen_ip_info</p>
</td>
<td class="cellrowborder" valign="top" width="15.03%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>str</p>
</td>
<td class="cellrowborder" valign="top" width="62.74999999999999%" headers="mcps1.1.4.1.3 "><p id="p987521985116"><a name="p987521985116"></a><a name="p987521985116"></a>设置为Host侧的IP地址和端口，支持配置为一个，例如："192.168.1.1:26000"</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.listen_ip_info = "192.168.1.1:26000"
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section9605173416486"></a>

无

## enable\_cache\_manager<a name="ZH-CN_TOPIC_0000002374252100"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

配置是否开启CacheManager模式，对应底层llm.EnableCacheManager配置项。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
enable_cache_manager(self, enable_cache_manager: bool)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.06%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.72%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>enable_cache_manager</p>
</td>
<td class="cellrowborder" valign="top" width="15.06%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>bool</p>
</td>
<td class="cellrowborder" valign="top" width="62.72%" headers="mcps1.1.4.1.3 "><p id="p128061047267"><a name="p128061047267"></a><a name="p128061047267"></a>是否开启CacheManager模式。需配置为开启。</p>
<a name="ul1844525575"></a><a name="ul1844525575"></a><ul id="ul1844525575"><li>True：开启。</li><li>False：不开启，不配置默认为不开启。</li></ul>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.enable_cache_manager = True
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**

无

## enable\_remote\_cache\_accessible<a name="ZH-CN_TOPIC_0000002374252084"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

配置是否开启远端Cache可直接访问功能。

开启该Option后，会在本地缓存远端Cache元数据（索引，内存地址等），从而加速Pull流程。但也会引入相关约束，详见“约束说明”。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
enable_remote_cache_accessible(self, enable_remote_cache_accessible: bool)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.06%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.72%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1595964613515"><a name="p1595964613515"></a><a name="p1595964613515"></a>enable_remote_cache_accessible</p>
</td>
<td class="cellrowborder" valign="top" width="15.06%" headers="mcps1.1.4.1.2 "><p id="p3959546113517"><a name="p3959546113517"></a><a name="p3959546113517"></a>bool</p>
</td>
<td class="cellrowborder" valign="top" width="62.72%" headers="mcps1.1.4.1.3 "><p id="p16503123123411"><a name="p16503123123411"></a><a name="p16503123123411"></a>是否开启远端Cache可直接访问功能。取值如下。</p>
<a name="ul7971431112518"></a><a name="ul7971431112518"></a><ul id="ul7971431112518"><li>True：开启</li><li>False：不开启</li></ul>
<p id="p1586601805816"><a name="p1586601805816"></a><a name="p1586601805816"></a>默认为不开启。</p>
<p id="p16554132114258"><a name="p16554132114258"></a><a name="p16554132114258"></a><span id="ph1017114205714"><a name="ph1017114205714"></a><a name="ph1017114205714"></a>不开启该option时，</span><span id="ph173015529551"><a name="ph173015529551"></a><a name="ph173015529551"></a><span id="ph83591245551"><a name="ph83591245551"></a><a name="ph83591245551"></a><term id="zh-cn_topic_0000001312391781_term1253731311225"><a name="zh-cn_topic_0000001312391781_term1253731311225"></a><a name="zh-cn_topic_0000001312391781_term1253731311225"></a>Atlas A3 训练系列产品</term>/<term id="zh-cn_topic_0000001312391781_term12835255145414"><a name="zh-cn_topic_0000001312391781_term12835255145414"></a><a name="zh-cn_topic_0000001312391781_term12835255145414"></a>Atlas A3 推理系列产品</term></span>仅支持RDMA传输协议。</span><span id="ph2913612135717"><a name="ph2913612135717"></a><a name="ph2913612135717"></a>建议开启该option，以支持更多类型的传输协议。</span></p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.enable_remote_cache_accessible = True
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section4233173018"></a>

索引的更新只在第一次Pull时触发，对端在第一次Pull之后的Cache操作对本端不可见。用户需要保证Cache的有效性。所以该Option更适用于PA的场景，因为该场景下Cache只会在用户脚本初始化阶段分配/注册，而不会频繁改变。

## rdma\_traffic\_class<a name="ZH-CN_TOPIC_0000002408011677"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

用于配置RDMA网卡的traffic class。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
rdma_traffic_class(self, rdma_traffic_class: int)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.06%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.72%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1595964613515"><a name="p1595964613515"></a><a name="p1595964613515"></a>rdma_traffic_class</p>
</td>
<td class="cellrowborder" valign="top" width="15.06%" headers="mcps1.1.4.1.2 "><p id="p3959546113517"><a name="p3959546113517"></a><a name="p3959546113517"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="62.72%" headers="mcps1.1.4.1.3 "><p id="p1284073872"><a name="p1284073872"></a><a name="p1284073872"></a>用于配置RDMA网卡的traffic class。和环境变量HCCL_RDMA_TC功能，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。</p>
<p id="p1282418359148"><a name="p1282418359148"></a><a name="p1282418359148"></a>取值范围为[0,255]，且需要配置为4的整数倍，默认值为132。</p>

</td>
</tr>
</tbody>
</table>

如上表格中的环境变量请参考[《环境变量参考》](https://www.hiascend.com/document/redirect/CannCommunityEnvRef)。

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.rdma_traffic_class = 100
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section4233173018"></a>

无。

## rdma\_service\_level<a name="ZH-CN_TOPIC_0000002408011661"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

用于配置RDMA网卡的service level。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
rdma_service_level(self, rdma_service_level: int)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.06%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.72%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1595964613515"><a name="p1595964613515"></a><a name="p1595964613515"></a>rdma_service_level</p>
</td>
<td class="cellrowborder" valign="top" width="15.06%" headers="mcps1.1.4.1.2 "><p id="p3959546113517"><a name="p3959546113517"></a><a name="p3959546113517"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="62.72%" headers="mcps1.1.4.1.3 "><p id="p141217396266"><a name="p141217396266"></a><a name="p141217396266"></a>用于配置RDMA网卡的service level。和环境变量HCCL_RDMA_SL功能相同，如同时配置，当前option优先级更高；未同时配置，以配置的一方为准。</p>
<p id="p66533506502"><a name="p66533506502"></a><a name="p66533506502"></a>取值范围为[0, 7]，默认值为4。</p>
</td>
</tr>
</tbody>
</table>

如上表格中的环境变量请参考[《环境变量参考》](https://www.hiascend.com/document/redirect/CannCommunityEnvRef)。

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.rdma_service_level = 2
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section2411483221"></a>

无。

## local\_comm\_res<a name="ZH-CN_TOPIC_0000002407891469"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

用于配置本地通信资源。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
local_comm_res(local_comm_res)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.06%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.72%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p45679551409"><a name="p45679551409"></a><a name="p45679551409"></a>local_comm_res</p>
</td>
<td class="cellrowborder" valign="top" width="15.06%" headers="mcps1.1.4.1.2 "><p id="p15567165511010"><a name="p15567165511010"></a><a name="p15567165511010"></a>str</p>
</td>
<td class="cellrowborder" valign="top" width="62.72%" headers="mcps1.1.4.1.3 "><p id="p1567255406"><a name="p1567255406"></a><a name="p1567255406"></a>配置本地通信资源信息，格式是json的字符串。仅需配置ranktable中当前llm datadist所使用Device信息，无需配置ranktable中的server_count和rank_id字段。配置示例如下。</p>
<pre class="screen" id="screen125671551908"><a name="screen125671551908"></a><a name="screen125671551908"></a>{
    "server_list": [
        {
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x"
                },
            ],
            "server_id": "xxxx"
        }
    ],
    "status": "completed",
    "version": "1.0"
}</pre>
<p id="p19567125511015"><a name="p19567125511015"></a><a name="p19567125511015"></a>若未配置enable_cache_manager</a>和enable_remote_cache_accessible</a>参数，在配置了当前option后，这两个参数默认为True。</p>
<div class="note" id="note45401946203"><a name="note45401946203"></a><a name="note45401946203"></a><span class="notetitle"> 说明： </span><div class="notebody"><p id="p1753920416207"><a name="p1753920416207"></a><a name="p1753920416207"></a>该字段可以配置为空，示例如下。</p>
<p id="p13211124820206"><a name="p13211124820206"></a><a name="p13211124820206"></a>local_comm_res = ""</p>
</div></div>
</td>
</tr>
</tbody>
</table>

如上表格中ranktable具体信息请参见[《HCCL集合通信库用户指南》](https://www.hiascend.com/document/redirect/CannCommunityHcclUg)。

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.local_comm_res = '''{
    "status": "completed",
    "version": "1.0",
    "server_list": [
        {
            "server_id": "node_0",
            "device": [
                {
                    "device_id": "0",
                    "device_ip": "x.x.x.x"
                }
            ]
        }
    ]
}'''
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section2411483221"></a>

配置了该option后，存在如下约束。

-   调用link\_clusters或unlink\_clusters时，如果local\_comm\_res不为空，则不需要配置“clusters”参数中的“append\_local\_ip\_info”信息。如果local\_comm\_res为空，则需要配置“clusters”参数中的“append\_local\_ip\_info”信息。
-   当前不支持enable\_cache\_manager和enable\_remote\_cache\_accessible配置为“False”的场景。

## link\_total\_time<a name="ZH-CN_TOPIC_0000002470373425"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

用于配置HCCL建链失败的总超时时间。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
link_total_time(self, link_total_time: int)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.049999999999999%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.73%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p45679551409"><a name="p45679551409"></a><a name="p45679551409"></a>link_total_time</p>
</td>
<td class="cellrowborder" valign="top" width="15.049999999999999%" headers="mcps1.1.4.1.2 "><p id="p15567165511010"><a name="p15567165511010"></a><a name="p15567165511010"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="62.73%" headers="mcps1.1.4.1.3 "><p id="p1567255406"><a name="p1567255406"></a><a name="p1567255406"></a>用于配置HCCL的建链超时时间，单位是秒，在总超时时间内会根据重试次数来进行重试。</p>
<p id="p9613337104416"><a name="p9613337104416"></a><a name="p9613337104416"></a>取值范围为[0, 2^32-1], 默认值为0，可不配置，不配置自动传入0；传入0后会自行读取HCCL_CONNECT_TIMEOUT环境变量，默认120s。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.link_total_time = 20
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section2411483221"></a>

无。

## link\_retry\_count<a name="ZH-CN_TOPIC_0000002436814998"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

用于配置HCCL建链失败的重试次数。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
link_retry_count(link_retry_count)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="15.06%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="62.72%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p3571642194914"><a name="p3571642194914"></a><a name="p3571642194914"></a>link_retry_count</p>
</td>
<td class="cellrowborder" valign="top" width="15.06%" headers="mcps1.1.4.1.2 "><p id="p15567165511010"><a name="p15567165511010"></a><a name="p15567165511010"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="62.72%" headers="mcps1.1.4.1.3 "><p id="p197501721125019"><a name="p197501721125019"></a><a name="p197501721125019"></a>用于配置HCCL建链失败的重试次数，单位是次，在总超时时间内会根据重试次数来进行重试。</p>
<p id="p9613337104416"><a name="p9613337104416"></a><a name="p9613337104416"></a>取值范围为[1, 100]，默认值为1，可不配置，不配置自动传入1。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import LLMConfig
llm_config = LLMConfig()
llm_config.link_retry_count = 2
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

参数错误可能抛出TypeError或ValueError。

**约束说明**<a name="section2411483221"></a>

无
















