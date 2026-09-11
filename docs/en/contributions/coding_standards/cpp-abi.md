# CANN C++ ABI Compatibility Coding Specifications

>  **Applicable Scope**: These specifications apply to the interface design of all public headers under `include/` (`hixl`, `adxl`, `cs`, `llm_datadist`) and the evolution management of exported symbols of `libcann_hixl.so` and `libllm_datadist.so`. When a PR involves files under `include/` or changes to public interface implementations, these specifications must be followed.

## Rule List

| Rule No. | Rule Name | Category | Severity |
|---------|---------|------|---------|
| 1.1 | Understand what constitutes an ABI change | General Principles | —— |
| 1.2 | ABI changes must be explicitly reviewed | General Principles | High |
| 2.1 | No std::string/std::list in public headers | STL Type Admission | High |
| 2.2 | No template instantiation involving dual-ABI types | STL Type Admission | High |
| 2.3 | Use AscendString or C-style strings for string parameters | STL Type Admission | Medium |
| 2.4 | Public headers must stay self-contained | STL Type Admission | Medium |
| 3.1 | New fields must be appended at the end of the struct | Data Layout | High |
| 3.2 | New fields should preferably reuse reserved space | Data Layout | High |
| 3.3 | New external structs must reserve a reserved field | Data Layout | Medium |
| 3.4 | Never modify the types or order of existing fields | Data Layout | High |
| 3.5 | Enum values must be explicitly assigned and appended at the end only | Data Layout | High |
| 4.1 | Public symbols are append-only | Symbol Evolution | High |
| 4.2 | Never change public constexpr constant values | Symbol Evolution | High |
| 4.3 | Changing a default parameter value counts as a signature change | Symbol Evolution | High |
| 5.1 | New virtual functions in public classes must be appended at the end | Class Layout | High |
| 5.2 | Prefer the pimpl idiom for public classes | Class Layout | Medium |
| 6.1 | Keep the _GLIBCXX_USE_CXX11_ABI setting consistent | Build Convention | Medium |

## Description

These specifications are based on the GCC libstdc++ dual-ABI behavior (`_GLIBCXX_USE_CXX11_ABI`), CANN package build settings, and the current state of HIXL public interfaces. They complement rules 10.11 and 10.12 of the [CANN C++ Secure Coding Specifications](cpp-secure.md): this file is the authoritative source of rules on ABI compatibility. If you disagree with a rule, submit an issue with your reasoning; the rule may be accepted and modified after review by the CANN operations team.

## Applicable Scope

Public header design, public interface evolution (C++ class interfaces and C interfaces), and exported symbol management in HIXL-related open source repositories.

---

### 1. General Principles

#### Rule 1.1 Understand what constitutes an ABI change

API compatibility is source-level compatibility (old code still compiles after recompilation); ABI compatibility is binary-level compatibility (old build artifacts still link and run correctly against a new `.so` without recompilation). Any of the following changes constitutes an ABI change. Check them one by one before modifying `include/` or public interface implementations:

- Deletion or renaming of exported symbols (C++ mangled names encode the full signature; changing the signature changes the symbol)
- Changes to the size, field offsets, or alignment of external structs
- Changes to enum values
- Changes to public `constexpr` constant values (compiled into callers)
- Changes to the vtable layout of a class (adding/removing virtual functions, reordering)
- Changes to function default argument values (expanded at call sites)

#### Rule 1.2 ABI changes must be explicitly reviewed

Avoid changing the public ABI unless necessary. When a change is required, the PR description must explicitly state the ABI impact, the compatibility conclusion, and the version evolution plan, and the change may be merged only after review and confirmation. The basis for judgment is the change checklist in Rule 1.1, not "whether header files were modified" — some ABI changes (such as adding or removing exported symbols) do not touch `include/`.

---

### 2. STL Type Admission in Public Headers

#### Rule 2.1 No std::string/std::list in public headers

After GCC 5.1 introduced the `_GLIBCXX_USE_CXX11_ABI` dual ABI, both the layout and the symbol names of `std::string` and `std::list` vary with this macro: `std::string` changed from a COW implementation (8-byte object) to an SSO implementation (32 bytes); the `std::list` node header gained a `size_t` size member (the object grew from 16 to 24 bytes); under the new ABI, the mangled names of both carry the `__cxx11` tag. When the caller and the library use different ABI macro settings, the result ranges from link-time failures to silent memory corruption caused by reading and writing the same object with different layouts — the most classic source of ABI breakage in public interfaces.

[Counter-example]:

```cpp
struct NotifyDesc {
  std::string name;         // Forbidden: dual-ABI type, layout varies with _GLIBCXX_USE_CXX11_ABI
  std::string notify_msg;
};
```

[Positive example]:

```cpp
struct NotifyDesc {
  AscendString name;        // Layout controlled by metadef, unaffected by the libstdc++ dual ABI
  AscendString notify_msg;
};
```

#### Rule 2.2 No template instantiation involving dual-ABI types

The toxicity of forbidden types propagates along template instantiation chains: combinations such as `std::vector<std::string>` and `std::map<std::string, AscendString>`, whose template arguments contain `std::string`/`std::list`, have element layouts and entire instantiation-chain symbol names that vary with the dual ABI, which is equivalent to directly introducing the forbidden types.

[Counter-example]:

```cpp
Status Initialize(const std::map<std::string, std::string> &options);
```

[Positive example]:

```cpp
Status Initialize(const std::map<AscendString, AscendString> &options);
```

#### Rule 2.3 Use AscendString or C-style strings for string parameters

String parameters of C++ public interfaces use `ge::AscendString`; C public interfaces use `const char *` together with an explicit length or a `NUL`-termination convention. `std::vector`, `std::map`, `std::pair`, and `std::unique_ptr` (for the pimpl idiom) carry no `__cxx11` tag and have stable layouts; they are allowed in public signatures, with the implication that the interface is bound to the libstdc++ standard library family.

#### Rule 2.4 Public headers must stay self-contained

Public headers may depend only on the C++ standard library and designated CANN headers (`ge_common`, `acl`, `hcomm`); they must not include repository-internal headers or implementation headers under `src/`, so that downstream users can compile against the public headers independently.

---

### 3. Data Layout

#### Rule 3.1 New fields must be appended at the end of the struct

Inserting a new field before or between existing fields shifts the offsets of all subsequent fields, breaking callers already compiled against the old layout. New fields can only be appended after the last field (in practice, the last field is the `reserved` field; see Rule 3.2).

[Counter-example]:

```cpp
struct MemDesc {
  uint32_t priority;        // Forbidden: inserted in the middle, offsets of addr/len/reserved all change
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};
};
```

[Positive example]:

```cpp
struct MemDesc {
  uintptr_t addr;
  size_t len;
  uint8_t reserved[128] = {};  // New fields must extend within the reserved area; see Rule 3.2
};
```

#### Rule 3.2 New fields should preferably reuse reserved space

The `reserved` field of an external struct is space reserved for compatible evolution. A new field should be placed before `reserved` (i.e., at the end of the effective fields), and the space it occupies — including any alignment padding it introduces — should be carved out of `reserved`, keeping the total struct size unchanged. An unchanged total size means that stack objects, arrays, and communication buffers allocated by callers under the old size will never overflow. Do not rely on implicit alignment padding: prefer field types and placements that introduce no padding (e.g., a 1-byte field adjacent to other 1-byte fields); when padding is unavoidable, calculate it explicitly and document it in a comment.

[Positive example]: `HixlClientDesc` keeps its total size at 128 bytes. A new `uint8_t` field placed right after `sl` (at offset 30) needs no alignment padding and occupies 1 byte, so `reserved` shrinks by 1:

```cpp
struct HixlClientDesc {
  const EndpointDesc *local_endpoint;
  const EndpointDesc *remote_endpoint;
  const char *server_ip;
  uint32_t server_port;
  uint8_t tc;
  uint8_t sl;
  uint8_t priority;          // New field: 1 byte, adjacent to sl at offset 30, no alignment padding
  uint8_t reserved[97];      // 98 - 1 (field), total size remains 128 bytes
};
```

#### Rule 3.3 New external structs must reserve a reserved field

New external structs must include a `reserved` field. The recommended reservation is 64–128 bytes (following the convention of existing public structs), preserving space for future evolution that does not break the layout.

#### Rule 3.4 Never modify the types or order of existing fields

Modifying the type or order of an existing field, or deleting a field, changes the struct size and field offsets — an ABI break. When evolution is required, follow Rules 3.1 and 3.2 to add new fields that carry the new semantics while keeping old fields unchanged.

#### Rule 3.5 Enum values must be explicitly assigned and appended at the end only

External enums must be explicitly assigned: implicit values drift as a whole when members are inserted or reordered. Existing enum values must never be modified (they are compiled into callers and leave no symbol trace); new enum values may only be appended at the end with explicit assignment. Prefer `enum class` with an explicit underlying type.

[Counter-example]:

```cpp
enum FeatureType : int32_t {
  AUTO_CONNECT = 0,
  NEW_FEATURE = 1,           // Forbidden: occupies an existing value, forcing CLIENT_SERVER_COMM to drift
  CLIENT_SERVER_COMM = 2,
};
```

[Positive example]:

```cpp
enum FeatureType : int32_t {
  AUTO_CONNECT = 0,
  CLIENT_SERVER_COMM = 1,
  NEW_FEATURE = 2,           // Appended at the end only, explicitly assigned
};
```

---

### 4. Symbol and Interface Evolution

#### Rule 4.1 Public symbols are append-only

Implementation symbols of public interfaces must not be deleted, renamed, or given changed signatures (parameter types, order, or count). C++ mangled names encode the full signature; even with the header updated in sync, renaming or changing the signature on the implementation side causes link failures for downstream artifacts already compiled. For C public interfaces (the `HixlCS*` series), the symbol name itself is the ABI and must not change. New capabilities are carried by new interfaces; deprecating an old interface follows a keep-and-mark deprecation process instead of direct removal.

#### Rule 4.2 Never change public constexpr constant values

Public `constexpr` constants in header files (status codes, feature values, etc.) are compiled directly into caller instructions. After a change, old and new binaries disagree on the same constant. Such changes alter no symbol or layout and are the most hidden category of ABI change; they must be treated the same as interface signature changes.

[Counter-example]:

```cpp
constexpr Status PARAM_INVALID = 103999U;  // Forbidden: changing 103900 to 103999 leaves already-compiled callers holding the old value
```

#### Rule 4.3 Changing a default parameter value counts as a signature change

Default arguments are expanded at call sites; changing a default value changes the behavior of every already-compiled call site that omits the argument, which is equivalent to changing the function signature. Changing `timeout_in_millis = 1000` to another value must be reviewed as an ABI change.

---

### 5. Class Layout

#### Rule 5.1 New virtual functions in public classes must be appended at the end

The vtable slot offset of a virtual function is part of the ABI. Inserting a virtual function in the middle shifts the slots of all subsequent virtual functions, misrouting every compiled virtual call. New virtual functions in a public class may only be appended after the existing virtual functions. The existing public classes (`hixl::Hixl`, `adxl::AdxlEngine`, `llm_datadist::LlmDataDist`) all have non-virtual interfaces; keeping this design avoids such constraints.

#### Rule 5.2 Prefer the pimpl idiom for public classes

A public class should hold only a `std::unique_ptr<Impl> impl_` member (8 bytes, stable layout), with all implementation details encapsulated in `Impl`: any evolution on the implementation side does not affect the size or layout of the public class. Destructors must be defined in implementation files (the complete-type requirement of `Impl`). The existing public classes all follow this pattern; new public classes must follow it as well.

---

### 6. Build Convention

#### Rule 6.1 Keep the _GLIBCXX_USE_CXX11_ABI setting consistent

CANN ecosystem prebuilt libraries are built with `_GLIBCXX_USE_CXX11_ABI=0`; HIXL host libraries receive the same value through the build settings injected by CANN packages, and downstream components (examples, benchmarks, hixl_tool) have explicitly pinned it to 0. Any build-side change to this macro setting can cause `__cxx11`-tagged symbol differences and mixed-linking failures, and must be reviewed as an ABI change. Combined with Rule 2.1, when public headers do not introduce `std::string`/`std::list`, the macro setting has no effect on the symbols and layouts of public interfaces.
