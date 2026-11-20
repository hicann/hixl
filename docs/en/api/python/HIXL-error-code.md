# HIXL Error Codes

<!-- md-trans-meta sourceCommit=a92bd6c0d9d03c2cf9d76cffc1b450dfa592cb69 translatedAt=2026-10-21T07:15:41.592Z pushedAt=2026-10-27T02:46:32.279Z nl=2 -->

Error codes are defined as follows and have type **int**.

```python
SUCCESS = 0
PARAM_INVALID = 103900
TIMEOUT = 103901
NOT_CONNECTED = 103902
ALREADY_CONNECTED = 103903
NOTIFY_FAILED = 103904
UNSUPPORTED = 103905
FAILED = 503900
RESOURCE_EXHAUSTED = 203900
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
