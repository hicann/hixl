# LayerSynchronizer<a name="ZH-CN_TOPIC_0000002408011637"></a>

抽象类，需要由用户继承该抽象类实现相关接口。当前该类下只有一个接口synchronize\_layer。

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

等待模型指定层执行完成，用户需要继承LayerSynchronizer并实现该接口。

该接口会在执行transfer\_cache\_async时被调用，当该接口返回成功，则开始当前层cache的传输。

## 函数原型<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
synchronize_layer(layer_index: int, timeout_in_millis: Optional[int]) -> bool
```

## 参数说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>layer_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>层的下标</p>
</td>
</tr>
<tr id="row0710153012620"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p971011302618"><a name="p971011302618"></a><a name="p971011302618"></a>timeout_in_millis</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p271013306616"><a name="p271013306616"></a><a name="p271013306616"></a>Optional[int]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p13710163018615"><a name="p13710163018615"></a><a name="p13710163018615"></a>超时时间，暂不支持。</p>
</td>
</tr>
</tbody>
</table>

## 调用示例<a name="section17821439839"></a>

该接口不由用户直接调用，而是作为回调由CacheManager调用。

## 返回值<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下返回是否同步成功。

参数错误可能抛出TypeError或ValueError。

## 约束说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

无

