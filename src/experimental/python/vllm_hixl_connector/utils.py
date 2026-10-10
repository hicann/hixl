# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""Standalone helpers: ZMQ send/recv, block-id grouping, PP layer indices."""

import contextlib
import hashlib
import struct
import time
from collections.abc import Iterator
from typing import Any
import numpy as np
import numpy.typing as npt
import zmq
from vllm.distributed.utils import get_pp_indices
from vllm.logger import logger
from vllm.utils.network_utils import make_zmq_socket


@contextlib.contextmanager
def zmq_ctx(socket_type: Any, addr: str) -> Iterator[zmq.Socket]:  # type: ignore
    """Context manager for a ZMQ socket"""

    if socket_type not in (zmq.ROUTER, zmq.REQ, zmq.DEALER):  # type: ignore
        raise ValueError(f"Unexpected socket type: {socket_type}")

    ctx: zmq.Context | None = None  # type: ignore
    try:
        ctx = zmq.Context()  # type: ignore
        yield make_zmq_socket(
            ctx=ctx, path=addr, socket_type=socket_type, bind=socket_type == zmq.ROUTER
        )  # type: ignore
    finally:
        if ctx is not None:
            ctx.destroy(linger=0)


def group_concurrent_contiguous(
    remote_ids: list[int],
    local_ids: list[int],
    remote_block_stride: int = 1,
    local_block_stride: int = 1,
    block_len: int = 1,
) -> tuple[list[list[int]], list[list[int]]]:
    """Pair-wise grouping of remote (READ source) and local (READ
    destination) block ids that are contiguous in both id space and
    memory."""
    remote_indices: npt.NDArray[np.int64] = np.array(remote_ids, dtype=np.int64)
    local_indices: npt.NDArray[np.int64] = np.array(local_ids, dtype=np.int64)

    if remote_indices.size == 0:
        return [], []

    remote_byte_contiguous = np.diff(remote_indices) * remote_block_stride == block_len
    local_byte_contiguous = np.diff(local_indices) * local_block_stride == block_len
    brk = np.where(~(remote_byte_contiguous & local_byte_contiguous))[0] + 1
    remote_groups = np.split(remote_indices, brk)
    local_groups = np.split(local_indices, brk)

    remote_groups = [g.tolist() for g in remote_groups]
    local_groups = [g.tolist() for g in local_groups]

    return remote_groups, local_groups


def split_if_not_byte_contiguous(
    remote_groups: list[list[int]],
    local_groups: list[list[int]],
    remote_block_stride: int,
    local_block_stride: int,
    block_len: int,
) -> tuple[list[list[int]], list[list[int]]]:
    if remote_block_stride == block_len and local_block_stride == block_len:
        return remote_groups, local_groups

    remote_ids = [bid for group in remote_groups for bid in group]
    local_ids = [bid for group in local_groups for bid in group]
    return group_concurrent_contiguous(
        remote_ids,
        local_ids,
        remote_block_stride=remote_block_stride,
        local_block_stride=local_block_stride,
        block_len=block_len,
    )


def string_to_int64_hash(input_str):
    """
    Hash the string using SHA-256 and convert it into an int64 integer.
    """
    hashed_bytes = hashlib.sha256(input_str.encode("utf-8")).digest()
    truncated_bytes = hashed_bytes[:8]
    uint64_value = struct.unpack("<Q", truncated_bytes)[0]
    return uint64_value


def ensure_zmq_send(
    socket: zmq.Socket,  # type: ignore
    data: bytes,
    path: str,
    max_retries: int = 3,
):
    retries_left = max_retries
    while True:
        try:
            socket.send(data)
            return
        except zmq.ZMQError as e:  # type: ignore
            retries_left -= 1
            if retries_left > 0:
                logger.warning(
                    "Send failed. error=%s, attempts_left=%d.", e, retries_left
                )
                time.sleep(0.1)
            else:
                logger.error("Send failed after all retries. error=%s.", e)
                raise RuntimeError(
                    f"Failed to send data to {path} after {max_retries} retries: {e}"
                )


def ensure_zmq_recv(
    socket: zmq.Socket,  # type: ignore
    path: str,
    max_retries: int = 3,
) -> bytes:
    retries_left = max_retries
    while True:
        try:
            return socket.recv()
        except zmq.ZMQError as e:  # type: ignore
            retries_left -= 1
            if retries_left > 0:
                logger.warning(
                    "Receive failed. error=%s, attempts_left=%d.", e, retries_left
                )
                time.sleep(0.1)
            else:
                logger.error(
                    "Receive failed after all retries. source=%s, error=%s.", path, e
                )
                raise RuntimeError(
                    f"Failed to receive data after {max_retries} retries: {e}"
                )


# decode node should know pp_partition_layer in prefill node,
# it is configured in kv_transfer_config by partition_list_str,
# default using vllm layer split algorithm.
def get_prefill_pp_indices(
    num_hidden_layers: int,
    pp_rank: int,
    pp_size: int,
    partition_list_str: str | None = None,
) -> tuple[int, int]:
    if partition_list_str is None:
        return get_pp_indices(num_hidden_layers, pp_rank, pp_size)
    else:
        try:
            partitions = [int(layer) for layer in partition_list_str.split(",")]
        except ValueError as err:
            raise ValueError(f"Invalid partition string: {partition_list_str}") from err
        if len(partitions) != pp_size:
            raise ValueError(f"{len(partitions)=} does not match {pp_size=}.")
        if sum(partitions) != num_hidden_layers:
            raise ValueError(f"{sum(partitions)=} does not match {num_hidden_layers=}.")
        start_layer = sum(partitions[:pp_rank])
        end_layer = start_layer + partitions[pp_rank]
        return (start_layer, end_layer)
