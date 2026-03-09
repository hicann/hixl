# LayerSynchronizer

抽象类，需要由用户继承该抽象类实现相关接口。当前该类下只有一个接口synchronize\_layer。

## 产品支持情况

| 产品 | 是否支持 |
| --- | --- |
| Ascend 950PR/Ascend 950DT | √ |
| Atlas A3 训练系列产品/Atlas A3 推理系列产品 | √ |
| Atlas A2 训练系列产品/Atlas A2 推理系列产品 | √ |

说明：针对Atlas A2 训练系列产品/Atlas A2 推理系列产品，仅支持Atlas 800I A2 推理服务器、Atlas 300I A2 推理卡、A200I A2 Box 异构组件。

## 函数功能

等待模型指定层执行完成，用户需要继承LayerSynchronizer并实现该接口。

该接口会在执行transfer\_cache\_async时被调用，当该接口返回成功，则开始当前层cache的传输。

## 函数原型

```
synchronize_layer(layer_index: int, timeout_in_millis: Optional[int]) -> bool
```

## 参数说明

| 参数名称 | 数据类型 | 取值说明 |
| --- | --- | --- |
| layer_index | int | 层的下标 |
| timeout_in_millis | Optional[int] | 超时时间，暂不支持。 |

## 调用示例

该接口不由用户直接调用，而是作为回调由CacheManager调用。

## 返回值

正常情况下返回是否同步成功。

参数错误可能抛出TypeError或ValueError。

## 约束说明

无
