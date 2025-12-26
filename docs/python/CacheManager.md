# CacheManager<a name="ZH-CN_TOPIC_0000002374252152"></a>
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



## CacheManager构造函数<a name="ZH-CN_TOPIC_0000002407891501"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

CacheManager的实例应该通过cache\_manager返回。
## pull\_cache<a name="ZH-CN_TOPIC_0000002374411972"></a>
**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

根据CacheKey，从对应的对端节点拉取到本地Cache。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
pull_cache(cache_key: Union[CacheKey, CacheKeyByIdAndIndex],
           cache: Cache,
           batch_index: int = 0,
           size: int = -1, **kwargs)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>cache_key</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>Union[CacheKey</a>,CacheKeyByIdAndIndex</a>]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p1725416211412"><a name="p1725416211412"></a><a name="p1725416211412"></a>需要被拉取的CacheKey。</p>
<p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>通过req_id，prefix_id，model_id拉取则传入CacheKey。</p>
<p id="p1855835919407"><a name="p1855835919407"></a><a name="p1855835919407"></a>通过cache_id，batch_index拉取则传入CacheKeyByIdAndIndex。</p>
</td>
</tr>
<tr id="row177951620637"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p137951720435"><a name="p137951720435"></a><a name="p137951720435"></a>cache</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p679552016316"><a name="p679552016316"></a><a name="p679552016316"></a>Cache</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p27957206311"><a name="p27957206311"></a><a name="p27957206311"></a>目标Cache。</p>
</td>
</tr>
<tr id="row235619231134"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143563231931"><a name="p143563231931"></a><a name="p143563231931"></a>batch_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p15356102314311"><a name="p15356102314311"></a><a name="p15356102314311"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p133562023030"><a name="p133562023030"></a><a name="p133562023030"></a>batch index，默认为0。</p>
</td>
</tr>
<tr id="row7699636630"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p166997367315"><a name="p166997367315"></a><a name="p166997367315"></a>size</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p96991036232"><a name="p96991036232"></a><a name="p96991036232"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p37063585311"><a name="p37063585311"></a><a name="p37063585311"></a>设置为&gt;0的整数，表示要拉取的tensor大小。</p>
<p id="p136582910548"><a name="p136582910548"></a><a name="p136582910548"></a>或设置为-1，表示完整拷贝：本地单个KV的大小。</p>
<p id="p7374185016429"><a name="p7374185016429"></a><a name="p7374185016429"></a>默认为-1。</p>
</td>
</tr>
<tr id="row1854190202411"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p7134101717582"><a name="p7134101717582"></a><a name="p7134101717582"></a>**kwargs</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p16134717155810"><a name="p16134717155810"></a><a name="p16134717155810"></a>NA</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p20795204113911"><a name="p20795204113911"></a><a name="p20795204113911"></a>这个是Python函数的可扩展参数通用写法，一般通过key=value的方式直接传入参数。</p>
<p id="p19795843395"><a name="p19795843395"></a><a name="p19795843395"></a>可选参数的详细信息请参考<a href="#table37855712016">表1</a>。</p>
</td>
</tr>
</tbody>
</table>

**表 1**  \*\*kwargs的可选参数

<a name="table37855712016"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001893731694_row16785471104"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.2.4.1.1"><p id="zh-cn_topic_0000001893731694_p47851972000"><a name="zh-cn_topic_0000001893731694_p47851972000"></a><a name="zh-cn_topic_0000001893731694_p47851972000"></a><strong id="zh-cn_topic_0000001893731694_b12785571609"><a name="zh-cn_topic_0000001893731694_b12785571609"></a><a name="zh-cn_topic_0000001893731694_b12785571609"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.2.4.1.2"><p id="zh-cn_topic_0000001893731694_p147851671508"><a name="zh-cn_topic_0000001893731694_p147851671508"></a><a name="zh-cn_topic_0000001893731694_p147851671508"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.2.4.1.3"><p id="zh-cn_topic_0000001893731694_p137859712019"><a name="zh-cn_topic_0000001893731694_p137859712019"></a><a name="zh-cn_topic_0000001893731694_p137859712019"></a><strong id="zh-cn_topic_0000001893731694_b20785137501"><a name="zh-cn_topic_0000001893731694_b20785137501"></a><a name="zh-cn_topic_0000001893731694_b20785137501"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001893731694_row1854190202411"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.2.4.1.1 "><p id="zh-cn_topic_0000001893731694_p1254219052410"><a name="zh-cn_topic_0000001893731694_p1254219052410"></a><a name="zh-cn_topic_0000001893731694_p1254219052410"></a>src_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.2.4.1.2 "><p id="zh-cn_topic_0000001893731694_p654212019249"><a name="zh-cn_topic_0000001893731694_p654212019249"></a><a name="zh-cn_topic_0000001893731694_p654212019249"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.2.4.1.3 "><p id="zh-cn_topic_0000001893731694_p05421012411"><a name="zh-cn_topic_0000001893731694_p05421012411"></a><a name="zh-cn_topic_0000001893731694_p05421012411"></a>可选参数，用于按层pull kv场景。传输源的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续<span id="ph1391373715362"><a name="ph1391373715362"></a><a name="ph1391373715362"></a>N</span>个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。<span id="ph0588012123212"><a name="ph0588012123212"></a><a name="ph0588012123212"></a>N为</span><span id="ph16257111853215"><a name="ph16257111853215"></a><a name="ph16257111853215"></a>tensor_num_per_layer</span><span id="ph7205151910326"><a name="ph7205151910326"></a><a name="ph7205151910326"></a>的取值，默认为2。</span></p>
</td>
</tr>
<tr id="zh-cn_topic_0000001893731694_row8974238247"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.2.4.1.1 "><p id="zh-cn_topic_0000001893731694_p497533142411"><a name="zh-cn_topic_0000001893731694_p497533142411"></a><a name="zh-cn_topic_0000001893731694_p497533142411"></a>dst_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.2.4.1.2 "><p id="zh-cn_topic_0000001893731694_p397511332418"><a name="zh-cn_topic_0000001893731694_p397511332418"></a><a name="zh-cn_topic_0000001893731694_p397511332418"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.2.4.1.3 "><p id="zh-cn_topic_0000001893731694_p199753318246"><a name="zh-cn_topic_0000001893731694_p199753318246"></a><a name="zh-cn_topic_0000001893731694_p199753318246"></a>可选参数，用于按层pull kv场景。传输目标的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续<span id="ph20486744153619"><a name="ph20486744153619"></a><a name="ph20486744153619"></a>N</span>个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。<span id="ph112854468366"><a name="ph112854468366"></a><a name="ph112854468366"></a>N为</span><span id="ph8285846133618"><a name="ph8285846133618"></a><a name="ph8285846133618"></a>tensor_num_per_layer</span><span id="ph19285194615367"><a name="ph19285194615367"></a><a name="ph19285194615367"></a>的取值，默认为2。</span></p>
</td>
</tr>
<tr id="row098112983920"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.2.4.1.1 "><p id="p1709929141119"><a name="p1709929141119"></a><a name="p1709929141119"></a>tensor_num_per_layer</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.2.4.1.2 "><p id="p6947331141115"><a name="p6947331141115"></a><a name="p6947331141115"></a>Optional[int]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.2.4.1.3 "><p id="p1017074311111"><a name="p1017074311111"></a><a name="p1017074311111"></a>可选参数，表示每层的tensor的数量，默认值是2，取值范围是[1,cache的tensor总数]。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

请参考[样例运行](../../examples/python/README.md)。

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

执行时间超过sync\_kv\_timeout配置会抛出LLMException异常。

layer\_range参数异常会抛出LLMException异常。

**约束说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

-   开启enable\_remote\_cache\_accessible时，只支持cache\_key类型为CacheKeyByIdAndIndex。
-   在D2H和H2D传输场景，需要在Host端初始化时配置Device内存池, 且内存池大小至少配置为100M。
## pull\_blocks<a name="ZH-CN_TOPIC_0000002407891505"></a>
**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

PagedAttention场景下，根据BlocksCacheKey，通过block列表的方式从对端节点拉取Cache到本地Cache。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
pull_blocks(src_cache_key: Union[CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey],
            dst_cache: Cache,
            src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
            dst_blocks: Union[Tuple[int], List[int]] = (), **kwargs)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>src_cache_key</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>Union[CacheKey</a>,CacheKeyByIdAndIndex</a>, BlocksCacheKey</a>]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>远端的Cache索引。</p>
</td>
</tr>
<tr id="row177951620637"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p137951720435"><a name="p137951720435"></a><a name="p137951720435"></a>dst_cache</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p679552016316"><a name="p679552016316"></a><a name="p679552016316"></a>Cache</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p27957206311"><a name="p27957206311"></a><a name="p27957206311"></a>目标Cache。</p>
</td>
</tr>
<tr id="row235619231134"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143563231931"><a name="p143563231931"></a><a name="p143563231931"></a>src_blocks</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p445821220290"><a name="p445821220290"></a><a name="p445821220290"></a>Optional[Union[Tuple[int], List[int]]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p133562023030"><a name="p133562023030"></a><a name="p133562023030"></a>远端的block index列表，src_cache_key不是BlocksCacheKey时，不填。</p>
</td>
</tr>
<tr id="row7699636630"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p166997367315"><a name="p166997367315"></a><a name="p166997367315"></a>dst_blocks</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p246221292918"><a name="p246221292918"></a><a name="p246221292918"></a>Union[Tuple[int], List[int]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p7374185016429"><a name="p7374185016429"></a><a name="p7374185016429"></a>本地的block index列表。</p>
</td>
</tr>
<tr id="row491210328310"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p7134101717582"><a name="p7134101717582"></a><a name="p7134101717582"></a>**kwargs</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p16134717155810"><a name="p16134717155810"></a><a name="p16134717155810"></a>NA</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p526383015394"><a name="p526383015394"></a><a name="p526383015394"></a>这个是Python函数的可扩展参数通用写法，一般通过key=value的方式直接传入参数。</p>
<p id="p326363014395"><a name="p326363014395"></a><a name="p326363014395"></a>可选参数的详细信息请参考<a href="#table37855712016">表1</a>。</p>
</td>
</tr>
</tbody>
</table>

**表 1**  \*\*kwargs的可选参数

<a name="table37855712016"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001893731694_row16785471104"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.2.4.1.1"><p id="zh-cn_topic_0000001893731694_p47851972000"><a name="zh-cn_topic_0000001893731694_p47851972000"></a><a name="zh-cn_topic_0000001893731694_p47851972000"></a><strong id="zh-cn_topic_0000001893731694_b12785571609"><a name="zh-cn_topic_0000001893731694_b12785571609"></a><a name="zh-cn_topic_0000001893731694_b12785571609"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.2.4.1.2"><p id="zh-cn_topic_0000001893731694_p147851671508"><a name="zh-cn_topic_0000001893731694_p147851671508"></a><a name="zh-cn_topic_0000001893731694_p147851671508"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.2.4.1.3"><p id="zh-cn_topic_0000001893731694_p137859712019"><a name="zh-cn_topic_0000001893731694_p137859712019"></a><a name="zh-cn_topic_0000001893731694_p137859712019"></a><strong id="zh-cn_topic_0000001893731694_b20785137501"><a name="zh-cn_topic_0000001893731694_b20785137501"></a><a name="zh-cn_topic_0000001893731694_b20785137501"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001893731694_row1854190202411"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.2.4.1.1 "><p id="zh-cn_topic_0000001893731694_p1254219052410"><a name="zh-cn_topic_0000001893731694_p1254219052410"></a><a name="zh-cn_topic_0000001893731694_p1254219052410"></a>src_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.2.4.1.2 "><p id="zh-cn_topic_0000001893731694_p654212019249"><a name="zh-cn_topic_0000001893731694_p654212019249"></a><a name="zh-cn_topic_0000001893731694_p654212019249"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.2.4.1.3 "><p id="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p05421012411"><a name="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p05421012411"></a><a name="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p05421012411"></a>可选参数，用于按层pull kv场景。传输源的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续<span id="zh-cn_topic_0000002374411972_ph1391373715362"><a name="zh-cn_topic_0000002374411972_ph1391373715362"></a><a name="zh-cn_topic_0000002374411972_ph1391373715362"></a>N</span>个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。<span id="zh-cn_topic_0000002374411972_ph0588012123212"><a name="zh-cn_topic_0000002374411972_ph0588012123212"></a><a name="zh-cn_topic_0000002374411972_ph0588012123212"></a>N为</span><span id="zh-cn_topic_0000002374411972_ph16257111853215"><a name="zh-cn_topic_0000002374411972_ph16257111853215"></a><a name="zh-cn_topic_0000002374411972_ph16257111853215"></a>tensor_num_per_layer</span><span id="zh-cn_topic_0000002374411972_ph7205151910326"><a name="zh-cn_topic_0000002374411972_ph7205151910326"></a><a name="zh-cn_topic_0000002374411972_ph7205151910326"></a>的取值，默认为2。</span></p>
</td>
</tr>
<tr id="zh-cn_topic_0000001893731694_row8974238247"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.2.4.1.1 "><p id="zh-cn_topic_0000001893731694_p497533142411"><a name="zh-cn_topic_0000001893731694_p497533142411"></a><a name="zh-cn_topic_0000001893731694_p497533142411"></a>dst_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.2.4.1.2 "><p id="zh-cn_topic_0000001893731694_p397511332418"><a name="zh-cn_topic_0000001893731694_p397511332418"></a><a name="zh-cn_topic_0000001893731694_p397511332418"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.2.4.1.3 "><p id="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p199753318246"><a name="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p199753318246"></a><a name="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p199753318246"></a>可选参数，用于按层pull kv场景。传输目标的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续<span id="zh-cn_topic_0000002374411972_ph20486744153619"><a name="zh-cn_topic_0000002374411972_ph20486744153619"></a><a name="zh-cn_topic_0000002374411972_ph20486744153619"></a>N</span>个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。<span id="zh-cn_topic_0000002374411972_ph112854468366"><a name="zh-cn_topic_0000002374411972_ph112854468366"></a><a name="zh-cn_topic_0000002374411972_ph112854468366"></a>N为</span><span id="zh-cn_topic_0000002374411972_ph8285846133618"><a name="zh-cn_topic_0000002374411972_ph8285846133618"></a><a name="zh-cn_topic_0000002374411972_ph8285846133618"></a>tensor_num_per_layer</span><span id="zh-cn_topic_0000002374411972_ph19285194615367"><a name="zh-cn_topic_0000002374411972_ph19285194615367"></a><a name="zh-cn_topic_0000002374411972_ph19285194615367"></a>的取值，默认为2。</span></p>
</td>
</tr>
<tr id="row19316535133915"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.2.4.1.1 "><p id="p1709929141119"><a name="p1709929141119"></a><a name="p1709929141119"></a>tensor_num_per_layer</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.2.4.1.2 "><p id="p6947331141115"><a name="p6947331141115"></a><a name="p6947331141115"></a>Optional[int]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.2.4.1.3 "><p id="zh-cn_topic_0000002374411972_p1017074311111"><a name="zh-cn_topic_0000002374411972_p1017074311111"></a><a name="zh-cn_topic_0000002374411972_p1017074311111"></a>可选参数，表示每层的tensor的数量，默认值是2，取值范围是[1,cache的tensor总数]。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

请参考[样例运行](../../examples/python/README.md)。

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

-   正常情况下无返回值。
-   传入数据类型错误情况下会抛出TypeError或ValueError异常。
-   执行时间超过sync\_kv\_timeout配置会抛出LLMException异常。
-   layer\_range参数异常会抛出LLMException异常。

**约束说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

-   当src\_cache是HOST设备时，dst\_cache是DEVICE设备时，仅支持src\_cache与dst\_cache都为blocks cache的场景。
-   开启enable\_remote\_cache\_accessible时会引入额外约束，详见该配置的约束说明章节。
-   在D2H和H2D传输场景，需要在Host端初始化时配置Device内存池，且内存池大小至少配置为100M。
## register\_cache<a name="ZH-CN_TOPIC_0000002408011649"></a>
**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

非PagedAttention场景下，调用此接口注册一个自行申请的内存。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
register_cache(cache_desc: CacheDesc, addrs: List[int], cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = (), remote_accessible: Optional[bool] = None) -> Cache:
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>cache_desc</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>CacheDesc</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>Cache的描述信息。</p>
</td>
</tr>
<tr id="row0710153012620"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p971011302618"><a name="p971011302618"></a><a name="p971011302618"></a>addrs</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p271013306616"><a name="p271013306616"></a><a name="p271013306616"></a>List[int]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p13710163018615"><a name="p13710163018615"></a><a name="p13710163018615"></a>Cache的地址。register_cache</a>中的地址个数与register_blocks_cache</a>的地址个数之和不超过240。</p>
</td>
</tr>
<tr id="row063443012566"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p363453025615"><a name="p363453025615"></a><a name="p363453025615"></a>cache_keys</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p063416304569"><a name="p063416304569"></a><a name="p063416304569"></a>Union[Tuple[CacheKey</a>], List[CacheKey</a>]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p166341304565"><a name="p166341304565"></a><a name="p166341304565"></a>Cache的索引。</p>
</td>
</tr>
<tr id="row196372034115616"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p10637193495613"><a name="p10637193495613"></a><a name="p10637193495613"></a>remote_accessible</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p763713412560"><a name="p763713412560"></a><a name="p763713412560"></a>Optional[bool]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p66379348564"><a name="p66379348564"></a><a name="p66379348564"></a>指定当前注册内存是否能用来做网络传输，对于Device内存，默认值是True，对于Host内存，默认值是False。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

请参考[样例运行](../../examples/python/README.md)。

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下返回注册的cache。

传入数据类型错误，src和dst不匹配情况下会抛出TypeError或ValueError异常。

传入参数为None，会抛出AttributeError异常。

**约束说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

- Device内存需要先注册再进行建链，Host内存不约束顺序。

- 注册内存地址需自行保证不重复。

- 最大注册50GB的Device内存，20GB的Host内存。注册内存越大，占用的OS内存越多。

- D2D HCCS数据传输时，首地址需要按照2MB对齐，否则可能导致link失败，HCCS不支持Host内存。

- RDMA数据传输时，申请Host内存必须通过aclrtMallocHost接口，否则可能导致link失败。
## register\_blocks\_cache<a name="ZH-CN_TOPIC_0000002374411992"></a>
**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

PagedAttention场景下，调用此接口注册一个自行申请的内存。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
register_blocks_cache(cache_desc: CacheDesc, addrs: List[int], blocks_cache_key: Optional[BlocksCacheKey] = None, remote_accessible: Optional[bool] = None) -> Cache:
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>cache_desc</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>CacheDesc</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>Cache的描述信息。</p>
</td>
</tr>
<tr id="row0710153012620"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p971011302618"><a name="p971011302618"></a><a name="p971011302618"></a>addrs</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p271013306616"><a name="p271013306616"></a><a name="p271013306616"></a>List[int]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p13710163018615"><a name="p13710163018615"></a><a name="p13710163018615"></a>Cache的地址。register_cache</a>中的地址个数与register_blocks_cache</a>的地址个数之和不超过240。</p>
</td>
</tr>
<tr id="row17917313164217"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p991731394217"><a name="p991731394217"></a><a name="p991731394217"></a>blocks_cache_key</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p891781319428"><a name="p891781319428"></a><a name="p891781319428"></a>Optional[BlocksCacheKey</a>]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p209171913164214"><a name="p209171913164214"></a><a name="p209171913164214"></a>可选的BlocksCacheKey索引。</p>
</td>
</tr>
<tr id="row2918813584"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p10637193495613"><a name="p10637193495613"></a><a name="p10637193495613"></a>remote_accessible</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p763713412560"><a name="p763713412560"></a><a name="p763713412560"></a>Optional[bool]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p66379348564"><a name="p66379348564"></a><a name="p66379348564"></a>指定当前注册内存是否能用来做网络传输，对于Device内存，默认值是True，对于Host内存，默认值是False。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

请参考[样例运行](../../examples/python/README.md)。

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下返回注册的cache。

传入数据类型错误，不匹配情况下会抛出TypeError或ValueError异常。

传入参数为None，会抛出AttributeError异常。

**约束说明**<a name="section5424822194110"></a>

如果通过HCCS进行数据传输，且内存不是通过aclrtMalloc申请，则地址需要按照页大小对齐（如果确定页大小是多少，推荐2MB对齐），否则可能导致link失败。
## transfer\_cache\_async<a name="ZH-CN_TOPIC_0000002374252092"></a>
**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

异步分层传输Cache。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
transfer_cache_async(self,
                     src_cache: Cache,
                     layer_synchronizer: LayerSynchronizer,
                     transfer_configs: Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]], Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]],
                     src_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                     dst_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                     dst_block_memory_size: Optional[int] = None) -> CacheTask
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="table32741116183317"></a>
<table><thead align="left"><tr id="row11274116133311"><th class="cellrowborder" valign="top" width="22.06%" id="mcps1.1.4.1.1"><p id="p12745161339"><a name="p12745161339"></a><a name="p12745161339"></a><strong id="b18274161615332"><a name="b18274161615332"></a><a name="b18274161615332"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="36.03%" id="mcps1.1.4.1.2"><p id="p152748168338"><a name="p152748168338"></a><a name="p152748168338"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="p2274161633318"><a name="p2274161633318"></a><a name="p2274161633318"></a><strong id="b427461613316"><a name="b427461613316"></a><a name="b427461613316"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="row4274216163318"><td class="cellrowborder" valign="top" width="22.06%" headers="mcps1.1.4.1.1 "><p id="p1027461611333"><a name="p1027461611333"></a><a name="p1027461611333"></a>src_cache</p>
</td>
<td class="cellrowborder" valign="top" width="36.03%" headers="mcps1.1.4.1.2 "><p id="p22749164337"><a name="p22749164337"></a><a name="p22749164337"></a>Cache</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p1727561620339"><a name="p1727561620339"></a><a name="p1727561620339"></a>源Cache。</p>
</td>
</tr>
<tr id="row8275111603311"><td class="cellrowborder" valign="top" width="22.06%" headers="mcps1.1.4.1.1 "><p id="p927512165338"><a name="p927512165338"></a><a name="p927512165338"></a>layer_synchronizer</p>
</td>
<td class="cellrowborder" valign="top" width="36.03%" headers="mcps1.1.4.1.2 "><p id="p20459131014913"><a name="p20459131014913"></a><a name="p20459131014913"></a>LayerSynchronizer</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p1227515164336"><a name="p1227515164336"></a><a name="p1227515164336"></a>LayerSynchronizer的实现类对象。</p>
</td>
</tr>
<tr id="row182114478816"><td class="cellrowborder" valign="top" width="22.06%" headers="mcps1.1.4.1.1 "><p id="p12118477817"><a name="p12118477817"></a><a name="p12118477817"></a>transfer_configs</p>
</td>
<td class="cellrowborder" valign="top" width="36.03%" headers="mcps1.1.4.1.2 "><p id="p11486101117158"><a name="p11486101117158"></a><a name="p11486101117158"></a>Union[List[Union[TransferConfig</a>, TransferWithCacheKeyConfig</a>]], Tuple[Union[TransferConfig</a>, TransferWithCacheKeyConfig</a>]]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p52111847887"><a name="p52111847887"></a><a name="p52111847887"></a>传输配置列表或元组。</p>
</td>
</tr>
<tr id="row112115451084"><td class="cellrowborder" valign="top" width="22.06%" headers="mcps1.1.4.1.1 "><p id="p5211345688"><a name="p5211345688"></a><a name="p5211345688"></a>src_block_indices</p>
</td>
<td class="cellrowborder" valign="top" width="36.03%" headers="mcps1.1.4.1.2 "><p id="p1821545385"><a name="p1821545385"></a><a name="p1821545385"></a>Optional[Union[List[int], Tuple[int]]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p1922104519815"><a name="p1922104519815"></a><a name="p1922104519815"></a>源Cache的block indices，当源Cache为PA场景时设置。</p>
</td>
</tr>
<tr id="row16899125121211"><td class="cellrowborder" valign="top" width="22.06%" headers="mcps1.1.4.1.1 "><p id="p0899165171217"><a name="p0899165171217"></a><a name="p0899165171217"></a>dst_block_indices</p>
</td>
<td class="cellrowborder" valign="top" width="36.03%" headers="mcps1.1.4.1.2 "><p id="p089965151218"><a name="p089965151218"></a><a name="p089965151218"></a>Optional[Union[List[int], Tuple[int]]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p964048294"><a name="p964048294"></a><a name="p964048294"></a>目的Cache的block indices，当目的Cache为PA场景时设置。</p>
</td>
</tr>
<tr id="row10921638488"><td class="cellrowborder" valign="top" width="22.06%" headers="mcps1.1.4.1.1 "><p id="p49211938982"><a name="p49211938982"></a><a name="p49211938982"></a>dst_block_memory_size</p>
</td>
<td class="cellrowborder" valign="top" width="36.03%" headers="mcps1.1.4.1.2 "><p id="p69217381280"><a name="p69217381280"></a><a name="p69217381280"></a>Optional[int]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p159211638988"><a name="p159211638988"></a><a name="p159211638988"></a>目的Cache每个block占用的内存大小，当目的Cache为PA场景时设置。如果源Cache也为PA场景，则可省略该参数，此时会自动将其设置为源Cache每个block占用的内存大小。</p>
<p id="p171940155453"><a name="p171940155453"></a><a name="p171940155453"></a>该参数设置为0时等同于省略该参数。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
cache_task = cache_manager.transfer_cache_async(cache, LayerSynchronizerImpl(), transfer_configs)
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下返回CacheTask。

传入数据类型错误，会抛出TypeError或ValueError异常。

传入数据非法，会抛出LLMException异常。

**约束说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

-   不支持src\_cache是HOST，dst\_cache是DEVICE的传输场景。
-   不支持src\_cache是PA场景，dst\_cache是非PA场景。
-   不支持单进程多卡场景。
-   若dst\_cache是HOST，仅支持dst\_cache通过allocate\_cache申请方式申请。
-   需要保证transfer\_config中的dst\_addrs的有效性，以及在传输dst\_block\_indices场景下数据的有效性，否则错误未知。
-   开启enable\_remote\_cache\_accessible时，transfer\_configs中的类型需为TransferWithCacheKeyConfig，不开启enable\_remote\_cache\_accessible时，transfer\_configs中的类型需为TransferConfig。
## push\_blocks<a name="ZH-CN_TOPIC_0000002374252056"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

PagedAttention场景下，根据BlocksCacheKey，通过block列表的方式从本地节点推送Cache到远端Cache。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
push_blocks(self,
                    dst_cache_key: BlocksCacheKey,
                    src_cache: Cache,
                    src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
                    dst_blocks: Union[Tuple[int], List[int]] = (),
                    src_layer_range: range = None,
                    dst_layer_range: range = None,
                    tensor_num_per_layer = _NUM_TENSORS_PER_LAYER)
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>dst_cache_key</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>BlocksCacheKey</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p7172700591"><a name="p7172700591"></a><a name="p7172700591"></a>远端的Cache索引。</p>
</td>
</tr>
<tr id="row177951620637"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p137951720435"><a name="p137951720435"></a><a name="p137951720435"></a>src_cache</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p679552016316"><a name="p679552016316"></a><a name="p679552016316"></a>Cache</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p27957206311"><a name="p27957206311"></a><a name="p27957206311"></a>本地Cache。</p>
</td>
</tr>
<tr id="row235619231134"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143563231931"><a name="p143563231931"></a><a name="p143563231931"></a>src_blocks</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p445821220290"><a name="p445821220290"></a><a name="p445821220290"></a>Optional[Union[Tuple[int], List[int]]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p133562023030"><a name="p133562023030"></a><a name="p133562023030"></a>本地的block index列表。</p>
</td>
</tr>
<tr id="row7699636630"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p166997367315"><a name="p166997367315"></a><a name="p166997367315"></a>dst_blocks</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p246221292918"><a name="p246221292918"></a><a name="p246221292918"></a>Union[Tuple[int], List[int]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p7374185016429"><a name="p7374185016429"></a><a name="p7374185016429"></a>远端的block index列表。</p>
</td>
</tr>
<tr id="row17596329124712"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p115907295473"><a name="p115907295473"></a><a name="p115907295473"></a>src_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p165901129104718"><a name="p165901129104718"></a><a name="p165901129104718"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p436165204910"><a name="p436165204910"></a><a name="p436165204910"></a>可选参数，用于按层pull kv场景。默认值为None。</p>
<p id="p060072514911"><a name="p060072514911"></a><a name="p060072514911"></a>传输源的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续<span id="ph1600122534910"><a name="ph1600122534910"></a><a name="ph1600122534910"></a>N</span>个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。<span id="ph36001425164913"><a name="ph36001425164913"></a><a name="ph36001425164913"></a>N为</span><span id="ph19600162516492"><a name="ph19600162516492"></a><a name="ph19600162516492"></a>tensor_num_per_layer</span><span id="ph1360032574910"><a name="ph1360032574910"></a><a name="ph1360032574910"></a>的取值，默认为2。</span></p>
</td>
</tr>
<tr id="row359672912473"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p1859082924718"><a name="p1859082924718"></a><a name="p1859082924718"></a>dst_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p10590122904711"><a name="p10590122904711"></a><a name="p10590122904711"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p167671614505"><a name="p167671614505"></a><a name="p167671614505"></a>可选参数，用于按层pull kv场景。默认值为None。</p>
<p id="p1817571411508"><a name="p1817571411508"></a><a name="p1817571411508"></a>传输目标的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续<span id="ph217571435015"><a name="ph217571435015"></a><a name="ph217571435015"></a>N</span>个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。<span id="ph1117521425016"><a name="ph1117521425016"></a><a name="ph1117521425016"></a>N为</span><span id="ph2017518144505"><a name="ph2017518144505"></a><a name="ph2017518144505"></a>tensor_num_per_layer</span><span id="ph1517513145502"><a name="ph1517513145502"></a><a name="ph1517513145502"></a>的取值，默认为2。</span></p>
</td>
</tr>
<tr id="row159512910477"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p8590142964712"><a name="p8590142964712"></a><a name="p8590142964712"></a>tensor_num_per_layer</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p2059032910476"><a name="p2059032910476"></a><a name="p2059032910476"></a>Optional[int]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000002374411972_p1017074311111"><a name="zh-cn_topic_0000002374411972_p1017074311111"></a><a name="zh-cn_topic_0000002374411972_p1017074311111"></a>可选参数，表示每层的tensor的数量，默认值是2，取值范围是[1,cache的tensor总数]。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。</p>
</td>
</tr>
</tbody>
</table>


**调用示例**<a name="section17821439839"></a>

请参考[样例运行](../../examples/python/README.md)。

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

-   正常情况下无返回值。
-   传入数据类型错误情况下会抛出TypeError或ValueError异常。
-   执行时间超过sync\_kv\_timeout配置会抛出LLMException异常。
-   layer\_range参数异常会抛出LLMException异常。


**约束说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

-   当src\_cache是HOST设备时，dst\_cache是DEVICE设备时，仅支持src\_cache与dst\_cache都为PA的场景。
-   开启enable\_remote\_cache\_accessible时会引入额外约束，详见该配置的约束说明章节。
-   在D2H和H2D传输场景，需要在Host端初始化时配置Device内存池。
-   当前仅支持src\_layer\_range和dst\_layer\_range取值一致。
## push\_cache<a name="ZH-CN_TOPIC_0000002374252060"></a>
**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

根据CacheKey，从本地节点推送Cache到远端Cache。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
push_cache(self,
                   dst_cache_key: CacheKeyByIdAndIndex,
                   src_cache: Cache,
                   src_batch_index: int = 0,
                   src_layer_range: range = None,
                   dst_layer_range: range = None,
                   size: int = -1,
                   tensor_num_per_layer = _NUM_TENSORS_PER_LAYER) -> None
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>dst_cache_key</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>CacheKeyByIdAndIndex</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p1725416211412"><a name="p1725416211412"></a><a name="p1725416211412"></a>远端的Cache索引</p>
</td>
</tr>
<tr id="row177951620637"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p137951720435"><a name="p137951720435"></a><a name="p137951720435"></a>src_cache</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p679552016316"><a name="p679552016316"></a><a name="p679552016316"></a>Cache</a></p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p27957206311"><a name="p27957206311"></a><a name="p27957206311"></a>本地的cache</p>
</td>
</tr>
<tr id="row235619231134"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p143563231931"><a name="p143563231931"></a><a name="p143563231931"></a>src_batch_index</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p15356102314311"><a name="p15356102314311"></a><a name="p15356102314311"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p133562023030"><a name="p133562023030"></a><a name="p133562023030"></a>本地的batch index，默认为0。</p>
</td>
</tr>
<tr id="row166103612549"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p166163625414"><a name="p166163625414"></a><a name="p166163625414"></a>src_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p1561193605418"><a name="p1561193605418"></a><a name="p1561193605418"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p26878251900"><a name="p26878251900"></a><a name="p26878251900"></a>可选参数，用于按层pull kv场景。默认值为None。</p>
<p id="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p05421012411"><a name="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p05421012411"></a><a name="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p05421012411"></a>传输源的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续<span id="zh-cn_topic_0000002374411972_ph1391373715362"><a name="zh-cn_topic_0000002374411972_ph1391373715362"></a><a name="zh-cn_topic_0000002374411972_ph1391373715362"></a>N</span>个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。<span id="zh-cn_topic_0000002374411972_ph0588012123212"><a name="zh-cn_topic_0000002374411972_ph0588012123212"></a><a name="zh-cn_topic_0000002374411972_ph0588012123212"></a>N为</span><span id="zh-cn_topic_0000002374411972_ph16257111853215"><a name="zh-cn_topic_0000002374411972_ph16257111853215"></a><a name="zh-cn_topic_0000002374411972_ph16257111853215"></a>tensor_num_per_layer</span><span id="zh-cn_topic_0000002374411972_ph7205151910326"><a name="zh-cn_topic_0000002374411972_ph7205151910326"></a><a name="zh-cn_topic_0000002374411972_ph7205151910326"></a>的取值，默认为2。</span></p>
</td>
</tr>
<tr id="row186663665414"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p26173695415"><a name="p26173695415"></a><a name="p26173695415"></a>dst_layer_range</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p16611036175412"><a name="p16611036175412"></a><a name="p16611036175412"></a>Optional[range]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p3231103818013"><a name="p3231103818013"></a><a name="p3231103818013"></a>可选参数，用于按层pull kv场景。默认值为None。</p>
<p id="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p199753318246"><a name="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p199753318246"></a><a name="zh-cn_topic_0000002374411972_zh-cn_topic_0000001893731694_p199753318246"></a>传输目标的layer的范围，step只支持1。不设置时为传输所有layer。需要注意这里是layer的index，而不是tensor的index，即1个layer对应连续<span id="zh-cn_topic_0000002374411972_ph20486744153619"><a name="zh-cn_topic_0000002374411972_ph20486744153619"></a><a name="zh-cn_topic_0000002374411972_ph20486744153619"></a>N</span>个tensor(K/V)，这里要求分配内存时，必须是KV,...,KV排布，不支持其他场景。<span id="zh-cn_topic_0000002374411972_ph112854468366"><a name="zh-cn_topic_0000002374411972_ph112854468366"></a><a name="zh-cn_topic_0000002374411972_ph112854468366"></a>N为</span><span id="zh-cn_topic_0000002374411972_ph8285846133618"><a name="zh-cn_topic_0000002374411972_ph8285846133618"></a><a name="zh-cn_topic_0000002374411972_ph8285846133618"></a>tensor_num_per_layer</span><span id="zh-cn_topic_0000002374411972_ph19285194615367"><a name="zh-cn_topic_0000002374411972_ph19285194615367"></a><a name="zh-cn_topic_0000002374411972_ph19285194615367"></a>的取值，默认为2。</span></p>
</td>
</tr>
<tr id="row265133655420"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p56153665418"><a name="p56153665418"></a><a name="p56153665418"></a>tensor_num_per_layer</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p1461133613542"><a name="p1461133613542"></a><a name="p1461133613542"></a>Optional[int]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="zh-cn_topic_0000002374411972_p1017074311111"><a name="zh-cn_topic_0000002374411972_p1017074311111"></a><a name="zh-cn_topic_0000002374411972_p1017074311111"></a>可选参数，表示每层的tensor的数量，默认值是2，取值范围是[1,cache的tensor总数]。当src_layer_range或dst_layer_range取值为非默认值时， tensor_num_per_layer可以保持默认值，也可以输入其他值，输入其他值的时，tensor_num_per_layer的取值还需要被当前cache的tensor总数整除。</p>
</td>
</tr>
<tr id="row125941840165419"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p178316581325"><a name="p178316581325"></a><a name="p178316581325"></a>size</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p98311058143219"><a name="p98311058143219"></a><a name="p98311058143219"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p18311658103213"><a name="p18311658103213"></a><a name="p18311658103213"></a>一个tensor传输的大小，默认值-1表示本地单个KV的大小，暂不支持其他设置。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

请参考[样例运行](../../examples/python/README.md)。

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

执行时间超过sync\_kv\_timeout配置会抛出LLMException异常。

layer\_range参数异常会抛出LLMException异常。

**约束说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

-   当src\_cache是HOST设备时，dst\_cache是DEVICE设备时，仅支持src\_cache与dst\_cache都为连续cache的场景。
-   开启enable\_remote\_cache\_accessible时，只支持cache\_key类型为CacheKeyByIdAndIndex。
-   在D2H和H2D传输场景，需要在Host端初始化时配置Device内存池。
-   当前仅支持src\_layer\_range和dst\_layer\_range取值一致。
## remap\_registered\_memory<a name="ZH-CN_TOPIC_0000002407891477"></a>

**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

大模型推理过程中，如果发生内存UCE故障，即返回错误码ACL\_ERROR\_RT\_DEVICE\_MEM\_ERROR，上层框架需要先判断发生该故障的内存是否为KV Cache内存，如果不是，请参考[PyTorch](https://www.hiascend.com/developer/software/ai-frameworks/pytorch)的torch\_npu.npu.restart\_device接口的说明获取并修复内存UCE的错误虚拟地址。如果是KV Cache内存，还需要再调用该接口修复注册给网卡的KV Cache内存。

>![](public_sys-resources/icon-note.gif) **说明：** 
>本接口为预留接口，暂不支持。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
remap_registered_memory(mem_infos: Union[MemInfo, list[MemInfo]]) -> None
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>mem_infos</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>Union[MemInfo</a>, list[MemInfo</a>]]</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p1725416211412"><a name="p1725416211412"></a><a name="p1725416211412"></a>内存信息，或者内存信息的列表。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

```
from llm_datadist import *
role = LLMRole.PROMPT  # 发生故障的角色
cluster_id = 1  # 发生故障的LLMDataDist id
datadist = LLMDataDist(role, cluster_id)
cache_manager = datadist.cache_manager
addr = 12345678 # 发生故障的地址
size = 1024  # 发生故障的地址大小
mem_info = MemInfo(Memtype.MEM_TYPE_DEVICE, addr, size)
cache_manager.remap_registered_memory(mem_info)
```

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

**约束说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

当前仅支持Device类型的内存修复。
## unregister\_cache<a name="ZH-CN_TOPIC_0000002374252120"></a>
**函数功能**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section3870635"></a>

解除注册一个自行申请的内存。

**函数原型**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section24431028171314"></a>

```
unregister_cache(cache_id: int) -> None
```

**参数说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section34835721"></a>

<a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_table2051894852017"></a>
<table><thead align="left"><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row4558174815206"><th class="cellrowborder" valign="top" width="22.220000000000002%" id="mcps1.1.4.1.1"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p255884814201"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b145581148152018"></a>参数名称</strong></p>
</th>
<th class="cellrowborder" valign="top" width="35.870000000000005%" id="mcps1.1.4.1.2"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p537710614477"></a>数据类型</p>
</th>
<th class="cellrowborder" valign="top" width="41.91%" id="mcps1.1.4.1.3"><p id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_p14558184812200"></a><strong id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a><a name="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_b19165651193118"></a>取值说明</strong></p>
</th>
</tr>
</thead>
<tbody><tr id="zh-cn_topic_0000001417673572_zh-cn_topic_0000001359609816_row35581048202018"><td class="cellrowborder" valign="top" width="22.220000000000002%" headers="mcps1.1.4.1.1 "><p id="p6621349454"><a name="p6621349454"></a><a name="p6621349454"></a>cache_id</p>
</td>
<td class="cellrowborder" valign="top" width="35.870000000000005%" headers="mcps1.1.4.1.2 "><p id="p9541205974512"><a name="p9541205974512"></a><a name="p9541205974512"></a>int</p>
</td>
<td class="cellrowborder" valign="top" width="41.91%" headers="mcps1.1.4.1.3 "><p id="p1725416211412"><a name="p1725416211412"></a><a name="p1725416211412"></a>调用register_cache或register_blocks_cache返回的cache的id。</p>
</td>
</tr>
</tbody>
</table>

**调用示例**<a name="section17821439839"></a>

请参考[样例运行](../../examples/python/README.md)。

**返回值**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section45086037"></a>

正常情况下无返回值。

传入数据类型错误情况下会抛出TypeError或ValueError异常。

**约束说明**<a name="zh-cn_topic_0000001481404214_zh-cn_topic_0000001488949573_zh-cn_topic_0000001357384997_zh-cn_topic_0000001312399929_section28090371"></a>

-   当前仅支持配置local\_comm\_res场景使用。
-   调用该接口之前，需要先调用unlink进行断链，否则会导致HCCL报错。











