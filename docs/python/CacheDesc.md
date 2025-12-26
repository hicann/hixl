# CacheDesc<a name="ZH-CN_TOPIC_0000002374412000"></a>
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


## 函数功能<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

构造CacheDesc，通常在CacheManager的allocate\_cache接口中作为参数类型使用。

## 函数原型<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
__init__(self,
                 num_tensors: int,
                 shape: Union[Tuple[int], List[int]],
                 data_type: DataType,
                 placement: Placement = Placement.DEVICE,
                 batch_dim_index: int = 0,
                 seq_len_dim_index: int = -1,
                 kv_tensor_format: str = None)
```

## 参数说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.89%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.89%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>num_tensors</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>cache中tensor的个数，操作cache时，所有tensor会做同样的操作。</p>
</td>
</tr>
<tr id="row99821205619"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1599201212562"><a name="p1599201212562"></a><a name="p1599201212562"></a>shape</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p149931218561"><a name="p149931218561"></a><a name="p149931218561"></a>Union[Tuple[int], List[int]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p09912124563"><a name="p09912124563"></a><a name="p09912124563"></a>tensor的shape。</p>
</td>
</tr>
<tr id="row426916171070"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p226918171874"><a name="p226918171874"></a><a name="p226918171874"></a>data_type</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p42693171773"><a name="p42693171773"></a><a name="p42693171773"></a>DataType</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p12699171717"><a name="p12699171717"></a><a name="p12699171717"></a>tensor的data type。</p>
</td>
</tr>
<tr id="row1154353971415"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1854443991410"><a name="p1854443991410"></a><a name="p1854443991410"></a>placement</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p1754443931410"><a name="p1754443931410"></a><a name="p1754443931410"></a>Placement</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p854411394140"><a name="p854411394140"></a><a name="p854411394140"></a>表示cache所在的设备类型。默认值Placement.DEVICE。</p>
</td>
</tr>
<tr id="row114822046171413"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p19482246151419"><a name="p19482246151419"></a><a name="p19482246151419"></a>batch_dim_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p1748212466145"><a name="p1748212466145"></a><a name="p1748212466145"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p20482174617142"><a name="p20482174617142"></a><a name="p20482174617142"></a>表示shape中batch size所在维度。默认值0，表示在第0维。</p>
</td>
</tr>
<tr id="row58601250101412"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p158601850111417"><a name="p158601850111417"></a><a name="p158601850111417"></a>seq_len_dim_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p20860135018148"><a name="p20860135018148"></a><a name="p20860135018148"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p2860145051417"><a name="p2860145051417"></a><a name="p2860145051417"></a>表示shape中seq_len所在维度。默认值-1，表示未配置。</p>
</td>
</tr>
<tr id="row6134756181417"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p513455610148"><a name="p513455610148"></a><a name="p513455610148"></a>kv_tensor_format</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p913412567144"><a name="p913412567144"></a><a name="p913412567144"></a>str</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p15134056161419"><a name="p15134056161419"></a><a name="p15134056161419"></a>表示cache的format。默认不配置。</p>
</td>
</tr>
</tbody>
</table>

## 调用示例<a name="section17821439839"></a>

```
from llm_datadist import CacheDesc
cache_desc = CacheDesc(80, [4, 2048, 1, 128], DataType.DT_FLOAT16)
```

## 返回值<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下返回CacheDesc的实例。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

## 约束说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

无

