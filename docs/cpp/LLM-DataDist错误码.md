# LLM-DataDist错误码<a name="ZH-CN_TOPIC_0000002374250044"></a>

错误码是通过如下宏定义的。

```
namespace llm_datadist {
constexpr Status LLM_SUCCESS = 0x0U;
constexpr Status LLM_FAILED = 0xFFFFFFFFU;
constexpr Status LLM_WAIT_PROC_TIMEOUT = 0x5010B001U;
constexpr Status LLM_KV_CACHE_NOT_EXIST = 0x5010B002U;
constexpr Status LLM_PARAM_INVALID = 0x5010B005U;
constexpr Status LLM_NOT_YET_LINK = 0x5010B007U;
constexpr Status LLM_ALREADY_LINK = 0x5010B008U;
constexpr Status LLM_LINK_FAILED = 0x5010B009U;
constexpr Status LLM_UNLINK_FAILED = 0x5010B00AU;
constexpr Status LLM_NOTIFY_PROMPT_UNLINK_FAILED = 0x5010B00BU;
constexpr Status LLM_CLUSTER_NUM_EXCEED_LIMIT = 0x5010B00CU;
constexpr Status LLM_PROCESSING_LINK = 0x5010B00DU;
constexpr Status LLM_DEVICE_OUT_OF_MEMORY = 0x5010B00EU;
constexpr Status LLM_EXIST_LINK = 0x5010B018U;
constexpr Status LLM_FEATURE_NOT_ENABLED = 0x5010B019U;
constexpr Status LLM_TIMEOUT = 0x5010B01AU;
constexpr Status LLM_LINK_BUSY = 0x5010B01BU;
constexpr Status LLM_OUT_OF_MEMORY = 0x5010B01CU;
}  // namespace llm_datadist
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
<tbody><tr id="row579015813543"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p67911835418"><a name="p67911835418"></a><a name="p67911835418"></a>LLM_SUCCESS</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p117912815412"><a name="p117912815412"></a><a name="p117912815412"></a>成功</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p7694192815408"><a name="p7694192815408"></a><a name="p7694192815408"></a>无</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1785153464014"><a name="p1785153464014"></a><a name="p1785153464014"></a>无</p>
</td>
</tr>
<tr id="row024710221414"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p19683111558"><a name="p19683111558"></a><a name="p19683111558"></a>LLM_FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p102471228410"><a name="p102471228410"></a><a name="p102471228410"></a>通用失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p56940282403"><a name="p56940282403"></a><a name="p56940282403"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p585113454013"><a name="p585113454013"></a><a name="p585113454013"></a>保留现场，获取Host/Device日志，并备份。</p>
</td>
</tr>
<tr id="row816634619448"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p650893131611"><a name="p650893131611"></a><a name="p650893131611"></a>LLM_WAIT_PROC_TIMEOUT</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p16508133113165"><a name="p16508133113165"></a><a name="p16508133113165"></a>处理超时</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p105081631171611"><a name="p105081631171611"></a><a name="p105081631171611"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><a name="ul1985759181216"></a><a name="ul1985759181216"></a><ul id="ul1985759181216"><li>如果是PullKvCache</a>、PullKvBlocks</a>等传输相关接口报该错误，该链路不可恢复，需重新建链。</li><li>其他接口报该异常，加大超时时间并重试。</li></ul>
</td>
</tr>
<tr id="row15325320162816"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p6134450175511"><a name="p6134450175511"></a><a name="p6134450175511"></a>LLM_KV_CACHE_NOT_EXIST</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p24578313550"><a name="p24578313550"></a><a name="p24578313550"></a>KV不存在</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p36941928104017"><a name="p36941928104017"></a><a name="p36941928104017"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><a name="ul41054220297"></a><a name="ul41054220297"></a><ul id="ul41054220297"><li>检查cache_id是否正确。</li><li>检查是否Cache已经释放。</li><li>检查对应全量侧报错日志中的请求是否完成。</li><li>检查是否存在重复拉取。</li></ul>
</td>
</tr>
<tr id="row1612782310553"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p434764425510"><a name="p434764425510"></a><a name="p434764425510"></a>LLM_PARAM_INVALID</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p10127142312558"><a name="p10127142312558"></a><a name="p10127142312558"></a>参数错误</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p17694152824015"><a name="p17694152824015"></a><a name="p17694152824015"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1985034114010"><a name="p1985034114010"></a><a name="p1985034114010"></a>基于日志排查错误原因。</p>
</td>
</tr>
<tr id="row12475964223"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p4475562222"><a name="p4475562222"></a><a name="p4475562222"></a>LLM_NOT_YET_LINK</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p4475136102213"><a name="p4475136102213"></a><a name="p4475136102213"></a>没有建链</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p1919453514410"><a name="p1919453514410"></a><a name="p1919453514410"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p19854344400"><a name="p19854344400"></a><a name="p19854344400"></a>上层排查Decode与Prompt建链情况。</p>
</td>
</tr>
<tr id="row18538101717229"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p853831792219"><a name="p853831792219"></a><a name="p853831792219"></a>LLM_ALREADY_LINK</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1053811712212"><a name="p1053811712212"></a><a name="p1053811712212"></a>重复建链</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p1069432820403"><a name="p1069432820403"></a><a name="p1069432820403"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p58523416407"><a name="p58523416407"></a><a name="p58523416407"></a>上层排查Decode与Prompt建链情况。</p>
</td>
</tr>
<tr id="row8514112814227"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1151519282224"><a name="p1151519282224"></a><a name="p1151519282224"></a>LLM_LINK_FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1051516282224"><a name="p1051516282224"></a><a name="p1051516282224"></a>建链失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p1369512813402"><a name="p1369512813402"></a><a name="p1369512813402"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p88380322379"><a name="p88380322379"></a><a name="p88380322379"></a>LinkLlmClusters</a>第二个返回值中有该错误码时，需要检查对应集群之间的网络连接。</p>
</td>
</tr>
<tr id="row31771641152211"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1517719415223"><a name="p1517719415223"></a><a name="p1517719415223"></a>LLM_UNLINK_FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p4177164118223"><a name="p4177164118223"></a><a name="p4177164118223"></a>断链失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p66951228114014"><a name="p66951228114014"></a><a name="p66951228114014"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p201342042133720"><a name="p201342042133720"></a><a name="p201342042133720"></a>UnlinkLlmClusters</a>第二个返回值中有该错误码时，需要检查对应集群之间的网络连接。</p>
</td>
</tr>
<tr id="row1444475042210"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p11444125022210"><a name="p11444125022210"></a><a name="p11444125022210"></a>LLM_NOTIFY_PROMPT_UNLINK_FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1844412502224"><a name="p1844412502224"></a><a name="p1844412502224"></a>通知Prompt侧断链失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p12695192824010"><a name="p12695192824010"></a><a name="p12695192824010"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><a name="ol10774115212272"></a><a name="ol10774115212272"></a><ol id="ol10774115212272"><li>排查Decode与Prompt之间的网络连接。</li><li>主动调Prompt侧的UnlinkLlmClusters</a>接口清理残留资源。</li></ol>
</td>
</tr>
<tr id="row1035323914230"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p103531139182316"><a name="p103531139182316"></a><a name="p103531139182316"></a>LLM_CLUSTER_NUM_EXCEED_LIMIT</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p123541439162312"><a name="p123541439162312"></a><a name="p123541439162312"></a>集群数量超过限制</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p10695102884016"><a name="p10695102884016"></a><a name="p10695102884016"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p485143413402"><a name="p485143413402"></a><a name="p485143413402"></a>检查LinkLlmClusters</a>和UnlinkLlmClusters</a>传入参数，clusters数量不能超过16。</p>
</td>
</tr>
<tr id="row16227125518239"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p16227145510236"><a name="p16227145510236"></a><a name="p16227145510236"></a>LLM_PROCESSING_LINK</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p20227155152313"><a name="p20227155152313"></a><a name="p20227155152313"></a>正在处理建链</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p1669532874016"><a name="p1669532874016"></a><a name="p1669532874016"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p18851734114016"><a name="p18851734114016"></a><a name="p18851734114016"></a>当前正在执行建链或断链操作，请稍后再试。</p>
</td>
</tr>
<tr id="row1121753094517"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p162173302458"><a name="p162173302458"></a><a name="p162173302458"></a>LLM_DEVICE_OUT_OF_MEMORY</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p132171430164516"><a name="p132171430164516"></a><a name="p132171430164516"></a>Device内存不足</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p121713024518"><a name="p121713024518"></a><a name="p121713024518"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p32171530114512"><a name="p32171530114512"></a><a name="p32171530114512"></a>检查申请的内存是否没有释放。</p>
</td>
</tr>
<tr id="row1532815012305"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1132850133016"><a name="p1132850133016"></a><a name="p1132850133016"></a>LLM_EXIST_LINK</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p183282010302"><a name="p183282010302"></a><a name="p183282010302"></a>设置角色时，存在未释放的链接</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p20328100163011"><a name="p20328100163011"></a><a name="p20328100163011"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p143284015303"><a name="p143284015303"></a><a name="p143284015303"></a>检查在SetRole前是否已经调用UnlinkLlmClusters</a>断开所有的链接。</p>
</td>
</tr>
<tr id="row119821824301"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p9982162163017"><a name="p9982162163017"></a><a name="p9982162163017"></a>LLM_FEATURE_NOT_ENABLED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1898242163010"><a name="p1898242163010"></a><a name="p1898242163010"></a>特性未使能</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p189827283010"><a name="p189827283010"></a><a name="p189827283010"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p46501596328"><a name="p46501596328"></a><a name="p46501596328"></a>检查初始化LLM-DataDist时是否传入了必要option。</p>
<p id="p16269144333317"><a name="p16269144333317"></a><a name="p16269144333317"></a>检查是否调用了不支持的接口。</p>
</td>
</tr>
<tr id="row15443154412310"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1044310442234"><a name="p1044310442234"></a><a name="p1044310442234"></a>LLM_TIMEOUT</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p17443344192314"><a name="p17443344192314"></a><a name="p17443344192314"></a>处理超时</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p844384413234"><a name="p844384413234"></a><a name="p844384413234"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1443444102314"><a name="p1443444102314"></a><a name="p1443444102314"></a>保留现场，获取Host/Device日志，并备份。</p>
</td>
</tr>
<tr id="row1833172516339"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p183382517337"><a name="p183382517337"></a><a name="p183382517337"></a>LLM_LINK_BUSY</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p16833825133317"><a name="p16833825133317"></a><a name="p16833825133317"></a>链路忙</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p1583342563310"><a name="p1583342563310"></a><a name="p1583342563310"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1783372573314"><a name="p1783372573314"></a><a name="p1783372573314"></a>预留错误码，暂不会返回。</p>
</td>
</tr>
<tr id="row277474265812"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p93645462582"><a name="p93645462582"></a><a name="p93645462582"></a>LLM_OUT_OF_MEMORY</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p103642046155817"><a name="p103642046155817"></a><a name="p103642046155817"></a>内存不足</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p7364346165817"><a name="p7364346165817"></a><a name="p7364346165817"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1936411464582"><a name="p1936411464582"></a><a name="p1936411464582"></a>检查内存池或系统内存是否充足。</p>

</td>
</tr>
</tbody>
</table>

