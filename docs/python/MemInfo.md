# MemInfo<a name="ZH-CN_TOPIC_0000002408011581"></a>

## 函数功能<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

构造MemInfo，通常在CacheManager的remap\_registered\_memory接口中作为参数类型使用。

## 函数原型<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
__init__(mem_type: Memtype, addr: int, size: int)
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
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>mem_type</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>MemType</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>内存地址类型。</p>
</td>
</tr>
<tr id="row99821205619"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1599201212562"><a name="p1599201212562"></a><a name="p1599201212562"></a>addr</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p149931218561"><a name="p149931218561"></a><a name="p149931218561"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p09912124563"><a name="p09912124563"></a><a name="p09912124563"></a>内存地址。</p>
</td>
</tr>
<tr id="row426916171070"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p226918171874"><a name="p226918171874"></a><a name="p226918171874"></a>size</p>
</td>
<td class="cellrowborder" valign="top" width="35.89%" headers="mcps1.1.4.1.2 "><p id="p182011521711"><a name="p182011521711"></a><a name="p182011521711"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.89%" headers="mcps1.1.4.1.3 "><p id="p12699171717"><a name="p12699171717"></a><a name="p12699171717"></a>内存地址对应大小，单位字节。</p>
</td>
</tr>
</tbody>
</table>

## 调用示例<a name="section17821439839"></a>

```
from llm_datadist import MemInfo
mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, 1234, 10)
```

## 返回值<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下返回MemInfo的实例。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

## 约束说明<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

无

