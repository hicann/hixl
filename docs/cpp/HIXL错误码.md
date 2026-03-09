# HIXL错误码

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

| 枚举值 | 含义 | 是否可恢复 | 解决办法 |
| --- | --- | --- | --- |
| SUCCESS | 成功 | 无 | 不涉及。 |
| PARAM_INVALID | 参数错误 | 是 | 基于日志排查错误原因。 |
| TIMEOUT | 处理超时 | 否 | 保留现场，获取Host/Device日志，并备份。 |
| NOT_CONNECTED | 没有建链 | 是 | 上层排查建链情况。 |
| ALREADY_CONNECTED | 已经建链 | 是 | 上层排查建链情况。 |
| NOTIFY_FAILED | 通知失败 | 否 | 预留错误码，暂不会返回。 |
| UNSUPPORTED | 不支持的参数或接口 | 是 | 预留错误码，暂不会返回。 |
| FAILED | 通用失败 | 否 | 保留现场，获取Host/Device日志，并备份。
| RESOURCE_EXHAUSTED | 资源耗尽，当前仅包含stream资源 | 是 | 等待资源释放后再进行尝试。 |
