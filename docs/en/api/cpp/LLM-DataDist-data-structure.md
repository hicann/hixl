# LLM-DataDist Data Structures

<!-- md-trans-meta sourceCommit=27c5648662778a98fcba982f28a7e86b13f8019c translatedAt=2026-10-21T07:15:41.579Z pushedAt=2026-10-27T02:46:32.040Z nl=2 -->

## LlmRole

LLM-DataDist role.

```cpp
enum class LlmRole : int32_t {
  kPrompt = 1,      // Prompt role.
  kDecoder = 2,     // Decoder role.
  kMix = 3,         // Mix role.
  kEnd              // Invalid value.
};
```

## CachePlacement

Cache memory type.

```cpp
enum class CachePlacement : uint32_t {
  kHost = 0U,             // Cache in Host memory.
  kDevice = 1U,           // Cache in Device memory.
};
```

## CacheDesc

Cache description.

```cpp
struct CacheDesc {
  CachePlacement placement = CachePlacement::kDevice;    // Memory type.
  uint32_t num_tensors = 0U;                             // Number of tensors in the Cache.
  DataType data_type = DT_UNDEFINED;                     // Data type of tensors in the Cache.
  std::vector<int64_t> shape;                            // Shape of tensors in the Cache.
  uint8_t reserved[128];                                 // Reserved.
};
```

## CacheIndex

Cache index.

```cpp
struct CacheIndex {
  uint64_t cluster_id;        // ID of the cluster containing the cache.
  int64_t cache_id;           // Cache ID.
  uint32_t batch_index;       // Batch index specified for PullKvCache.
  uint8_t reserved[128];      // Reserved.
};
```

## Cache

Cache maintaining a group of tensor addresses.

```cpp
struct Cache {
  int64_t cache_id = -1;                     // Cache ID.
  std::vector<uintptr_t> tensor_addrs;       // Addresses of tensors in the Cache. In a single-process, multiple-device scenario, addresses are arranged by device in order.
  CacheDesc cache_desc;                      // Cache description.
  uint8_t reserved[128];                     // Reserved.
};
```

## ClusterInfo and IpInfo

Cluster information for connection establishment and disconnection.

```cpp
struct ClusterInfo {
  uint64_t remote_cluster_id = 0U;     // cluster_id of the peer LLM-DataDist.
  int32_t remote_role_type = 0;        // role_type of the peer LLM-DataDist. 0 indicates Prompt (full inference); 1 indicates Decode (incremental inference).
  std::vector<IpInfo> local_ip_infos;  // Local LLM-DataDist IP information. See the IpInfo structure below.
  std::vector<IpInfo> remote_ip_infos; // Peer LLM-DataDist IP information. See the IpInfo structure below.
  uint8_t reserved[128];               // Reserved.
};

struct IpInfo {
  AscendString ip;         // IP address.
  uint16_t port = 0U;      // Port number.
  uint8_t reserved[128];   // Reserved.
};
```

## KvCacheExtParam

Extended parameters for Pull- or Push-related APIs.

```cpp
struct KvCacheExtParam {
  std::pair<int32_t, int32_t> src_layer_range = {-1, -1};  // Source layer range for KV transfers.
  std::pair<int32_t, int32_t> dst_layer_range{-1, -1};  // Destination layer range for KV transfers.
  uint8_t tensor_num_per_layer = 2U;                       // Tensors per layer for KV transfers.
  uint8_t reserved[127];                                   // Reserved field.
};
```

## RegisterCfg

Configuration parameters passed to RegisterKvCache.

```cpp
struct RegisterCfg {
  uint8_t reserved[128] = {0};  // Reserved field.
};
```
