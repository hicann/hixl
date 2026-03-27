# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

# Parse merged rank logs from fabric_mem_kv_benchmark and print aggregate lines.
# Expects machine-readable [RESULT] lines from the C++ binary.

/^\[RESULT\]/ && /get_rh2d_time_avg_us=/ && !/put_d2rh_time_avg_us=/ {
  b = ""; t_us = ""; g_bw = ""
  for (i = 1; i <= NF; i++) {
    if ($i ~ /^blocks=/) { split($i, a, "="); b = a[2] }
    if ($i ~ /^get_rh2d_time_avg_us=/) { split($i, a, "="); t_us = a[2] }
    if ($i ~ /^get_rh2d_bandwidth_GBps=/) { split($i, a, "="); g_bw = a[2] }
  }
  if (b != "" && t_us != "") {
    sum_us[b] += t_us
    sum_bw[b] += g_bw
    cnt[b]++
  }
}

/^\[RESULT\]/ && /rank=0/ && /put_d2rh_time_avg_us=/ {
  b = ""
  for (i = 1; i <= NF; i++) {
    if ($i ~ /^blocks=/) { split($i, a, "="); b = a[2] }
    if ($i ~ /^put_d2rh_time_avg_us=/) { split($i, a, "="); put_us[b] = a[2] }
    if ($i ~ /^put_d2rh_bandwidth_GBps=/) { split($i, a, "="); put_bw[b] = a[2] }
    if ($i ~ /^get_rh2d_time_max_avg_us=/) { split($i, a, "="); get_max_us[b] = a[2] }
    if ($i ~ /^get_rh2d_bandwidth_GBps=/) { split($i, a, "="); get_max_bw[b] = a[2] }
  }
}

END {
  n = 0
  for (b in cnt) {
    idx[++n] = b + 0
  }
  for (i = 1; i <= n; i++) {
    for (j = i + 1; j <= n; j++) {
      if (idx[j] < idx[i]) {
        tmp = idx[i]
        idx[i] = idx[j]
        idx[j] = tmp
      }
    }
  }
  for (i = 1; i <= n; i++) {
    b = idx[i]
    if (cnt[b] > 0) {
      printf "[AVG] blocks=%s n_ranks=%d get_rh2d_time_avg_us_mean=%.6f get_rh2d_bandwidth_GBps_mean=%.6f\n",
        b, cnt[b], sum_us[b] / cnt[b], sum_bw[b] / cnt[b]
    }
  }
  for (i = 1; i <= n; i++) {
    b = idx[i]
    if (b in put_us) {
      printf "[RANK0] blocks=%s put_d2rh_time_avg_us=%s put_d2rh_bandwidth_GBps=%s get_rh2d_time_max_avg_us=%s get_rh2d_bandwidth_GBps=%s\n",
        b, put_us[b], put_bw[b], get_max_us[b], get_max_bw[b]
    }
  }
}
