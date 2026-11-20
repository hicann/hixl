# HIXL Error Codes

<!-- md-trans-meta sourceCommit=12cb6594bcfbfd466c7647a4052c49d254a5a5b0 translatedAt=2026-10-21T07:15:41.575Z pushedAt=2026-10-27T02:46:32.002Z nl=2 -->

Error codes are defined as follows and have type **uint32\_t**.

```cpp
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

The error codes are described below.

| Enum Value | Meaning | Recoverable | Solution |
| --- | --- | --- | --- |
| SUCCESS | Success | None | Not applicable. |
| PARAM_INVALID | Invalid parameter | Yes | Troubleshoot the cause using the logs. |
| TIMEOUT | Processing timed out | No | Preserve the environment, obtain the Host/Device logs, and back them up. |
| NOT_CONNECTED | Not connected | Yes | Check the connection status at the upper layer. |
| ALREADY_CONNECTED | Already connected | Yes | Check the connection status at the upper layer. |
| NOTIFY_FAILED | Notification failed | No | Reserved error code; currently not returned. |
| UNSUPPORTED | Unsupported parameter or API | Yes | Check the API return information to determine whether the current configuration or feature is supported. If unsupported, use supported parameters or APIs. |
| FAILED | General failure | No | Preserve the environment, obtain the Host/Device logs, and back them up. |
| RESOURCE_EXHAUSTED | Resources exhausted; currently includes only stream resources. | Yes | Retry after resources are released. |
