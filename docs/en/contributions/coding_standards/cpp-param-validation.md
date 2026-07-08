# Untrusted Input Parameter Validation Specifications

>  **Applicable Scope**: When function parameters come from untrusted modules (such as device kernel entry points, `extern "C"` exported functions, Python bindings, cross-process RPC callbacks, etc.), validation must be completed before the parameters are dereferenced or used, to prevent crashes, arbitrary address read/write, or resource exhaustion caused by illegal input.

## Core Principle

**Report issues that can lead to illegal dereferencing, illegal memory access, undefined behavior, or OOM/DoS.** The following cases are not considered security issues and should not be reported:

- The parameter is only passed through to a lower-level function and is not dereferenced in this layer
- The lower-level function already validates before use (repeated validation at the trust boundary entry layer is not required)
- A pointer + length combination guaranteed by the caller per the API contract (such as the C API's `desc_list` + `list_num`)

## Trust Boundary Identification

The following function signature patterns are considered trust boundary entry points:

| Entry Type | Identification Feature | Example |
|---------|---------|------|
| Device kernel entry | `extern "C"` + kernel function name | `HixlBatchPut(HixlOneSideOpParam *param)` |
| Public C API | `extern "C"` or header declaration under `include/` | `HixlCSServerCreate(...)` |
| Python binding | `pybind11` module def | `m.def("batch_put", ...)` |
| Cross-process callback | RPC/message processing function | `OnMessage(...)` |

## Validation Checklist

For each trust boundary entry function, check whether parameters are validated **before being dereferenced or used**. Validation does not have to occur at the entry layer, but it must be completed before the first dereference.

| No. | Check Item | Validation Requirement | Consequence of Violation |
|------|--------|---------|---------|
| P1 | Null check before pointer dereference | A pointer must be checked for null before dereferencing (`*p`, `p->`, `std::string(p)`, `strlen(p)`, etc.) | Null pointer dereference causes crash |
| P2 | Address field non-zero before dereference | Address fields stored as integers in a struct (such as `xxx_addr`) must be validated as non-0 before dereferencing (`reinterpret_cast` itself does not dereference; a null check after the cast is also acceptable) | Zero-address dereference causes crash or illegal access |
| P3 | Quantity/length range | Integer fields representing quantity, length, or size must be validated as `> 0` and `<= upper limit`; not required if only used for loop iteration and the caller guarantees the array length | A 0 value causes an empty loop or OOM; an extremely large value causes integer overflow or OOM |
| P4 | Block execution on validation failure | Validation failure must immediately return an error code and must not continue executing subsequent logic | Illegal parameters passed through to the lower layer trigger more severe issues |
| P5 | Do not dereference parameters before validation | No dereference of parameters may occur before validation is complete | Illegal access before validation has already caused a crash |
| P6 | Avoid undefined behavior | C++ standard undefined behaviors such as taking the address of an empty container (`&vec[0]`) or taking a reference to a null pointer must be avoided | Undefined behavior may cause crashes or unpredictable results |

## Non-Issues (Exclusions)

The following cases **should not be reported** as security issues:

| Exclusion | Description | Example |
|--------|------|------|
| Pass-through without dereference | The parameter is only passed to a lower-level function and is not dereferenced in this layer | `mem_tag` is passed through to `HcommMemReg` and is not dereferenced in HIXL code |
| Lower layer already validates | The lower-level function validates before first use; repeated validation at the entry layer is not required | `endpoint_list` is not null-checked at the C API layer, but `Initialize` internally uses `CHECK_NOTNULL` |
| Caller-guaranteed pointer + length | In the C API calling convention, the caller guarantees the pointer points to at least N elements | `desc_list` + `list_num`: the caller guarantees `desc_list` has at least `list_num` elements |
| Integer overflow without illegal access | Integer arithmetic overflows but the result is not used for dereferencing, memory access, or resource allocation | A counter overflows but is only used for log printing |

## Inspection Method

### 1. Locate Trust Boundary Entry Points

Search for the following patterns in the changed files:

```
extern "C"
PYBIND11_MODULE
m\.def\(
```

As well as public functions declared in headers under the `include/` directory.

### 2. Check Validation Completeness Function by Function

For each entry function:

1. List all input parameter fields (including struct members)
2. For each field, trace to the location of **first dereference or use**
3. Check whether validation is completed before the first dereference (can be in this layer or a lower layer)
4. Compare against exclusions to confirm it is not a false positive
5. Check whether the validation failure path immediately returns an error code

## Correct Example

```cpp
uint32_t HixlBatchTransfer(bool is_read, HixlOneSideOpParam *param) {
  // P1: Null check on pointer (param is about to be dereferenced)
  if (param == nullptr) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] param is nullptr");
    return PARAM_INVALID;
  }
  // P3: list_num needs an upper limit check, because list_num is subsequently used to construct a vector and access the array pointed to by op_desc_list_addr
  constexpr uint32_t kMaxBatchSize = 8192;
  if (param->list_num == 0 || param->list_num > kMaxBatchSize) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] invalid list_num=%u, valid range is [1, %u]",
              param->list_num, kMaxBatchSize);
    return PARAM_INVALID;
  }
  // P2: Address field non-zero (about to reinterpret_cast and access)
  if (param->op_desc_list_addr == 0) {
    HIXL_LOGE(PARAM_INVALID, "[HixlBatchPutAndGet] op_desc_list_addr is null");
    return PARAM_INVALID;
  }
  // P5: Parameters are only used after validation is complete
  auto *op_list = reinterpret_cast<HixlOneSideOpDesc *>(
      static_cast<uintptr_t>(param->op_desc_list_addr));
  ...
}
```

## Incorrect Example

```cpp
// Incorrect: server_ip is not null-checked; the std::string constructor internally calls strlen which dereferences nullptr
HixlCSServer(const char *ip, uint32_t port) : ip_(ip), port_(port) {};
// Call site:
auto server = new HixlCSServer(server_desc->server_ip, server_desc->server_port);
// When server_ip is nullptr, std::string(nullptr) crashes
```

## Non-Issue Examples (Should Not Be Reported)

```cpp
// Not an issue: mem_tag is only passed through and not dereferenced in this layer
HIXL_CHK_STATUS_RET(server->RegisterMem(mem_tag, mem, mem_handle), ...);

// Not an issue: endpoint_list is null-checked inside Initialize
HIXL_CHK_STATUS_RET(server->Initialize(server_desc->endpoint_list, ...), ...);
// Inside Initialize: HIXL_CHECK_NOTNULL(endpoint_list);

// Not an issue: the caller guarantees desc_list has at least list_num elements
for (uint32_t i = 0; i < list_num; i++) {
    void *dst = desc_list[i].local_buf;  // Guaranteed by the caller, not the library's responsibility
}
```
