# HIXL Data Structures

<!-- md-trans-meta sourceCommit=49bd2a256b728544c1e12df555c545670f9ab51a translatedAt=2026-10-21T07:15:41.574Z pushedAt=2026-10-27T02:46:31.993Z nl=2 -->

## MemDesc

Memory description.

```cpp
struct MemDesc {
  uintptr_t addr;
  size_t len;
  bool local_only = false;  // Whether the memory is for local use only and is not exposed to remote peers. Defaults to false.
  uint8_t reserved[127] = {};
};
```

## MemHandle

Memory handle.

```cpp
using MemHandle = void *;
```

## MemType

Memory type.

```cpp
enum MemType {
  MEM_DEVICE,
  MEM_HOST
};
```

## AsyncConnectStatus

Asynchronous connection/disconnection status.

```cpp
enum class AsyncConnectStatus {
  NOT_CONNECT,        // Not connected
  CONNECT_PENDING,    // Connection establishment pending
  CONNECTING,         // Connection establishment in progress
  CONNECTED,          // Connection established
  CONNECT_FAILED,     // Connection establishment failed
  DISCONNECT_PENDING, // Disconnection pending
  DISCONNECTING       // Disconnection in progress
};
```

## TransferOp

Transfer operation type.

```cpp
enum TransferOp {
  READ,
  WRITE
};
```

## TransferOpDesc

Transfer operation description.

```cpp
struct TransferOpDesc {
  uintptr_t local_addr;
  uintptr_t remote_addr;
  size_t len;
};
```

## TransferArgs

Optional transfer parameters.

```cpp
struct TransferArgs {
  const void *user_data = nullptr;  // User-defined information used with the API for querying all asynchronous transfer request statuses.
  uint8_t reserved[120] = {};  // Reserved parameter.
};
```

## TransferReq

Transfer request handle.

```cpp
using TransferReq = void *;
```

## TransferStatus

Asynchronous transfer status.

```cpp
enum class TransferStatus {
  WAITING,
  COMPLETED,
  TIMEOUT, //Currently unsupported
  FAILED
};
```

## GetTransferStatusArgs

Parameters for querying all asynchronous transfer request statuses.

```cpp
struct GetTransferStatusArgs {
  uint32_t max_query_count = UINT32_MAX;  // Maximum number of transfer request statuses to query.
  bool skip_waiting = false;  // Whether to skip transfer requests whose status is TransferStatus::WAITING.
  uint8_t reserved[123] = {};  // Reserved parameter.
};
```

## TransferResult

Result information for each request when querying all asynchronous transfer request statuses.

```cpp
struct TransferResult {
  TransferReq req = nullptr;  // Transfer request handle.
  const void *user_data = nullptr;  // User-defined information.
  TransferStatus status = TransferStatus::WAITING;  // Transfer request status.
  uint8_t reserved[108] = {};  // Reserved parameter.
};
```

## NotifyDesc

Notify description.

```cpp
struct NotifyDesc {
  AscendString name;
  AscendString notify_msg;
};
```

## FeatureType

Library capability type queried through **GetCapability**. Enum values must be assigned explicitly. New capabilities may be appended only at the end.

```cpp
enum FeatureType : int32_t {
  AUTO_CONNECT = 0,
  CLIENT_SERVER_COMM = 1,
};
```

| Enum Value | Description |
| --- | --- |
| AUTO_CONNECT | Auto Connect mode, corresponding to the **OPTION_AUTO_CONNECT** option in **Initialize**. |
| CLIENT_SERVER_COMM | Client/Server communication mode, where the Server listens on a port and the Client initiates a connection. |

## FEATURE_SUPPORTED / FEATURE_NOT_SUPPORTED

Constants for the **value** output parameter of **GetCapability**.

```cpp
constexpr int32_t FEATURE_SUPPORTED = 1;
constexpr int32_t FEATURE_NOT_SUPPORTED = 0;
```
