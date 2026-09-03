# HIXL错误码

错误码是通过如下定义的，类型为int。

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

具体错误码含义如下。

| 枚举值 | 含义 | 是否可恢复 | 解决办法 |
| --- | --- | --- | --- |
| SUCCESS | 成功 | 无 | 不涉及。 |
| PARAM_INVALID | 参数错误 | 是 | 基于日志排查错误原因。 |
| TIMEOUT | 处理超时 | 否 | 保留现场，获取Host/Device日志，并备份。 |
| NOT_CONNECTED | 没有建链 | 是 | 上层排查建链情况。 |
| ALREADY_CONNECTED | 已经建链 | 是 | 上层排查建链情况。 |
| NOTIFY_FAILED | 通知失败 | 否 | 预留错误码，暂不会返回。 |
| UNSUPPORTED | 不支持的参数或接口 | 是 | 根据接口返回信息确认是否支持当前配置或特性，若不支持，请使用已经支持的参数或接口。 |
| FAILED | 通用失败 | 否 | 保留现场，获取Host/Device日志，并备份。 |
| RESOURCE_EXHAUSTED | 资源耗尽，当前仅包含stream资源 | 是 | 等待资源释放后再进行尝试。 |
