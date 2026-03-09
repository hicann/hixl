# LLM-DataDist错误码

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

| 枚举值 | 含义 | 是否可恢复 | 解决办法 |
| --- | --- | --- | --- |
| LLM_SUCCESS | 成功 | 无 | 无 |
| LLM_FAILED | 通用失败 | 否 | 保留现场，获取Host/Device日志，并备份。 |
| LLM_WAIT_PROC_TIMEOUT | 处理超时 | 是 | - 如果是PullKvCache、PullKvBlocks等传输相关接口报该错误，该链路不可恢复，需重新建链。<br>  - 其他接口报该异常，加大超时时间并重试。 |
| LLM_KV_CACHE_NOT_EXIST | KV不存在 | 是 | - 检查cache_id是否正确。<br>  - 检查是否Cache已经释放。<br>  - 检查对应全量侧报错日志中的请求是否完成。<br>  - 检查是否存在重复拉取。 |
| LLM_PARAM_INVALID | 参数错误 | 是 | 基于日志排查错误原因。 |
| LLM_NOT_YET_LINK | 没有建链 | 是 | 上层排查Decode与Prompt建链情况。 |
| LLM_ALREADY_LINK | 重复建链 | 是 | 上层排查Decode与Prompt建链情况。 |
| LLM_LINK_FAILED | 建链失败 | 是 | LinkLlmClusters第二个返回值中有该错误码时，需要检查对应集群之间的网络连接。 |
| LLM_UNLINK_FAILED | 断链失败 | 是 | UnlinkLlmClusters第二个返回值中有该错误码时，需要检查对应集群之间的网络连接。 |
| LLM_NOTIFY_PROMPT_UNLINK_FAILED | 通知Prompt侧断链失败 | 是 | 1. 排查Decode与Prompt之间的网络连接。<br>  2. 主动调Prompt侧的UnlinkLlmClusters接口清理残留资源。 |
| LLM_CLUSTER_NUM_EXCEED_LIMIT | 集群数量超过限制 | 是 | 检查LinkLlmClusters和UnlinkLlmClusters传入参数，clusters数量不能超过16。 |
| LLM_PROCESSING_LINK | 正在处理建链 | 是 | 当前正在执行建链或断链操作，请稍后再试。 |
| LLM_DEVICE_OUT_OF_MEMORY | Device内存不足 | 是 | 检查申请的内存是否没有释放。 |
| LLM_EXIST_LINK | 设置角色时，存在未释放的链接 | 是 | 检查在SetRole前是否已经调用UnlinkLlmClusters断开所有的链接。 |
| LLM_FEATURE_NOT_ENABLED | 特性未使能 | 是 | 检查初始化LLM-DataDist时是否传入了必要option。<br>检查是否调用了不支持的接口。 |
| LLM_TIMEOUT | 处理超时 | 否 | 保留现场，获取Host/Device日志，并备份。 |
| LLM_LINK_BUSY | 链路忙 | 是 | 预留错误码，暂不会返回。 |
| LLM_OUT_OF_MEMORY | 内存不足 | 是 | 检查内存池或系统内存是否充足。 |
