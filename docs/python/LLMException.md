# LLMException<a name="ZH-CN_TOPIC_0000002374252160"></a>

调用LLMDataDist各接口，异常场景可能抛出LLMException异常。当前该类下只有一个接口status\_code。

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

获取异常的错误码。错误码列表详见LLMStatusCode。

## 函数原型<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
status_code()
```

## 参数说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

无

## 调用示例<a name="section17821439839"></a>

```
from llm_datadist import *
...
cache_keys = [CacheKey(1, req_id=1), CacheKey(1, req_id=2)]
try:
    kv_cache_manager.pull_cache(cache_keys[0], cache, 0)
except LLMException as exe:
    print(exe.status_code)
```

## 返回值<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

返回错误码。

## 约束说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

无

