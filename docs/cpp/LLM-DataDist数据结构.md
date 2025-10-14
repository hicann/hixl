# LLM-DataDist数据结构<a name="ZH-CN_TOPIC_0000002407889497"></a>
## LlmRole<a name="ZH-CN_TOPIC_0000002374250036"></a>
LLM-DataDist的角色

```
enum class LlmRole : int32_t {
  kPrompt = 1,      // 角色为Prompt
  kDecoder = 2,     // 角色为Decoder
  kMix = 3,         // 角色为Mix
  kEnd              // 无效值
}
```
## CachePlacement<a name="ZH-CN_TOPIC_0000002407889481"></a>
Cache的内存类型

```
enum class CachePlacement : uint32_t {
  kHost = 0U,             // Cache为Host内存
  kDevice = 1U,           // Cache为Device内存
}
```
## CacheDesc<a name="ZH-CN_TOPIC_0000002374409968"></a>
Cache的描述信息

```
struct CacheDesc {
  CachePlacement placement = CachePlacement::kDevice;    // 内存类型
  uint32_t num_tensors = 0U;                             // Cache包含的tensor个数
  DataType data_type = DT_UNDEFINED;                     // Cache中tensor的数据类型
  std::vector<int64_t> shape;                            // Cache中tensor的shape
  uint8_t reserved[128];                                 // 预留
}
```
## CacheIndex<a name="ZH-CN_TOPIC_0000002374250076"></a>

Cache的索引

```
struct CacheIndex {
  uint64_t cluster_id;        // cache所在的集群ID
  int64_t cache_id;           // cache的ID
  uint32_t batch_index;       // PullKvCache时用于指定batch的下标
  uint8_t reserved[128];      // 预留
}
```
## Cache<a name="ZH-CN_TOPIC_0000002408009637"></a>

Cache，其中维护了一组tensor的地址

```
struct Cache {
  int64_t cache_id = -1;                     // Cache的ID
  std::vector<uintptr_t> tensor_addrs;       // Cache中各tensor的地址, 在单进程多卡场景中，多卡的地址依次排列。
  CacheDesc cache_desc;                      // Cache描述
  uint8_t reserved[128];                     // 预留
}
```
## ClusterInfo和IpInfo<a name="ZH-CN_TOPIC_0000002374250088"></a>

用于描述集群信息，用于建链与断链。

```
struct ClusterInfo {
  uint64_t remote_cluster_id = 0U;     // 对端的LLM-DataDist的cluster_id
  int32_t remote_role_type = 0;        // 对端的LLM-DataDist的role_type，0表示全量，1表示增量
  std::vector<IpInfo> local_ip_infos;  // 本地LLM-DataDist的IP信息，详见如下结构体IpInfo
  std::vector<IpInfo> remote_ip_infos; // 对端LLM-DataDist的IP信息，详见如下结构体IpInfo
  uint8_t reserved[128];               // 预留
}

struct IpInfo {
  AscendString ip;         // IP地址
  uint16_t port = 0U;      // 端口号
  uint8_t reserved[128];   // 预留
}
```
## KvCacheExtParam<a name="ZH-CN_TOPIC_0000002374409956"></a>

调用Pull或Push相关接口时传入的扩展参数。

```
struct KvCacheExtParam {
  std::pair<int32_t, int32_t> src_layer_range = {-1, -1};  // KV传输时源端的层数范围
  std::pair<int32_t, int32_t> dst_layer_range{-1, -1};  // KV传输时目的端的层数范围
  uint8_t tensor_num_per_layer = 2U;                       // KV传输时一层的tensor数量
  uint8_t reserved[127];                                   // 预留字段
}
```
## RegisterCfg<a name="ZH-CN_TOPIC_0000002408009633"></a>

调用RegisterKvCache接口时传入的配置参数。

```
struct RegisterCfg {
  uint8_t reserved[128] = {0};  // 预留字段
};
```