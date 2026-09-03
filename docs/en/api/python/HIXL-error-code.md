# HIXL Error Codes

Error codes are defined as follows, type is int.

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

Detailed error code meanings are as follows.

| Enum Value | Description | Recoverable | Solution |
| --- | --- | --- | --- |
| SUCCESS | Success | N/A | Not applicable. |
| PARAM_INVALID | Parameter error | Yes | Investigate error cause based on logs. |
| TIMEOUT | Processing timeout | No | Preserve the scene, obtain Host/Device logs, and back up. |
| NOT_CONNECTED | No link established | Yes | Upper layer investigates link establishment status. |
| ALREADY_CONNECTED | Already linked | Yes | Upper layer investigates link establishment status. |
| NOTIFY_FAILED | Notification failed | No | Reserved error code, will not be returned currently. |
| UNSUPPORTED | Unsupported parameter or interface | Yes | Confirm whether current configuration or feature is supported based on interface return information. If not supported, use already supported parameters or interfaces. |
| FAILED | General failure | No | Preserve the scene, obtain Host/Device logs, and back up. |
| RESOURCE_EXHAUSTED | Resource exhausted, currently only includes stream resources | Yes | Wait for resources to be released before retrying. |
