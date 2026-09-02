/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HIXL_TOOL_LOCAL_COMM_RES_H_
#define HIXL_TOOL_LOCAL_COMM_RES_H_

#include <string>
#include <vector>

namespace hixl_tool {

/**
 * @brief local_comm_res subcommand entry
 * @param [in] args Argument list (args[0] is the subcommand name)
 * @return 0 on success, non-zero on failure
 */
int RunLocalCommRes(const std::vector<std::string> &args);

}  // namespace hixl_tool

#endif  // HIXL_TOOL_LOCAL_COMM_RES_H_
