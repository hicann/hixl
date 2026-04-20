#!/bin/bash
# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

export ASCEND_GLOBAL_EVENT_ENABLE=1
export ASCEND_HOST_LOG_FILE_NUM=500
# export HCCL_INTRA_PCIE_ENABLE=1

# export HCCL_INTRA_ROCE_ENABLE=1

# source /usr/local/Ascend/cann/set_env.sh
export MC_LOG_LEVEL=ERROR
export ASCEND_BUFFER_POOL=4:8 # BUFFER_NUM:BUFFER_SIZE (MB)
# export ASCEND_ENABLE_USE_FABRIC_MEM=1

python3 $@