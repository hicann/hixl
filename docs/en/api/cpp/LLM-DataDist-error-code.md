# LLM-DataDist Error Codes

<!-- md-trans-meta sourceCommit=9524869fa42b724c5fa4b2e3cc6ab5e310ab84a6 translatedAt=2026-10-21T07:15:41.579Z pushedAt=2026-10-27T02:46:32.052Z nl=2 -->

Error codes are defined by the following macros.

```cpp
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

The error codes are described below.

| Enum Value | Meaning | Recoverable | Solution |
| --- | --- | --- | --- |
| LLM_SUCCESS | Success | None | None |
| LLM_FAILED | General failure | No | Preserve the environment, obtain the Host/Device logs, and back them up. |
| LLM_WAIT_PROC_TIMEOUT | Processing timed out | Yes | - If returned by a transfer API such as **PullKvCache** or **PullKvBlocks**, the connection is unrecoverable and must be reestablished.<br>  - For other APIs, increase the timeout and retry. |
| LLM_KV_CACHE_NOT_EXIST | KV does not exist | Yes | - Check whether **cache_id** is correct.<br>  - Check whether the Cache has been freed.<br>  - Check whether the request in the corresponding Prompt-side error log has completed.<br>  - Check for duplicate pulls. |
| LLM_PARAM_INVALID | Invalid parameter | Yes | Troubleshoot the cause using the logs. |
| LLM_NOT_YET_LINK | Not connected | Yes | Check the Decode-Prompt connection at the upper layer. |
| LLM_ALREADY_LINK | Already connected | Yes | Check the Decode-Prompt connection at the upper layer. |
| LLM_LINK_FAILED | Connection establishment failed | Yes | If this code appears in the second return value of **LinkLlmClusters**, check network connectivity between the corresponding clusters. |
| LLM_UNLINK_FAILED | Disconnection failed | Yes | If this code appears in the second return value of **UnlinkLlmClusters**, check network connectivity between the corresponding clusters. |
| LLM_NOTIFY_PROMPT_UNLINK_FAILED | Failed to notify the Prompt side to disconnect | Yes | 1. Check network connectivity between Decode and Prompt.<br>  2. Call **UnlinkLlmClusters** on the Prompt side to clean up remaining resources. |
| LLM_CLUSTER_NUM_EXCEED_LIMIT | Cluster count exceeds the limit | Yes | Check **LinkLlmClusters** and **UnlinkLlmClusters** parameters. **clusters** cannot contain more than 16 entries. |
| LLM_PROCESSING_LINK | Connection establishment in progress | Yes | Connection establishment or disconnection is in progress. Retry later. |
| LLM_DEVICE_OUT_OF_MEMORY | Insufficient Device memory | Yes | Check whether allocated memory has not been freed. |
| LLM_EXIST_LINK | Unreleased connections exist when setting the role | Yes | Check that **UnlinkLlmClusters** has disconnected all connections before **SetRole**. |
| LLM_FEATURE_NOT_ENABLED | Feature not enabled | Yes | Check that required options were passed during LLM-DataDist initialization.<br>Check whether an unsupported API was called. |
| LLM_TIMEOUT | Processing timed out | No | Preserve the environment, obtain the Host/Device logs, and back them up. |
| LLM_LINK_BUSY | Connection busy | Yes | Reserved error code; currently not returned. |
| LLM_OUT_OF_MEMORY | Insufficient memory | Yes | Check that the memory pool or system has sufficient memory. |
