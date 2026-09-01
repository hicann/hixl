/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_BENCHMARK_LOG_H
#define HIXL_BENCHMARK_LOG_H

#include <cstdio>

#define BENCH_LOGE(fmt, ...) fprintf(stderr, "[ERROR] " fmt, ##__VA_ARGS__)
#define BENCH_LOGW(fmt, ...) fprintf(stderr, "[WARN] " fmt, ##__VA_ARGS__)
#define BENCH_LOGI(fmt, ...) fprintf(stdout, "[INFO] " fmt, ##__VA_ARGS__)
#define BENCH_LOGT(fmt, ...) fprintf(stdout, "[TRACE] " fmt, ##__VA_ARGS__)

#endif  // HIXL_BENCHMARK_LOG_H
