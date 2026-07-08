# HIXL Logging and Documentation Specifications

>  **Applicable Scope**: These specifications apply to log printing in all code within the HIXL repository, as well as the review of all Markdown documents in the repository. When a PR modifies `.md` files or log output, these specifications must be enforced.

## Rule List

| Rule No. | Rule Name | Category | Severity |
|---------|---------|------|---------|
| 1.1 | Failures must print an error description and key information | Logging | High |
| 1.2 | Failed calls to external components must print the API name/error code/parameters | Logging | Medium |
| 1.3 | Log content must use English exclusively | Logging | High |
| 1.4 | Content must be free of grammar and spelling errors, complete and accurate, and avoid self-invented abbreviations | Logging | Medium |
| 1.5 | Measurement information must include units | Logging | Medium |
| 1.6 | Sensitive information must not be logged in plaintext | Logging | High |
| 1.7 | Logs must not contain personal information | Logging | High |
| 1.8 | Performance-sensitive flows must not log run-level logs; production environments must not continuously output DEBUG | Logging | Medium |
| 1.9 | Avoid printing duplicate error logs within high-frequency loops | Logging | Medium |
| 1.10 | A single log line must not exceed 1024 characters | Logging | Medium |
| 2.1 | Document tables and list items must be complete item by item | Documentation Writing | Medium |

## Description

The logging portion of these specifications (Chapter 1) is based on the GitCode HIXL repository Wiki "Logging Content Printing Specification" and consolidated from existing logging check items in the HIXL PR review process. The documentation writing specifications (Chapter 2) directly follow the GitCode CANN community "Document Writing Specifications" and are not repeated in this file.

When developers contributing to HIXL submit log- or documentation-related changes, they must first follow these specifications; all other aspects follow the general CANN coding specifications. If there is an objection to a rule, it is recommended to submit an issue with the rationale. After review and approval by the CANN operations team, the change may be accepted and take effect.

## Scope of Application

Review of log printing, Markdown documents, READMEs, examples, and script descriptions in HIXL-related open-source repositories.

---

### 1. Logging Specifications

#### Rule 1.1 Failures must print an error description and key information

When an execution error or failure occurs, the log must provide an error description and print the key information that may have caused the failure. It must not silently return a failure.

[Counter-example]: No log or key information printed

```cpp
auto ret = IpToInt(ip_info.ip.GetString(), llm_ip_info.ip);
if (ret != SUCCESS) {
  return FAILED;
}
```

[Correct example]:

```cpp
auto ret = IpToInt(ip_info.ip.GetString(), llm_ip_info.ip);
if (ret != SUCCESS) {
  LOGE(FAILED, "Failed to transfer ip to int, please check ip:%s is valid.", ip_info.ip.GetString());
  return FAILED;
}
```

#### Rule 1.2 Failed calls to external components must print the API name/error code/parameters

When a call to an external component interface fails, the called API name, failure error code, and parameters must be printed to facilitate problem locating. Refer to the format `Call api:xxx failed, ret:xxx, param:xxx.`.

[Counter-example]: Function name and input parameters not printed

```cpp
auto ret = aclrtSetDevice(device_id);
if (ret != ACL_ERROR_NONE) {
  LOGE(FAILED, "Failed to set device.");
  return FAILED;
}
```

[Correct example]:

```cpp
auto ret = aclrtSetDevice(device_id);
if (ret != ACL_ERROR_NONE) {
  LOGE(FAILED, "Call api:aclrtSetDevice failed, ret:%u, device_id:xxx.");
  return FAILED;
}
```

#### Rule 1.3 Log content must use English exclusively

All log content must be described in English. Pinyin, Chinese, and Chinese punctuation must not be used.

#### Rule 1.4 Content must be free of grammar and spelling errors, complete and accurate, and avoid self-invented abbreviations

Log content must not contain grammar or spelling errors. The expression should be complete, accurate, and concise, and avoid self-invented abbreviations.

#### Rule 1.5 Measurement information must include units

Measurement information involved in logs (such as elapsed time, data volume, and bandwidth) must include units.

#### Rule 1.6 Sensitive information must not be logged in plaintext

Sensitive information, such as passwords, keys, and tokens, must not be recorded in plaintext.

#### Rule 1.7 Logs must not contain personal information

Log content must not contain personal information.

#### Rule 1.8 Performance-sensitive flows must not log run-level logs; production environments must not continuously output DEBUG

Performance-sensitive flows must not log run-level (INFO-level) logs, for example, the HIXL data-plane interface in the KV transfer flow; production environments must not have continuous DEBUG-level output.

#### Rule 1.9 Avoid printing duplicate error logs within high-frequency loops

Avoid printing duplicate error logs within high-frequency loop bodies; otherwise, useful error information may be obscured.

#### Rule 1.10 A single log line must not exceed 1024 characters

The length of a single log line must not exceed 1024 characters.

---

### 2. Documentation Writing Specifications

The documentation (`.md`) writing specifications directly follow the GitCode CANN community ["Document Writing Specifications"](https://gitcode.com/cann/community/blob/master/contributor/docs/document_writing_specs.md). These specifications cover all documentation writing requirements including file naming, headings, font styles, images, code blocks, lists, links, anchors, tables, punctuation, and more (including rules such as "no spaces between numbers/units/Chinese and English, with product names as exceptions" and "Chinese documents use full-width punctuation, numbers use half-width"). When reviewing documentation changes, use this as the primary reference (the community specifications are not repeated here). HIXL adds the following check item:

#### Rule 2.1 Document tables and list items must be complete item by item

In document tables and lists, every item must have corresponding descriptive content. Empty cells or missing items are not allowed.
