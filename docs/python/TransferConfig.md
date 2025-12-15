# TransferConfig<a name="ZH-CN_TOPIC_0000002407891545"></a>

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


## 函数功能<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

构造TransferConfig。

## 函数原型<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
__init__(dst_cluster_id: int, dst_addrs: List[int], src_layer_range: Optional[range] = None, src_batch_index: int = 0)
```

## 参数说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_table66993202"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_row41236172"><th class="cellrowborder" valign="top" width="27.63%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p51795644"><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p51795644"></a><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p51795644"></a>参数名</p>
</th>
<th class="cellrowborder" valign="top" width="25.580000000000002%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p34697616"><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p34697616"></a><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p34697616"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="46.79%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p17794566"><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p17794566"></a><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p17794566"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_row32073719"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p47834478"><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p47834478"></a><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p47834478"></a>dst_cluster_id</p>
</td>
<td class="cellrowborder" valign="top" width="25.580000000000002%" headers="mcps1.1.4.1.2 "><p id="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p49387472"><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p49387472"></a><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p49387472"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="46.79%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p29612923"><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p29612923"></a><a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312720989_p29612923"></a>目的Cache所在实例的cluster_id。</p>
</td>
</tr>
<tr id="row11804113011"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p118941173010"><a name="p118941173010"></a><a name="p118941173010"></a>dst_addrs</p>
</td>
<td class="cellrowborder" valign="top" width="25.580000000000002%" headers="mcps1.1.4.1.2 "><p id="p13894183016"><a name="p13894183016"></a><a name="p13894183016"></a>List[int]</p>
</td>
<td class="cellrowborder" valign="top" width="46.79%" headers="mcps1.1.4.1.3 "><p id="p0167134151519"><a name="p0167134151519"></a><a name="p0167134151519"></a>目的Cache中各tensor的内存地址。如果目的Cache为非PA场景，且需要传输到的batch_index非0，则此处需要将dst_addrs偏移到实际地址。</p>
</td>
</tr>
<tr id="row778116534512"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p478120554515"><a name="p478120554515"></a><a name="p478120554515"></a>src_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="25.580000000000002%" headers="mcps1.1.4.1.2 "><p id="p37813516450"><a name="p37813516450"></a><a name="p37813516450"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="46.79%" headers="mcps1.1.4.1.3 "><p id="p378111513451"><a name="p378111513451"></a><a name="p378111513451"></a>本地要传输的层的范围，step只支持为1，默认为None，表示传输所有层。</p>
</td>
</tr>
<tr id="row227948114517"><td class="cellrowborder" valign="top" width="27.63%" headers="mcps1.1.4.1.1 "><p id="p428048184518"><a name="p428048184518"></a><a name="p428048184518"></a>src_batch_index</p>
</td>
<td class="cellrowborder" valign="top" width="25.580000000000002%" headers="mcps1.1.4.1.2 "><p id="p82803817456"><a name="p82803817456"></a><a name="p82803817456"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="46.79%" headers="mcps1.1.4.1.3 "><p id="p142803854512"><a name="p142803854512"></a><a name="p142803854512"></a>本地cache的batch下标。当源Cache为非PA场景时可以设置。</p>
</td>
</tr>
</tbody>
</table>

## 调用示例<a name="section17821439839"></a>

```
from llm_datadist import TransferConfig
TransferConfig(1, dst_addrs, range(0, 3), 1)
```

## 返回值<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下返回TransferConfig的实例。

参数错误可能抛出TypeError或ValueError。

src\_layer\_range不合法会抛出LLMException。

## 约束说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

目标地址列表中地址的个数需要为需要传输的层数的2倍。

