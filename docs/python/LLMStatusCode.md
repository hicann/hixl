# LLMStatusCode<a name="ZH-CN_TOPIC_0000002374252148"></a>

LLMException中status\_code对应的枚举类，枚举值及解决方法如下表。

<a name="table124618224416"></a>
<table><thead align="left"><tr id="row7246102215412"><th class="cellrowborder" valign="top" width="34.26342634263426%" id="mcps1.1.5.1.1"><p id="p324682215414"><a name="p324682215414"></a><a name="p324682215414"></a>枚举值</p>
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
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1993462415105"><a name="p1993462415105"></a><a name="p1993462415105"></a>重启机器或容器。</p>
<p id="p585113454013"><a name="p585113454013"></a><a name="p585113454013"></a>保留现场，获取Host/Device日志，并备份。</p>
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
<tr id="row145793116555"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p6134450175511"><a name="p6134450175511"></a><a name="p6134450175511"></a>LLM_KV_CACHE_NOT_EXIST</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p24578313550"><a name="p24578313550"></a><a name="p24578313550"></a>KV不存在</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p36941928104017"><a name="p36941928104017"></a><a name="p36941928104017"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><a name="ul41054220297"></a><a name="ul41054220297"></a><ul id="ul41054220297"><li>检查对应全量侧报错日志中的请求是否完成。</li><li>检查是否存在重复拉取。</li><li>检查标记目标cache的参数是否错误。</li></ul>
</td>
</tr>
<tr id="row85851234105510"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p684815220553"><a name="p684815220553"></a><a name="p684815220553"></a>LLM_REPEAT_REQUEST</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p20585103419552"><a name="p20585103419552"></a><a name="p20585103419552"></a>重复请求</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p4694528204019"><a name="p4694528204019"></a><a name="p4694528204019"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p38513454011"><a name="p38513454011"></a><a name="p38513454011"></a>检查是否存在重复调用。</p>
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
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1053811712212"><a name="p1053811712212"></a><a name="p1053811712212"></a>已经建过链</p>
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
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p88380322379"><a name="p88380322379"></a><a name="p88380322379"></a>link_clusters</a>第二个返回值中有该错误码时，需要检查对应集群之间的网络连接。</p>
</td>
</tr>
<tr id="row31771641152211"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1517719415223"><a name="p1517719415223"></a><a name="p1517719415223"></a>LLM_UNLINK_FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p4177164118223"><a name="p4177164118223"></a><a name="p4177164118223"></a>断链失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p66951228114014"><a name="p66951228114014"></a><a name="p66951228114014"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p201342042133720"><a name="p201342042133720"></a><a name="p201342042133720"></a>unlink_clusters</a>第二个返回值中有该错误码时，需要检查对应集群之间的网络连接。</p>
</td>
</tr>
<tr id="row1444475042210"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p11444125022210"><a name="p11444125022210"></a><a name="p11444125022210"></a>LLM_NOTIFY_PROMPT_UNLINK_FAILED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1844412502224"><a name="p1844412502224"></a><a name="p1844412502224"></a>通知Prompt侧断链失败</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p12695192824010"><a name="p12695192824010"></a><a name="p12695192824010"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><a name="ol10774115212272"></a><a name="ol10774115212272"></a><ol id="ol10774115212272"><li>排查Decode与Prompt之间的网络连接。</li><li>主动调Prompt侧的unlink_clusters</a>清理残留资源。</li></ol>
</td>
</tr>
<tr id="row1035323914230"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p103531139182316"><a name="p103531139182316"></a><a name="p103531139182316"></a>LLM_CLUSTER_NUM_EXCEED_LIMIT</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p123541439162312"><a name="p123541439162312"></a><a name="p123541439162312"></a>集群数量超过限制。</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p10695102884016"><a name="p10695102884016"></a><a name="p10695102884016"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p485143413402"><a name="p485143413402"></a><a name="p485143413402"></a>排查link_clusters</a>和unlink_clusters</a>传入参数，clusters数量不能超过16。</p>
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
<tr id="row19105151319240"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p151058134242"><a name="p151058134242"></a><a name="p151058134242"></a>LLM_PREFIX_ALREADY_EXIST</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p18106121312419"><a name="p18106121312419"></a><a name="p18106121312419"></a>前缀已经存在</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p2069552820406"><a name="p2069552820406"></a><a name="p2069552820406"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p118510345405"><a name="p118510345405"></a><a name="p118510345405"></a>检查是否已加载过相同Prefix Id的公共前缀。如果是，需要先释放。</p>
</td>
</tr>
<tr id="row15898307246"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1458910303248"><a name="p1458910303248"></a><a name="p1458910303248"></a>LLM_PREFIX_NOT_EXIST</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p16589130102416"><a name="p16589130102416"></a><a name="p16589130102416"></a>前缀不存在</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p6695172864012"><a name="p6695172864012"></a><a name="p6695172864012"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p13851234134010"><a name="p13851234134010"></a><a name="p13851234134010"></a>检查Request中的Prefix Id是否已加载过。</p>
</td>
</tr>
<tr id="row1532815012305"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1132850133016"><a name="p1132850133016"></a><a name="p1132850133016"></a>LLM_EXIST_LINK</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p183282010302"><a name="p183282010302"></a><a name="p183282010302"></a>switch_role时，存在未释放的链接。</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p20328100163011"><a name="p20328100163011"></a><a name="p20328100163011"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p143284015303"><a name="p143284015303"></a><a name="p143284015303"></a>检查在切换当前LLMDataDist的角色前是否已经调用unlink_clusters</a>断开所有的链接。</p>
</td>
</tr>
<tr id="row119821824301"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p9982162163017"><a name="p9982162163017"></a><a name="p9982162163017"></a>LLM_FEATURE_NOT_ENABLED</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1898242163010"><a name="p1898242163010"></a><a name="p1898242163010"></a>特性未使能</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p189827283010"><a name="p189827283010"></a><a name="p189827283010"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p46501596328"><a name="p46501596328"></a><a name="p46501596328"></a>检查初始化LLMDataDist时是否传入了必要option。</p>
<p id="p16269144333317"><a name="p16269144333317"></a><a name="p16269144333317"></a>如果是切换当前LLMDataDist的角色时抛出该异常，排查初始化时LLMConfig是否设置了enable_switch_role = True。</p>
</td>
</tr>
<tr id="row15443154412310"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p1044310442234"><a name="p1044310442234"></a><a name="p1044310442234"></a>LLM_TIMEOUT</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p17443344192314"><a name="p17443344192314"></a><a name="p17443344192314"></a>处理超时</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p844384413234"><a name="p844384413234"></a><a name="p844384413234"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><a name="ul02175372013"></a><a name="ul02175372013"></a><ul id="ul02175372013"><li>如果是pull_cache</a>、pull_blocks</a>、transfer_cache_async</a>等传输相关接口报错，该链路不可恢复，需重新建链。</li><li>其他接口报该异常，加大超时时间并重试。</li></ul>
</td>
</tr>
<tr id="row4762643192212"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p11762843132215"><a name="p11762843132215"></a><a name="p11762843132215"></a>LLM_LINK_BUSY</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p276254302213"><a name="p276254302213"></a><a name="p276254302213"></a>链路繁忙</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p117621143162216"><a name="p117621143162216"></a><a name="p117621143162216"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p17762184310222"><a name="p17762184310222"></a><a name="p17762184310222"></a>检查同时调用的接口是否有冲突，例如：同时调用如下接口时，会报该错误码。</p>
<li>同时调用unlink</a>和pull_cache</a>。
</li><li>使用相同链路同时调用pull_cache</a>和transfer_cache_async</a>。</li></ul>
</td>
</tr>
<tr id="row13117497241"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p22184952412"><a name="p22184952412"></a><a name="p22184952412"></a>LLM_OUT_OF_MEMORY</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p62849192410"><a name="p62849192410"></a><a name="p62849192410"></a>内存不足</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p2294952415"><a name="p2294952415"></a><a name="p2294952415"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1675154613254"><a name="p1675154613254"></a><a name="p1675154613254"></a>检查内存池是否足够容纳申请的KV大小。</p>
<p id="p12114932415"><a name="p12114932415"></a><a name="p12114932415"></a>检查申请的内存是否没有释放。</p>
</td>
</tr>
<tr id="row145964718194"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p65976791916"><a name="p65976791916"></a><a name="p65976791916"></a>LLM_DEVICE_MEM_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p86801366195"><a name="p86801366195"></a><a name="p86801366195"></a>出现内存UCE（uncorrect error，指系统硬件不能直接处理恢复内存错误）的错误虚拟地址</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p155971670190"><a name="p155971670190"></a><a name="p155971670190"></a>是</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p12312104312411"><a name="p12312104312411"></a><a name="p12312104312411"></a>请参考<span id="ph963712461518"><a name="ph963712461518"></a><a name="ph963712461518"></a>《Ascend Extension for PyTorch 自定义API参考》</span>中的torch_npu.npu.restart_device接口的说明获取并修复内存UCE的错误虚拟地址。如果是KV Cache内存，需要再调用cache manager的remap_registered_memory</a>接口修复注册给网卡的KV Cache内存。</p>
<div class="note" id="note17365201515316"><a name="note17365201515316"></a><a name="note17365201515316"></a><span class="notetitle"> 说明： </span><div class="notebody"><p id="p133650151335"><a name="p133650151335"></a><a name="p133650151335"></a>本错误码为预留，暂不支持。</p>
</div></div>
</td>
</tr>
<tr id="row1030133172215"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p530115317223"><a name="p530115317223"></a><a name="p530115317223"></a>LLM_SUSPECT_REMOTE_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p1032419212320"><a name="p1032419212320"></a><a name="p1032419212320"></a>疑似是UCE内存故障</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p33011936223"><a name="p33011936223"></a><a name="p33011936223"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p1586415339239"><a name="p1586415339239"></a><a name="p1586415339239"></a>上层框架需要结合其它故障进行综合判断是UCE内存故障还是他故障。</p>
</td>
</tr>
<tr id="row14741204713015"><td class="cellrowborder" valign="top" width="34.26342634263426%" headers="mcps1.1.5.1.1 "><p id="p162864718254"><a name="p162864718254"></a><a name="p162864718254"></a>LLM_UNKNOWN_ERROR</p>
</td>
<td class="cellrowborder" valign="top" width="18.421842184218423%" headers="mcps1.1.5.1.2 "><p id="p192861074257"><a name="p192861074257"></a><a name="p192861074257"></a>未知错误</p>
</td>
<td class="cellrowborder" valign="top" width="14.151415141514152%" headers="mcps1.1.5.1.3 "><p id="p1669514284402"><a name="p1669514284402"></a><a name="p1669514284402"></a>否</p>
</td>
<td class="cellrowborder" valign="top" width="33.16331633163316%" headers="mcps1.1.5.1.4 "><p id="p149401316470"><a name="p149401316470"></a><a name="p149401316470"></a>保留现场，获取Host/Device日志，并备份。</p>
</td>
</tr>
</tbody>
</table>

