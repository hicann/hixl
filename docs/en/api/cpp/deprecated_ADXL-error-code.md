# ADXL Error Codes (To Be Deprecated)

<!-- md-trans-meta sourceCommit=9524869fa42b724c5fa4b2e3cc6ab5e310ab84a6 translatedAt=2026-10-21T07:15:41.571Z pushedAt=2026-10-27T02:46:32.117Z nl=2 -->

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
```

The error codes are described below.

|Enum Value|Meaning|Recoverable|Solution|
|--|--|--|--|
|SUCCESS|Success|None|Not applicable.|
|PARAM_INVALID|Invalid parameter|Yes|Troubleshoot the cause using the logs.|
|TIMEOUT|Processing timed out|No|Preserve the environment, obtain the Host/Device logs, and back them up.|
|NOT_CONNECTED|Not connected|Yes|Check the connection status at the upper layer.|
|ALREADY_CONNECTED|Already connected|Yes|Check the connection status at the upper layer.|
|NOTIFY_FAILED|Notification failed|No|Reserved error code; currently not returned.|
|UNSUPPORTED|Unsupported parameter or API|Yes|Reserved error code; currently not returned.|
|FAILED|General failure|No|Preserve the environment, obtain the Host/Device logs, and back them up.|
