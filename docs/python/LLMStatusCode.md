# LLMStatusCode

LLMException中status\_code对应的枚举类，枚举值及解决方法如下表。

| 枚举值 | 含义 | 是否可恢复 | 解决办法 |
| --- | --- | --- | --- |
| LLM_SUCCESS | 成功 | 无 | 无 |
| LLM_FAILED | 通用失败 | 否 | 重启机器或容器。<br>保留现场，获取Host/Device日志，并备份。 |
| LLM_PARAM_INVALID | 参数错误 | 是 | 基于日志排查错误原因。 |
| LLM_KV_CACHE_NOT_EXIST | KV不存在 | 是 | - 检查对应全量侧报错日志中的请求是否完成。<br>  - 检查是否存在重复拉取。<br>  - 检查标记目标cache的参数是否错误。 |
| LLM_REPEAT_REQUEST | 重复请求 | 是 | 检查是否存在重复调用。 |
| LLM_NOT_YET_LINK | 没有建链 | 是 | 上层排查Decode与Prompt建链情况。 |
| LLM_ALREADY_LINK | 已经建过链 | 是 | 上层排查Decode与Prompt建链情况。 |
| LLM_LINK_FAILED | 建链失败 | 是 | link_clusters第二个返回值中有该错误码时，需要检查对应集群之间的网络连接。 |
| LLM_UNLINK_FAILED | 断链失败 | 是 | unlink_clusters第二个返回值中有该错误码时，需要检查对应集群之间的网络连接。 |
| LLM_NOTIFY_PROMPT_UNLINK_FAILED | 通知Prompt侧断链失败 | 是 | 1. 排查Decode与Prompt之间的网络连接。<br>  2. 主动调Prompt侧的unlink_clusters清理残留资源。 |
| LLM_CLUSTER_NUM_EXCEED_LIMIT | 集群数量超过限制。 | 是 | 排查link_clusters和unlink_clusters传入参数，clusters数量不能超过16。 |
| LLM_PROCESSING_LINK | 正在处理建链 | 是 | 当前正在执行建链或断链操作，请稍后再试。 |
| LLM_PREFIX_ALREADY_EXIST | 前缀已经存在 | 是 | 检查是否已加载过相同Prefix Id的公共前缀。如果是，需要先释放。 |
| LLM_PREFIX_NOT_EXIST | 前缀不存在 | 是 | 检查Request中的Prefix Id是否已加载过。 |
| LLM_EXIST_LINK | switch_role时，存在未释放的链接。 | 是 | 检查在切换当前LLMDataDist的角色前是否已经调用unlink_clusters断开所有的链接。 |
| LLM_FEATURE_NOT_ENABLED | 特性未使能 | 是 | 检查初始化LLMDataDist时是否传入了必要option。<br>如果是切换当前LLMDataDist的角色时抛出该异常，排查初始化时LLMConfig是否设置了enable_switch_role = True。 |
| LLM_TIMEOUT | 处理超时 | 是 | - 如果是pull_cache、pull_blocks、transfer_cache_async等传输相关接口报错，该链路不可恢复，需重新建链。<br>  - 其他接口报该异常，加大超时时间并重试。 |
| LLM_LINK_BUSY | 链路繁忙 | 是 | 检查同时调用的接口是否有冲突，例如：同时调用如下接口时，会报该错误码。<br>同时调用unlink和pull_cache。<br>使用相同链路同时调用pull_cache和transfer_cache_async。 |
| LLM_OUT_OF_MEMORY | 内存不足 | 是 | 检查内存池是否足够容纳申请的KV大小。<br>检查申请的内存是否没有释放。 |
| LLM_DEVICE_MEM_ERROR | 出现内存UCE（incorrect error，指系统硬件不能直接处理恢复内存错误）的错误虚拟地址 | 是 | 获取并修复内存UCE的错误虚拟地址。如果是KV Cache内存，需要再调用cache manager的remap_registered_memory接口修复注册给网卡的KV Cache内存。<br> 说明： 本错误码为预留，暂不支持。 |
| LLM_SUSPECT_REMOTE_ERROR | 疑似是UCE内存故障 | 否 | 上层框架需要结合其它故障进行综合判断是UCE内存故障还是他故障。 |
| LLM_UNKNOWN_ERROR | 未知错误 | 否 | 保留现场，获取Host/Device日志，并备份。 |

请参考[PyTorch](https://www.hiascend.com/developer/software/ai-frameworks/pytorch)的torch\_npu.npu.restart\_device接口的说明获取并修复内存UCE的错误虚拟地址。
