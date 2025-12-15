# HIXL错误码<a name="ZH-CN_TOPIC_0000002413184460"></a>

错误码是通过如下定义的，类型为uint32\_t。

```
// status codes
constexpr Status SUCCESS = 0U;
constexpr Status PARAM_INVALID = 103900U;
constexpr Status TIMEOUT = 103901U;
constexpr Status NOT_CONNECTED = 103902U;
constexpr Status ALREADY_CONNECTED = 103903U;
constexpr Status NOTIFY_FAILED = 103904U;
constexpr Status UNSUPPORTED = 103905U;
constexpr Status FAILED = 503900U;
constexpr Status RESOURCE_EXHAUSTED = 203900U;
```

具体错误码含义如下。

<a name="table124618224416"></a>
<table><thead align="left"><tr id="row833920317342"><th class="cellrowborder" valign="top" width="34.26342634263426%" id="mcps1.1.5.1.1"><p id="p324682215414"><a name="p324682215414"></a><a name="p324682215414"></a>枚举值</p>
</th>
<th class="cellrowborder" valign="top" width="18.421842184218423%" id="mcps1.1.5.1.2"><p id="p132471122448"><a name="p132471122448"></a><a name="p132471122448"></a>含义</p>
</th>
<th class="cellrowborder" valign="top" width="14.151415141514152%" id="mcps1.1.5.1.3"><p id="p26947289405"><a name="p26947289405"></a><a name="p26947289405"></a>是否可恢复</p>
</th>
<th class="cellrowborder" valign="top" width="33.16331633163316%" id="mcps1.1.5.1.4"><p id="p1385143414013"><a name="p1385143414013"></a><a name="p1385143414013"></a>解决办法</p>
</th>
</tr>
</thead>
<tbody><tr id="row579015813543"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p67911835418"><a name="p67911835418"></a><a name="p67911835418"></a>SUCCESS</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p117912815412"><a name="p117912815412"></a><a name="p117912815412"></a>成功</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p7694192815408"><a name="p7694192815408"></a><a name="p7694192815408"></a>无</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1785153464014"><a name="p1785153464014"></a><a name="p1785153464014"></a>不涉及。</p>
</td>
</tr>
<tr id="row024710221414"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p6114134464315"><a name="p6114134464315"></a><a name="p6114134464315"></a>PARAM_INVALID</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p201143445431"><a name="p201143445431"></a><a name="p201143445431"></a>参数错误</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p2011444434319"><a name="p2011444434319"></a><a name="p2011444434319"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p2114154420439"><a name="p2114154420439"></a><a name="p2114154420439"></a>基于日志排查错误原因。</p>
</td>
</tr>
<tr id="row1612782310553"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1044310442234"><a name="p1044310442234"></a><a name="p1044310442234"></a>TIMEOUT</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p17443344192314"><a name="p17443344192314"></a><a name="p17443344192314"></a>处理超时</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p844384413234"><a name="p844384413234"></a><a name="p844384413234"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1443444102314"><a name="p1443444102314"></a><a name="p1443444102314"></a>保留现场，获取Host/Device日志，并备份。</p>
</td>
</tr>
<tr id="row12475964223"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p4475562222"><a name="p4475562222"></a><a name="p4475562222"></a>NOT_CONNECTED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p4475136102213"><a name="p4475136102213"></a><a name="p4475136102213"></a>没有建链</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p1919453514410"><a name="p1919453514410"></a><a name="p1919453514410"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p19854344400"><a name="p19854344400"></a><a name="p19854344400"></a>上层排查建链情况。</p>
</td>
</tr>
<tr id="row18538101717229"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p853831792219"><a name="p853831792219"></a><a name="p853831792219"></a>ALREADY_CONNECTED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1053811712212"><a name="p1053811712212"></a><a name="p1053811712212"></a>已经建链</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p1069432820403"><a name="p1069432820403"></a><a name="p1069432820403"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p58523416407"><a name="p58523416407"></a><a name="p58523416407"></a>上层排查建链情况。</p>
</td>
</tr>
<tr id="row119821824301"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p169731139183119"><a name="p169731139183119"></a><a name="p169731139183119"></a>NOTIFY_FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p11973639153119"><a name="p11973639153119"></a><a name="p11973639153119"></a>通知失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p109732039153117"><a name="p109732039153117"></a><a name="p109732039153117"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p8973139183110"><a name="p8973139183110"></a><a name="p8973139183110"></a>预留错误码，暂不会返回。</p>
</td>
</tr>
<tr id="row15443154412310"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p9982162163017"><a name="p9982162163017"></a><a name="p9982162163017"></a>UNSUPPORTED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1898242163010"><a name="p1898242163010"></a><a name="p1898242163010"></a>不支持的参数或接口</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p189827283010"><a name="p189827283010"></a><a name="p189827283010"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p16269144333317"><a name="p16269144333317"></a><a name="p16269144333317"></a>预留错误码，暂不会返回。</p>
</td>
</tr>
<tr id="row1791926194512"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p19683111558"><a name="p19683111558"></a><a name="p19683111558"></a>FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p102471228410"><a name="p102471228410"></a><a name="p102471228410"></a>通用失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p56940282403"><a name="p56940282403"></a><a name="p56940282403"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p585113454013"><a name="p585113454013"></a><a name="p585113454013"></a>保留现场，获取Host/Device日志，并备份。</p>
<tr id="row277474265812"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p93645462582"><a name="p93645462582"></a><a name="p93645462582"></a>RESOURCE_EXHAUSTED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p103642046155817"><a name="p103642046155817"></a><a name="p103642046155817"></a>资源耗尽，当前仅包括stream资源</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p7364346165817"><a name="p7364346165817"></a><a name="p7364346165817"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1936411464582"><a name="p1936411464582"></a><a name="p1936411464582"></a>等待资源耗尽后再进行尝试。</p>
</td>
</tr>
</tbody>
</table>

