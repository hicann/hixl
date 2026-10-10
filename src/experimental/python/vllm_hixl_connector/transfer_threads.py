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

"""Background handshake threads: P-side ``KVCacheSendingThread`` (ZMQ ROUTER)
and D-side ``KVCacheRecvingThread`` (request queue, pull execution, reformat)."""

import logging
import queue
import threading
import time
from collections import defaultdict, deque
from concurrent.futures import ThreadPoolExecutor
from typing import Any
import msgspec
import torch
import torch_npu
import zmq
from vllm.config import VllmConfig
from vllm.distributed import get_pcp_group
from vllm.distributed.kv_transfer.kv_connector.utils import BlockIds
from vllm.distributed.parallel_state import (
    get_pp_group,
    get_tensor_model_parallel_world_size,
)
from vllm.logger import logger
from vllm.utils.network_utils import get_ip, make_zmq_path, make_zmq_socket
from vllm_ascend import envs as ascend_envs
from vllm_ascend.ascend_config import get_ascend_config
from vllm_ascend.utils import enable_custom_op, enable_sfa_dcp_replicated_indexer
from .constants import (
    ACK_SEND_MAX_RETRIES,
    DONE_RECVING_MSG,
    GET_META_MSG,
    MAX_REQUESTS_PER_PEER_HANDLER,
)
from .hixl_wrapper import _Hixl
from .metadata import GroupPull, HIXLAgentMetadata, RemotePortInfo, SizedDict
from .task_tracker import KVCacheTaskTracker
from .utils import (
    ensure_zmq_recv,
    ensure_zmq_send,
    get_prefill_pp_indices,
    group_concurrent_contiguous,
    split_if_not_byte_contiguous,
    zmq_ctx,
)


# Cap on how many address entries a DEBUG transfer log prints; the full
# lists can be thousands of entries for large prompts.
_MAX_DEBUG_ADDR_ENTRIES = 8


def _summarize_decoded_msg(msg: Any, max_chars: int = 128) -> str:
    """Log-injection-safe, truncated repr of a decoded control message."""
    text = repr(msg)
    return text[:max_chars] + "..." if len(text) > max_chars else text


def _summarize_frames(frames: list[bytes], max_bytes: int = 64) -> str:
    """One-line, log-injection-safe summary of raw ZMQ frames.

    Remote peers control frame bytes, so raw frames must never reach a log
    verbatim: newlines or terminal escapes would forge log lines. Print the
    length of every frame plus a short ``repr``-escaped preview instead.
    """
    return " ".join(
        f"frame[{i}] len={len(frame)} preview={repr(frame[:max_bytes])}"
        f"{'...' if len(frame) > max_bytes else ''}"
        for i, frame in enumerate(frames)
    )


def _capture_acl_runtime_context() -> Any:
    """Capture the calling thread's ACL context for executor-thread handoff.

    HIXL binds its engine session to the ACL context of the thread that calls
    Initialize/RegisterMem, while Connect/TransferAsync run on executor
    threads; ACL contexts are thread-local, so each executor thread must
    re-enter the same context before touching the engine. pyACL (``import acl``)
    may be absent in stub/test environments; return None then and fall back to
    plain device selection.
    """
    try:
        import acl  # type: ignore[import-not-found]
    except ImportError:
        return None
    try:
        ret, context = acl.rt.get_context()
    except (AttributeError, TypeError, ValueError) as e:
        logger.warning("Failed to get ACL runtime context. error=%s.", e)
        return None
    if ret != 0:
        logger.warning("Failed to get ACL runtime context, ret=%d.", ret)
        return None
    return context


def _apply_acl_runtime_context(context: Any) -> None:
    """Re-enter the captured ACL context on the calling (executor) thread."""
    if context is None:
        return
    import acl  # type: ignore[import-not-found]

    ret = acl.rt.set_context(context)
    if ret != 0:
        raise RuntimeError(f"Failed to set ACL runtime context, ret={ret}.")


def _init_executor_thread(device: Any, acl_context: Any) -> None:
    # NPU device selection is thread-local: executor workers do not inherit
    # the device selected by the model worker thread and would otherwise use
    # device 0 on their first NPU operation. The ACL context is thread-local
    # too; re-enter the engine's context before any HIXL call on this thread.
    torch.npu.set_device(device)
    _apply_acl_runtime_context(acl_context)


class KVCacheSendingThread(threading.Thread):
    def __init__(
        self,
        vllm_config: VllmConfig,
        tp_rank: int,
        prefill_tp_size: int,
        local_engine_id: str,
        side_channel_host: str,
        side_channel_port: int,
        metadata: HIXLAgentMetadata,
        ready_event: threading.Event,
        kv_caches: dict[str, Any],
        pcp_rank: int,
    ):
        super().__init__(daemon=True, name="KVCacheSendingThread")
        self.tp_rank = tp_rank
        self.prefill_tp_size = prefill_tp_size
        self.pp_rank = get_pp_group().rank_in_group
        self.pcp_size = get_pcp_group().world_size
        self.pp_size = vllm_config.parallel_config.pipeline_parallel_size
        self.tp_size = get_tensor_model_parallel_world_size()
        self.local_engine_id = local_engine_id
        self.side_channel_host = side_channel_host
        self.side_channel_port = side_channel_port
        self.metadata = metadata
        self.ready_event = ready_event
        self.kv_caches = kv_caches
        self.pcp_rank = pcp_rank
        self.port_send_num: dict[str, int] = {}

        self.task_tracker = KVCacheTaskTracker()
        self._stopped = threading.Event()

    def shutdown(self):
        """Ask the ROUTER loop to exit.

        Best effort: the thread is a daemon and also dies with the process if
        it is still busy when shutdown() is called.
        """
        self._stopped.set()

    def get_and_clear_finished_requests(self) -> set[str]:
        """
        Get and clear the requests that have been completed.
        Returns:
            A set of request IDs that have been completed.
        """
        return self.task_tracker.get_and_clear_finished_requests()

    def add_not_transfer_request(self, request_id: str):
        self.task_tracker.add_not_transfer_request(request_id)

    def add_delayed_request(self, request_id: str, delay_start_time: float):
        return self.task_tracker.add_delayed_request(request_id, delay_start_time)

    def run(self):
        """Run the thread to handle KV cache transfer requests."""
        try:
            # Listen for new requests for metadata. NOTE: we need each rank
            # to have a unique port. This hack keeps us moving for now. We
            # will switch when moving to etcd or where we have a single ZMQ
            # socket in the scheduler.
            device_index = (
                self.pp_rank * self.pcp_size + self.pcp_rank
            ) * self.tp_size + self.tp_rank
            handshake_port = self.side_channel_port + device_index
            path = make_zmq_path("tcp", self.side_channel_host, handshake_port)
            logger.info(
                "KVCacheSendingThread started listening on path: %s. Thread: tp_rank=%d, pp_rank=%d, pcp_rank=%d",
                path,
                self.tp_rank,
                self.pp_rank,
                self.pcp_rank,
            )
            with zmq_ctx(zmq.ROUTER, path) as sock:  # type: ignore
                # Wake up periodically so the loop can observe shutdown().
                sock.setsockopt(zmq.RCVTIMEO, 1000)  # type: ignore[attr-defined]
                self.ready_event.set()
                self.run_busy_loop(sock)
        except Exception as e:
            if self._stopped.is_set():
                return
            logger.exception(
                "HIXLConnector KVCacheSendingThread encountered exception. "
                "Thread: tp_rank=%d, pp_rank=%d, listening_path=%s. "
                "Error: %s",
                self.tp_rank,
                self.pp_rank,
                path,
                e,
            )

    def run_busy_loop(self, sock: zmq.Socket):  # type: ignore
        encoder = msgspec.msgpack.Encoder()
        encoded_data = encoder.encode(self.metadata)
        size_in_bytes = len(encoded_data)
        if logger.isEnabledFor(logging.DEBUG):
            logger.debug(
                "Size of encoded HIXLAgentMetadata: %s bytes", str(size_in_bytes)
            )

        decoder = msgspec.msgpack.Decoder(type=tuple)
        while not self._stopped.is_set():
            try:
                frames = sock.recv_multipart()
            except zmq.Again:  # type: ignore[attr-defined]
                # RCVTIMEO elapsed; loop back to re-check shutdown().
                continue
            try:
                if len(frames) < 2:
                    logger.error(
                        "Invalid message format in KVCacheSendingThread. "
                        "Expected: at least 2 frames (identity + payload). "
                        "Actual: %d frames. "
                        "Frames: %s. "
                        "Check: Verify message sender implementation.",
                        len(frames),
                        _summarize_frames(frames),
                    )
                    continue

                identity = frames[0]
                payload = [f for f in frames[1:] if f != b""]
                if len(payload) != 1:
                    logger.error(
                        "Invalid message format in KVCacheSendingThread. "
                        "Expected: exactly 1 payload frame. "
                        "Actual: %d payload frames. "
                        "Frames: %s. "
                        "Check: Verify message sender removes empty frames correctly.",
                        len(payload),
                        _summarize_frames(frames),
                    )
                    continue

                msg = decoder.decode(payload[0])
                if msg[0] == GET_META_MSG:
                    sock.send_multipart((identity, b"", encoded_data))
                elif msg[0] == DONE_RECVING_MSG:
                    logger.debug("Got DONE_RECVING_MSG for request %s", msg[1])
                    request_id = msg[1]
                    remote_port_send_num = msg[2]
                    if remote_port_send_num:
                        if request_id not in self.port_send_num:
                            self.port_send_num[request_id] = 0
                        self.port_send_num[request_id] += 1
                        device_index = (
                            self.pp_rank * self.pcp_size + self.pcp_rank
                        ) * self.tp_size + self.tp_rank
                        handshake_port = self.side_channel_port + device_index
                        # The count map comes from the remote side; a missing or
                        # malformed entry must not wedge the delayed free. Fall
                        # back to finishing on this DONE signal and log loudly.
                        port_info = remote_port_send_num.get(handshake_port) or {}
                        expected_done = port_info.get("num", 0)
                        if self.port_send_num[request_id] >= expected_done:
                            if not port_info:
                                logger.error(
                                    "DONE message carries no expected count for "
                                    "handshake port %d; finishing request %s on "
                                    "this signal alone.",
                                    handshake_port,
                                    request_id,
                                )
                            self.task_tracker.update_done_task_count(request_id)
                            del self.port_send_num[request_id]
                    else:
                        self.task_tracker.update_done_task_count(request_id)
                    # Acknowledge the request completion. Bounded retries: a
                    # vanished or busy peer must not stall this rank's whole
                    # ROUTER loop (GET_META / DONE would stop being served).
                    ack_sent = False
                    for _ in range(ACK_SEND_MAX_RETRIES):
                        try:
                            # Send ACK to the sender.
                            sock.send_multipart(
                                (identity, b"", b"ACK"), flags=zmq.NOBLOCK
                            )  # type: ignore
                            ack_sent = True
                            break
                        except zmq.Again:  # type: ignore
                            # If the socket is not ready, retry sending.
                            time.sleep(0.01)
                    if not ack_sent:
                        logger.error(
                            "Failed to send ACK after %d retries; giving up so the "
                            "control loop can continue. The decode side may retry "
                            "or rely on the delayed-free timeout. request_id=%s.",
                            ACK_SEND_MAX_RETRIES,
                            request_id,
                        )
                else:
                    logger.error(
                        "Connection listener received unexpected message type. "
                        "Expected: GET_META_MSG or DONE_RECVING_MSG. "
                        "Actual: %s. "
                        "Full message: %s. "
                        "Check: Verify message protocol implementation.",
                        msg[0] if msg else "empty",
                        _summarize_decoded_msg(msg),
                    )
            except Exception as e:
                logger.error(
                    "Connection listener encountered exception during message processing. "
                    "Exception type: %s. "
                    "Error: %s. "
                    "Context: Processing frames from socket. "
                    "Check: Review message handling logic and socket state.",
                    type(e).__name__,
                    e,
                )


class KVCacheRecvingThread(threading.Thread):
    def __init__(
        self,
        tp_rank: int,
        tp_size: int,
        _prefill_pp_size: int,
        engine: _Hixl,
        local_engine_id: str,
        local_handshake_port: int,
        side_channel_port: int,
        local_kv_caches_base_addr: list[list[int]],
        block_len_per_addr: list[list[int]],
        block_stride_per_addr: list[list[int]],
        is_hma_required=False,
        ready_event: threading.Event | None = None,
        vllm_config: VllmConfig | None = None,
        kv_caches: dict[str, Any] | None = None,
        prefill_pp_layer_partition: str | None = None,
        kv_group2layeridx: dict[int, tuple[dict[str, Any], list[int]]] | None = None,
        block_size_scale: list[list[int]] | None = None,
    ):
        super().__init__(daemon=True, name="KVCacheRecvingThread")
        self.tp_rank = tp_rank
        self.tp_size = tp_size
        self._prefill_pp_size = _prefill_pp_size
        self.local_engine_id = local_engine_id
        self.local_handshake_port = local_handshake_port
        self.side_channel_port = side_channel_port
        self.engine = engine
        if ready_event is None:
            ready_event = threading.Event()
        self.ready_event = ready_event

        if kv_caches is None:
            kv_caches = {}
        self.kv_caches = kv_caches
        self.kv_caches_base_addr: dict[str, dict[int, list[list[int]]]] = SizedDict()
        self.kv_caches_base_addr.get_or_create(local_engine_id)[
            local_handshake_port
        ] = local_kv_caches_base_addr
        self.block_len_per_addr = block_len_per_addr
        self.block_stride_per_addr = block_stride_per_addr
        if kv_group2layeridx is None:
            kv_group2layeridx = {}
        self.kv_group2layeridx = kv_group2layeridx
        self.group_compress_ratios: dict[int, int] = {}
        for group_id, (group_spec, _) in self.kv_group2layeridx.items():
            compress_ratio = 1
            kv_cache_spec = group_spec.get("kv_cache_spec")
            if isinstance(kv_cache_spec, dict):
                for spec in kv_cache_spec.values():
                    if isinstance(spec, dict) and isinstance(
                        spec.get("compress_ratio"), int
                    ):
                        compress_ratio = max(1, spec["compress_ratio"])
                        break
            self.group_compress_ratios[group_id] = compress_ratio
        self.remote_transfer_port: dict[str, dict[int, int]] = SizedDict()
        self.remote_block_size_scale: dict[str, dict[int, list[list[int]]]] = (
            SizedDict()
        )
        self.remote_block_stride_per_addr: dict[str, dict[int, list[list[int]]]] = (
            SizedDict()
        )
        self.remote_kv_group2layeridx: dict[
            str, dict[int, dict[int, tuple[dict[str, Any], list[int]]]]
        ] = SizedDict()
        self.remote_metadata_lock = threading.Lock()
        # Reformat metadata keyed by request_id then CP shard index. Populated by the
        # last TP-offset pull task for each shard; applied once all pull tasks finish.
        self.pending_reformat: defaultdict[
            str, dict[int, list[tuple[int, list[list[int]], int, list[int]]]]
        ] = defaultdict(dict)
        self.pending_reformat_lock = threading.Lock()

        self.request_queue: queue.Queue[Any] = queue.Queue()
        first_kv_cache = next(iter(self.kv_caches.values()), None)
        # ACL contexts are thread-local; capture the engine thread's context so
        # executor workers can re-enter it before calling into HIXL.
        self._acl_context = _capture_acl_runtime_context()
        if first_kv_cache is None:
            self.executor = ThreadPoolExecutor(max_workers=32)
        else:
            kv_cache_device = first_kv_cache[0].device
            self.executor = ThreadPoolExecutor(
                max_workers=32,
                initializer=_init_executor_thread,
                initargs=(kv_cache_device, self._acl_context),
            )
        self.peer_request_queues: defaultdict[
            tuple[str, int], deque[dict[str, Any]]
        ] = defaultdict(deque)
        self.active_peer_request_handlers: set[tuple[str, int]] = set()
        self.peer_request_queues_lock = threading.Lock()
        self.request_task_counts: defaultdict[str, int] = defaultdict(int)
        self.finished_request_markers: set[str] = set()
        self.request_task_counts_lock = threading.Lock()

        self.task_tracker = KVCacheTaskTracker()

        self.encoder = msgspec.msgpack.Encoder()
        self.decoder = msgspec.msgpack.Decoder(HIXLAgentMetadata)
        self.remote_sockets_lock = threading.Lock()
        self.remote_sockets: dict[  # type: ignore
            str, deque[zmq.Socket]
        ] = defaultdict(  # type: ignore
            deque
        )
        # Shared ZMQ context for the REQ socket pool: created lazily, closed
        # together with the pool in shutdown() instead of leaking one context
        # (and its IO thread) per socket for the life of the process.
        self._remote_zmq_ctx: zmq.Context | None = None  # type: ignore[valid-type]
        self._stopped = threading.Event()
        self.timeout = 1.0  # seconds

        if vllm_config is None:
            raise ValueError("KVCacheRecvingThread requires a non-None vllm_config.")
        self.vllm_config: VllmConfig = vllm_config
        self.model_config = self.vllm_config.model_config
        self.num_speculative_tokens = (
            self.vllm_config.speculative_config.num_speculative_tokens
            if self.vllm_config.speculative_config is not None
            else 0
        )
        self.use_mla = self.model_config.is_deepseek_mla
        self.enable_sfa_dcp_replicated_indexer = enable_sfa_dcp_replicated_indexer(
            self.vllm_config
        )
        self.is_hma_required = is_hma_required
        self.block_size = self.vllm_config.cache_config.block_size
        try:
            hf_text_config = self.model_config.hf_text_config
            if hf_text_config is None:
                raise AttributeError
        except AttributeError:
            hf_text_config = self.model_config.hf_config
        self.num_layers = hf_text_config.num_hidden_layers
        if block_size_scale is None:
            block_size_scale = []
        self.block_size_scale = block_size_scale
        self.pp_layer_indices = {
            rank: get_prefill_pp_indices(
                self.num_layers, rank, self._prefill_pp_size, prefill_pp_layer_partition
            )
            for rank in range(self._prefill_pp_size)
        }
        self.proc_not_transfer_request: dict[str, bool] = {}
        self.proc_not_transfer_request_lock = threading.Lock()
        self.failed_recv_requests: set[str] = set()
        self.invalid_block_ids: set[int] = set()
        self.failed_recv_requests_lock = threading.Lock()

        self.num_draft_layers = 0
        if self.vllm_config.speculative_config is not None:
            if self.vllm_config.speculative_config.method == "mtp":
                # all MTP layer use the same kv cache layer, so only need to transfer once
                self.num_draft_layers = 1
            elif (
                hasattr(
                    self.vllm_config.speculative_config.draft_model_config, "hf_config"
                )
                and getattr(
                    self.vllm_config.speculative_config.draft_model_config.hf_config,
                    "num_hidden_layers",
                    None,
                )
                is not None
            ):
                self.num_draft_layers = self.vllm_config.speculative_config.draft_model_config.hf_config.num_hidden_layers

    def add_request(
        self,
        request_id: str,
        remote_request_id: str,
        local_block_ids: BlockIds,
        remote_block_ids: BlockIds,
        group_pulls: list[GroupPull],
        remote_engine_id: str,
        remote_host: str,
        remote_handshake_port: int,
        remote_block_size=None,
        remote_port_send_num: dict[int, RemotePortInfo] | None = None,
        num_computed_tokens: int = 0,
        all_task_done: bool = False,
        shard_idx: int = 0,
        local_block_ids_replicate_k: BlockIds | None = None,
        remote_block_ids_replicate_k: BlockIds | None = None,
    ):
        """Add a new request to the queue for processing."""
        if remote_port_send_num is None:
            remote_port_send_num = {}
        trans_info = {
            "request_id": request_id,
            "local_block_ids": local_block_ids,
            "remote_block_ids": remote_block_ids,
            "local_block_ids_replicate_k": local_block_ids_replicate_k or tuple(),
            "remote_block_ids_replicate_k": remote_block_ids_replicate_k or tuple(),
            "group_pulls": group_pulls,
            "remote_engine_id": remote_engine_id,
            "remote_request_id": remote_request_id,
            "remote_host": remote_host,
            "remote_handshake_port": remote_handshake_port,
            "num_computed_tokens": num_computed_tokens,
            "remote_port_send_num": remote_port_send_num,
            "all_task_done": all_task_done,
            "shard_idx": shard_idx,
            "remote_block_size": remote_block_size,
        }
        logger.debug(
            "Adding request %s to the queue.Trans info:%s", request_id, trans_info
        )
        self.request_queue.put(trans_info)

    def get_and_clear_finished_requests(self) -> set[str]:
        """
        Get and clear the requests that have been completed.
        Returns:
            A set of request IDs that have been completed.
        """
        return self.task_tracker.get_and_clear_finished_requests()

    def get_and_clear_invalid_block_ids(self) -> set[int]:
        """Get and clear block ids that failed to load."""
        with self.failed_recv_requests_lock:
            invalid_block_ids = self.invalid_block_ids
            self.invalid_block_ids = set()
        return invalid_block_ids

    def _is_failed_recv_request(self, request_id: str) -> bool:
        with self.failed_recv_requests_lock:
            return request_id in self.failed_recv_requests

    def _mark_failed_recv_request(
        self, request_id: str, local_block_ids: BlockIds
    ) -> None:
        with self.failed_recv_requests_lock:
            self.failed_recv_requests.add(request_id)
            # local_block_ids is grouped per KV cache group; report failed
            # blocks from every group (not just the first one) so partially
            # loaded requests are never treated as cache hits.
            for group_block_ids in local_block_ids:
                self.invalid_block_ids.update(group_block_ids)

    def _clear_failed_recv_request(self, request_id: str) -> None:
        with self.failed_recv_requests_lock:
            self.failed_recv_requests.discard(request_id)

    def run(self):
        """Run the thread to handle KV cache transfer requests."""
        self.ready_event.set()
        while not self._stopped.is_set():
            try:
                request_data = self.request_queue.get(timeout=1.0)
                if request_data is None:
                    logger.warning("Received a None request.")
                    self.request_queue.task_done()
                    continue
                self._submit_request(request_data)
            except queue.Empty:
                continue
            except Exception as e:
                logger.error("Error in KVCacheTransferThread. error=%s.", e)

    def shutdown(self):
        """Close the transfer lifecycle: stop dispatching, drain in-flight
        pulls, then close the ZMQ socket pool.

        ``executor.shutdown(wait=True)`` blocks until running pulls reach a
        terminal state (bounded by the transfer timeouts), so the engine can
        be deregistered and finalized afterwards without racing an in-flight
        DMA into freed memory.
        """
        self._stopped.set()
        self.executor.shutdown(wait=True)
        self._close_remote_sockets()

    def _submit_request(self, request_data: dict[str, Any]) -> None:
        peer_key = (request_data["remote_host"], request_data["remote_handshake_port"])
        self._mark_request_task_submitted(request_data)
        should_start_worker = False
        with self.peer_request_queues_lock:
            self.peer_request_queues[peer_key].append(request_data)
            if peer_key not in self.active_peer_request_handlers:
                self.active_peer_request_handlers.add(peer_key)
                should_start_worker = True

        if should_start_worker:
            self.executor.submit(self._handle_peer_requests, peer_key)

    def _handle_peer_requests(self, peer_key: tuple[str, int]) -> None:
        requests_handled = 0
        while requests_handled < MAX_REQUESTS_PER_PEER_HANDLER:
            with self.peer_request_queues_lock:
                peer_queue = self.peer_request_queues.get(peer_key)
                if not peer_queue:
                    self.peer_request_queues.pop(peer_key, None)
                    self.active_peer_request_handlers.discard(peer_key)
                    return
                req_meta = peer_queue.popleft()

            requests_handled += 1
            try:
                self._handle_request(req_meta)
            except Exception:
                logger.exception(
                    "Error handling KV cache transfer request for peer %s:%d.",
                    peer_key[0],
                    peer_key[1],
                )

        should_resubmit = False
        with self.peer_request_queues_lock:
            peer_queue = self.peer_request_queues.get(peer_key)
            if peer_queue:
                should_resubmit = True
            else:
                self.peer_request_queues.pop(peer_key, None)
                self.active_peer_request_handlers.discard(peer_key)

        if should_resubmit:
            try:
                self.executor.submit(self._handle_peer_requests, peer_key)
            except RuntimeError:
                # The executor was shut down; the drain loop stops here.
                pass

    def _mark_request_task_submitted(self, req_meta: dict[str, Any]) -> None:
        request_id = req_meta["request_id"]
        with self.request_task_counts_lock:
            self.request_task_counts[request_id] += 1
            if req_meta["all_task_done"]:
                self.finished_request_markers.add(request_id)

    def _mark_request_task_done(self, request_id: str, all_task_done: bool) -> bool:
        with self.request_task_counts_lock:
            pending_count = self.request_task_counts.get(request_id)
            if pending_count is None:
                return all_task_done

            pending_count -= 1
            if pending_count > 0:
                self.request_task_counts[request_id] = pending_count
                return False

            self.request_task_counts.pop(request_id, None)
            has_finished_marker = request_id in self.finished_request_markers
            self.finished_request_markers.discard(request_id)
            return has_finished_marker

    def _handle_request(self, req_meta: dict[str, Any]):
        request_id = req_meta["request_id"]
        remote_request_id = req_meta["remote_request_id"]
        remote_host = req_meta["remote_host"]
        remote_handshake_port = req_meta["remote_handshake_port"]
        remote_port_send_num = req_meta["remote_port_send_num"]
        all_task_done = req_meta["all_task_done"]
        transfer_failed = self._is_failed_recv_request(request_id)

        try:
            if transfer_failed:
                self._mark_failed_recv_request(request_id, req_meta["local_block_ids"])
                logger.warning(
                    "Skipping KV cache transfer for request. remote_request_id=%s.",
                    remote_request_id,
                )
            else:
                try:
                    logger.debug(
                        "Starting to transfer KV cache for request %s.",
                        remote_request_id,
                    )
                    self._transfer_kv_cache_all_groups(req_meta)
                    logger.debug(
                        "Finished transferring KV cache for request %s.",
                        remote_request_id,
                    )
                except Exception as e:
                    transfer_failed = True
                    self._mark_failed_recv_request(
                        request_id, req_meta["local_block_ids"]
                    )
                    logger.exception(
                        "Failed to transfer KV cache for request %s: %s",
                        remote_request_id,
                        e,
                    )
        finally:
            all_tasks_done = self._mark_request_task_done(request_id, all_task_done)
            if all_tasks_done:
                if transfer_failed or self._is_failed_recv_request(request_id):
                    with self.pending_reformat_lock:
                        self.pending_reformat.pop(request_id, None)
                else:
                    try:
                        self._reformat_pending_kv_caches(request_id)
                    except Exception as e:
                        transfer_failed = True
                        self._mark_failed_recv_request(
                            request_id, req_meta["local_block_ids"]
                        )
                        with self.pending_reformat_lock:
                            self.pending_reformat.pop(request_id, None)
                        logger.exception(
                            "Failed to reformat KV cache after all pulls for request %s: %s",
                            remote_request_id,
                            e,
                        )
                self.task_tracker.update_done_task_count(request_id)
                with self.proc_not_transfer_request_lock:
                    self.proc_not_transfer_request.pop(remote_request_id, None)
                self._clear_failed_recv_request(request_id)
            self.request_queue.task_done()
            self._send_done_signal_to_free_remote_port(
                remote_request_id, remote_host, remote_port_send_num
            )
            # Always send the done signal to the remote host to ensure proper
            # resource cleanup. Failing to do so may cause a memory leak on the
            # remote host.
            self._send_done_recv_signal(
                remote_request_id,
                remote_host,
                remote_handshake_port,
                remote_port_send_num,
            )

    def _send_done_signal_to_free_remote_port(
        self,
        request_id: str,
        remote_host: str,
        remote_port_send_num: dict[int, RemotePortInfo],
    ):
        if (
            self.side_channel_port != self.local_handshake_port
            or not remote_port_send_num
        ):
            return
        with self.proc_not_transfer_request_lock:
            if request_id not in self.proc_not_transfer_request:
                self.proc_not_transfer_request[request_id] = True
            should_send = self.proc_not_transfer_request[request_id]
            if should_send:
                self.proc_not_transfer_request[request_id] = False
        if should_send:
            for remote_port, port_info in remote_port_send_num.items():
                if not isinstance(port_info, dict):
                    logger.warning(
                        "Ignoring malformed remote port info. remote_port=%s, info=%s.",
                        remote_port,
                        repr(port_info)[:64],
                    )
                    continue
                remaining = port_info.get("num")
                remote_host_ = port_info.get("host")
                if remaining is None or remote_host_ is None:
                    logger.warning(
                        "Remote port info is missing num/host keys. remote_port=%s, info=%s.",
                        remote_port,
                        repr(port_info)[:64],
                    )
                    continue
                if remaining == 0:
                    self._send_done_recv_signal(
                        request_id, remote_host_, remote_port, remote_port_send_num
                    )

    def _transfer_kv_cache_all_groups(self, req_meta: dict[str, Any]):
        """Handle a KV cache transfer request."""
        remote_request_id = req_meta["remote_request_id"]
        local_block_ids: BlockIds = req_meta["local_block_ids"]
        remote_block_ids: BlockIds = req_meta["remote_block_ids"]
        local_block_ids_replicate_k: BlockIds = req_meta.get(
            "local_block_ids_replicate_k", tuple()
        )
        remote_block_ids_replicate_k: BlockIds = req_meta.get(
            "remote_block_ids_replicate_k", tuple()
        )
        has_replicate_k_blocks = any(local_block_ids_replicate_k) and any(
            remote_block_ids_replicate_k
        )
        group_pulls: list[GroupPull] = req_meta["group_pulls"]
        remote_engine_id = req_meta["remote_engine_id"]
        remote_host = req_meta["remote_host"]
        remote_handshake_port = req_meta["remote_handshake_port"]
        # Full prefix cache hit: do not need to read remote blocks, just notify
        # P worker that we have the blocks we need.
        num_local_blocks = sum(
            len(group_block_ids) for group_block_ids in local_block_ids
        )
        if num_local_blocks == 0 and not has_replicate_k_blocks:
            return

        # Check if we have the remote metadata cached.
        with self.remote_metadata_lock:
            has_remote_metadata = (
                remote_engine_id in self.kv_caches_base_addr
                and remote_handshake_port in self.kv_caches_base_addr[remote_engine_id]
            )
        if not has_remote_metadata:
            self._get_remote_metadata(
                remote_host, remote_handshake_port, remote_engine_id
            )
        with self.remote_metadata_lock:
            remote_kv_caches_base_addrs = self.kv_caches_base_addr[remote_engine_id][
                remote_handshake_port
            ]
            local_kv_caches_base_addrs = self.kv_caches_base_addr[self.local_engine_id][
                self.local_handshake_port
            ]
            remote_transfer_port = self.remote_transfer_port[remote_engine_id][
                remote_handshake_port
            ]
            remote_block_stride_per_addr = self.remote_block_stride_per_addr[
                remote_engine_id
            ][remote_handshake_port]
        session_id = f"{remote_host}:{remote_transfer_port}"

        req_start_time = time.perf_counter()
        local_addr_list: list[int] = []
        remote_addr_list: list[int] = []
        length_list: list[int] = []
        attention_group_reformat_block_ids: list[
            tuple[tuple[int, list[list[int]], int, list[int]], bool]
        ] = []
        grouped_remote_k_block_ids: list[list[int]] = []
        grouped_local_k_block_ids: list[list[int]] = []
        if self.enable_sfa_dcp_replicated_indexer and has_replicate_k_blocks:
            grouped_remote_k_block_ids, grouped_local_k_block_ids = (
                group_concurrent_contiguous(
                    remote_block_ids_replicate_k[0],
                    local_block_ids_replicate_k[0],
                )
            )

        def pp_layer_indices(
            layer_indices: list[int], prefill_pp_rank: int
        ) -> list[int]:
            first_layer_index, end_layer_index = self.pp_layer_indices[prefill_pp_rank]
            if (
                self.vllm_config.speculative_config is not None
                and prefill_pp_rank == self._prefill_pp_size - 1
            ):
                end_layer_index += self.num_draft_layers
            return [
                layer_idx
                for layer_idx in layer_indices
                if first_layer_index <= layer_idx < end_layer_index
            ]

        for group_pull in group_pulls:
            group_idx = group_pull.group_id
            group_spec, layer_indices = self.kv_group2layeridx[group_idx]
            kv_cache_group_id = group_spec.get("kv_cache_group_id", group_idx)
            layer_indices = pp_layer_indices(layer_indices, group_pull.prefill_pp_rank)
            if not layer_indices:
                continue
            tp_num_need_pulls = group_pull.num_group_pulls
            inner_offset = group_pull.remote_tp_offset
            is_mamba_group = group_spec["kv_cache_spec_type"] == "MambaSpec"
            local_group_block_ids = local_block_ids[kv_cache_group_id]
            remote_group_block_ids = remote_block_ids[kv_cache_group_id]
            has_group_blocks = bool(local_group_block_ids)
            if not has_group_blocks and (is_mamba_group or not has_replicate_k_blocks):
                continue
            if not is_mamba_group:
                grouped_remote_block_ids: list[list[int]] = []
                grouped_local_block_ids: list[list[int]] = []
                if has_group_blocks:
                    is_group_transfer_end = group_pull.is_group_transfer_end
                    # Block ids are already expanded to kernel granularity and truncated in
                    # _get_kv_split_metadata, so consume them directly here.
                    kernel_remote_block_ids = remote_group_block_ids
                    kernel_local_block_ids = local_group_block_ids

                    if tp_num_need_pulls == 1:
                        grouped_remote_block_ids, grouped_local_block_ids = (
                            group_concurrent_contiguous(
                                kernel_remote_block_ids, kernel_local_block_ids
                            )
                        )
                    else:
                        grouped_remote_block_ids = [
                            [block_id] for block_id in kernel_remote_block_ids
                        ]
                        grouped_local_block_ids = [
                            [block_id] for block_id in kernel_local_block_ids
                        ]
                    attention_group_reformat_block_ids.append(
                        (
                            (
                                group_idx,
                                grouped_local_block_ids,
                                tp_num_need_pulls,
                                layer_indices,
                            ),
                            is_group_transfer_end,
                        )
                    )
            else:
                # When Prefix Caching is enabled on both P and D nodes, num_block should not be forced to match,
                # as the D-node requires dynamic allocation based on its specific cache hit rate.
                transfer_block_idx = (
                    len(remote_group_block_ids) - self.num_speculative_tokens - 1
                )
                grouped_remote_block_ids = [
                    [remote_group_block_ids[transfer_block_idx]]
                ]
                grouped_local_block_ids = [[local_group_block_ids[0]]]

            if is_mamba_group:
                for layer_idx in layer_indices:
                    start_meta_idx = len(local_addr_list)
                    self._append_mamba_transfer_meta(
                        local_addr_list,
                        remote_addr_list,
                        length_list,
                        group_spec=group_spec,
                        local_layer_base_addr=local_kv_caches_base_addrs[layer_idx],
                        remote_layer_base_addr=remote_kv_caches_base_addrs[layer_idx],
                        block_len=self.block_len_per_addr[layer_idx],
                        block_stride=self.block_stride_per_addr[layer_idx],
                        remote_block_stride=remote_block_stride_per_addr[layer_idx],
                        remote_block_id=grouped_remote_block_ids[0][0],
                        local_block_id=grouped_local_block_ids[0][0],
                        tp_num_need_pulls=tp_num_need_pulls,
                        remote_tp_offset=inner_offset,
                    )
                    if logger.isEnabledFor(logging.DEBUG):
                        for local_addr, remote_addr, length in zip(
                            local_addr_list[start_meta_idx:],
                            remote_addr_list[start_meta_idx:],
                            length_list[start_meta_idx:],
                        ):
                            logger.debug(
                                "HIXL mamba transfer meta: request_id=%s group_idx=%s layer_idx=%s "
                                "local_block_id=%s remote_block_id=%s tp_num_need_pulls=%s "
                                "remote_tp_offset=%s  session_id=%s",
                                remote_request_id,
                                group_idx,
                                layer_idx,
                                grouped_local_block_ids[0][0],
                                grouped_remote_block_ids[0][0],
                                tp_num_need_pulls,
                                inner_offset,
                                session_id,
                            )
                continue

            for layer_idx in layer_indices:
                for cache_idx in range(len(local_kv_caches_base_addrs[layer_idx])):
                    local_layer_base_addr = local_kv_caches_base_addrs[layer_idx][
                        cache_idx
                    ]
                    remote_layer_base_addr = remote_kv_caches_base_addrs[layer_idx][
                        cache_idx
                    ]
                    block_len = self.block_len_per_addr[layer_idx][cache_idx]
                    block_stride = self.block_stride_per_addr[layer_idx][cache_idx]
                    remote_block_stride = remote_block_stride_per_addr[layer_idx][
                        cache_idx
                    ]
                    inner_block_len = block_len // tp_num_need_pulls
                    if (
                        self.enable_sfa_dcp_replicated_indexer
                        and self.block_size_scale[layer_idx][cache_idx] > 1
                    ):
                        if has_replicate_k_blocks:
                            transfer_remote_block_ids = grouped_remote_k_block_ids
                            transfer_local_block_ids = grouped_local_k_block_ids
                        else:
                            continue
                    else:
                        if not has_group_blocks:
                            continue
                        transfer_remote_block_ids, transfer_local_block_ids = (
                            split_if_not_byte_contiguous(
                                grouped_remote_block_ids,
                                grouped_local_block_ids,
                                remote_block_stride=remote_block_stride,
                                local_block_stride=block_stride,
                                block_len=inner_block_len,
                            )
                        )
                    for remote_block_id, local_block_id in zip(
                        transfer_remote_block_ids, transfer_local_block_ids
                    ):
                        local_addr = (
                            local_layer_base_addr
                            + local_block_id[0] * block_stride
                            + inner_offset * inner_block_len
                        )
                        remote_addr = (
                            remote_layer_base_addr
                            + remote_block_id[0] * remote_block_stride
                        )
                        length = inner_block_len * len(local_block_id)
                        local_addr_list.append(local_addr)
                        remote_addr_list.append(remote_addr)
                        length_list.append(length)
                    logger.debug(
                        "HIXL kv transfer meta: request_id=%s group_idx=%s layer_idx=%s local_block_ids=%s "
                        "remote_block_ids=%s tp_num_need_pulls=%s remote_tp_offset=%s session_id=%s",
                        remote_request_id,
                        group_idx,
                        layer_idx,
                        grouped_local_block_ids,
                        grouped_remote_block_ids,
                        tp_num_need_pulls,
                        inner_offset,
                        session_id,
                    )
        if not local_addr_list:
            return

        if logger.isEnabledFor(logging.DEBUG):
            shown = local_addr_list[:_MAX_DEBUG_ADDR_ENTRIES]
            logger.debug(
                "HIXLConnector transfer request=%s session id=%s entries=%d "
                "local_addrs(first %d)=%s remote_addrs(first %d)=%s "
                "lengths(first %d)=%s",
                remote_request_id,
                session_id,
                len(local_addr_list),
                len(shown),
                shown,
                len(shown),
                remote_addr_list[:_MAX_DEBUG_ADDR_ENTRIES],
                len(shown),
                length_list[:_MAX_DEBUG_ADDR_ENTRIES],
            )
        self.engine.sync_read(
            session_id, local_addr_list, remote_addr_list, length_list
        )

        req_end_time = time.perf_counter()
        req_transfer_elapsed = (req_end_time - req_start_time) * 1000
        transfer_bytes = sum(length_list)
        transfer_elapsed_s = req_end_time - req_start_time
        transfer_bandwidth_gbs = (
            transfer_bytes / transfer_elapsed_s / 1e9 if transfer_elapsed_s > 0 else 0.0
        )
        logger.info(
            "KV cache transfer for request %s took %.2f ms. "
            "size=%d bytes (%.1f MiB) bandwidth=%.2f GB/s "
            "local_ip %s local_device_id %s remote_session_id %s",
            remote_request_id,
            req_transfer_elapsed,
            transfer_bytes,
            transfer_bytes / 1048576.0,
            transfer_bandwidth_gbs,
            get_ip(),
            self.tp_rank,
            session_id,
        )

        ready_attention_group_reformat_block_ids = []
        for reformat_group, is_group_transfer_end in attention_group_reformat_block_ids:
            if is_group_transfer_end:
                ready_attention_group_reformat_block_ids.append(reformat_group)
        if ready_attention_group_reformat_block_ids:
            shard_idx = int(req_meta.get("shard_idx", 0))
            self._stash_pending_reformat(
                req_meta["request_id"],
                shard_idx,
                ready_attention_group_reformat_block_ids,
            )

    def _stash_pending_reformat(
        self,
        request_id: str,
        shard_idx: int,
        ready_attention_group_reformat_block_ids: list[
            tuple[int, list[list[int]], int, list[int]]
        ],
    ) -> None:
        with self.pending_reformat_lock:
            self.pending_reformat[request_id][shard_idx] = (
                ready_attention_group_reformat_block_ids
            )

    def _reformat_pending_kv_caches(self, request_id: str) -> None:
        with self.pending_reformat_lock:
            shard_reformats = self.pending_reformat.pop(request_id, {})
        for shard_idx in sorted(shard_reformats):
            logger.debug(
                "Reformatting KV cache after all pulls completed. request_id=%s shard_idx=%s",
                request_id,
                shard_idx,
            )
            self._apply_kv_cache_reformat(shard_reformats[shard_idx])

    def _apply_kv_cache_reformat(
        self,
        ready_attention_group_reformat_block_ids: list[
            tuple[int, list[list[int]], int, list[int]]
        ],
    ) -> None:
        if not ready_attention_group_reformat_block_ids:
            return

        gqa_reformat_groups = [
            (group_idx, grouped_local_block_ids, num_group_pulls, layer_indices)
            for (
                group_idx,
                grouped_local_block_ids,
                num_group_pulls,
                layer_indices,
            ) in ready_attention_group_reformat_block_ids
            if num_group_pulls > 1
        ]

        if self.is_hma_required:
            for (
                group_idx,
                grouped_local_block_ids,
                num_group_pulls,
                layer_indices,
            ) in gqa_reformat_groups:
                num_reformat_blocks = sum(
                    len(block_ids) for block_ids in grouped_local_block_ids
                )
                logger.debug(
                    "Reformat hybrid linear KV cache for GQA attention group. "
                    "group_idx=%s, num_group_pulls=%s, num_block_groups=%s, num_reformat_blocks=%s, "
                    "layer_indices=%s",
                    group_idx,
                    num_group_pulls,
                    len(grouped_local_block_ids),
                    num_reformat_blocks,
                    layer_indices,
                )
                group_kv_caches = self._get_group_kv_caches(group_idx, layer_indices)
                if not group_kv_caches:
                    continue
                self.reformat_kv_cache_hybrid_linear_torch(
                    grouped_local_block_ids, num_group_pulls, group_kv_caches
                )
            return

        uniform_num_pulls = {
            num_group_pulls
            for _, _, num_group_pulls, _ in ready_attention_group_reformat_block_ids
        }
        if len(uniform_num_pulls) != 1:
            raise RuntimeError(
                f"Non-hybrid HIXL KV reformat expects uniform group pulls, but got {uniform_num_pulls}."
            )

        num_group_pulls = next(iter(uniform_num_pulls))
        need_cat_cache = num_group_pulls > 1
        need_nz_cache = get_ascend_config().enable_kv_nz
        if not (need_cat_cache or need_nz_cache):
            return

        use_fused_op = ascend_envs.VLLM_ASCEND_FUSION_OP_TRANSPOSE_KV_CACHE_BY_BLOCK
        for (
            group_idx,
            reformat_block_ids,
            _,
            layer_indices,
        ) in ready_attention_group_reformat_block_ids:
            group_kv_caches = self._get_group_kv_caches(group_idx, layer_indices)
            if not group_kv_caches:
                continue
            if use_fused_op and enable_custom_op():
                if need_cat_cache:
                    self.reformat_kv_cache_with_fused_op(
                        reformat_block_ids, num_group_pulls, group_kv_caches
                    )
                if need_nz_cache:
                    self.reformat_kv_cache(
                        reformat_block_ids,
                        num_group_pulls,
                        False,
                        need_nz_cache,
                        group_kv_caches,
                    )
            else:
                self.reformat_kv_cache(
                    reformat_block_ids,
                    num_group_pulls,
                    need_cat_cache,
                    need_nz_cache,
                    group_kv_caches,
                )

    @torch.no_grad()
    def reformat_kv_cache_hybrid_linear_torch(
        self, block_ids: list[list[int]], tp_num_need_pulls: int, group_kv_caches
    ):
        flat_block_ids = [item for sublist in block_ids for item in sublist]
        if not flat_block_ids or tp_num_need_pulls == 1:
            return
        device = list(self.kv_caches.values())[0][0].device
        block_ids_tensor = torch.tensor(flat_block_ids, dtype=torch.long, device=device)
        num_blocks = block_ids_tensor.numel()

        def _transpose_cache_by_block(cache: torch.Tensor):
            # The transferred cache is laid out as
            # [block, split, token, head_per_split, dim]. Restore it to
            # [block, token, split, head_per_split, dim] in the selected blocks.
            selected = cache.index_select(0, block_ids_tensor)
            block_size = cache.shape[1]
            transposed = (
                selected.reshape(num_blocks, tp_num_need_pulls, block_size, -1)
                .transpose(1, 2)
                .contiguous()
                .reshape_as(selected)
            )
            cache.index_copy_(0, block_ids_tensor, transposed)

        for _, (k_cache_layer, v_cache_layer) in group_kv_caches.items():
            _transpose_cache_by_block(k_cache_layer)
            _transpose_cache_by_block(v_cache_layer)

    def _append_mamba_transfer_meta(
        self,
        local_addr_list: list[int],
        remote_addr_list: list[int],
        length_list: list[int],
        group_spec: dict[str, Any],
        local_layer_base_addr: list[int],
        remote_layer_base_addr: list[int],
        block_len: list[int],
        block_stride: list[int],
        remote_block_stride: list[int],
        remote_block_id: int,
        local_block_id: int,
        tp_num_need_pulls: int,
        remote_tp_offset: int,
    ) -> None:
        remote_tp_size = self.tp_size * tp_num_need_pulls
        if remote_tp_size < self.tp_size:
            raise ValueError(
                f"Mamba prefill TP size({remote_tp_size}) must be >= decode TP "
                f"size({self.tp_size})."
            )
        if remote_tp_size % self.tp_size != 0:
            raise ValueError(
                f"Mamba prefill TP size({remote_tp_size}) must be divisible by "
                f"decode TP size({self.tp_size})."
            )

        remote_conv_addr, remote_ssm_addr = remote_layer_base_addr[:2]
        local_conv_addr, local_ssm_addr = local_layer_base_addr[:2]
        local_conv_len, local_ssm_len = block_len[:2]
        local_conv_stride, local_ssm_stride = block_stride[:2]
        remote_conv_stride, remote_ssm_stride = remote_block_stride[:2]

        tp_ratio = tp_num_need_pulls
        remote_conv_len = local_conv_len // tp_ratio
        remote_ssm_len = local_ssm_len // tp_ratio

        if tp_ratio == 1:
            local_addr_list.extend(
                [
                    local_conv_addr + local_block_id * local_conv_stride,
                    local_ssm_addr + local_block_id * local_ssm_stride,
                ]
            )
            remote_addr_list.extend(
                [
                    remote_conv_addr + remote_block_id * remote_conv_stride,
                    remote_ssm_addr + remote_block_id * remote_ssm_stride,
                ]
            )
            length_list.extend([remote_conv_len, remote_ssm_len])
            return

        conv_shape = group_spec["shapes"][0]
        conv_dtype_size = group_spec["dtype_sizes"][0]

        linear_key_head_dim = (
            self.vllm_config.model_config.hf_text_config.linear_key_head_dim
        )
        linear_num_key_heads = (
            self.vllm_config.model_config.hf_text_config.linear_num_key_heads
        )
        linear_value_head_dim = (
            self.vllm_config.model_config.hf_text_config.linear_value_head_dim
        )
        linear_num_value_heads = (
            self.vllm_config.model_config.hf_text_config.linear_num_value_heads
        )
        remote_num_key_heads = linear_num_key_heads // remote_tp_size
        remote_num_value_heads = linear_num_value_heads // remote_tp_size
        remote_conv_width = (
            remote_num_key_heads * 2 * linear_key_head_dim
            + remote_num_value_heads * linear_value_head_dim
        )
        remote_conv_offsets = [
            0,
            remote_num_key_heads * linear_key_head_dim,
            remote_num_key_heads * 2 * linear_key_head_dim,
        ]
        remote_conv_sizes = [
            remote_num_key_heads * linear_key_head_dim,
            remote_num_key_heads * linear_key_head_dim,
            remote_num_value_heads * linear_value_head_dim,
        ]

        for i in range(conv_shape[0]):
            for remote_conv_offset, remote_conv_size in zip(
                remote_conv_offsets, remote_conv_sizes
            ):
                remote_addr_offset = (
                    i * remote_conv_width + remote_conv_offset
                ) * conv_dtype_size
                local_addr_offset = (
                    (i * remote_conv_width + remote_conv_offset) * tp_ratio
                    + remote_tp_offset * remote_conv_size
                ) * conv_dtype_size
                local_addr_list.append(
                    local_conv_addr
                    + local_block_id * local_conv_stride
                    + local_addr_offset
                )
                remote_addr_list.append(
                    remote_conv_addr
                    + remote_block_id * remote_conv_stride
                    + remote_addr_offset
                )
                length_list.append(remote_conv_size * conv_dtype_size)

        local_addr_list.append(
            local_ssm_addr
            + local_block_id * local_ssm_stride
            + remote_tp_offset * local_ssm_len // tp_num_need_pulls
        )
        remote_addr_list.append(remote_ssm_addr + remote_block_id * remote_ssm_stride)
        length_list.append(remote_ssm_len)

    def _get_group_kv_caches(
        self, group_idx: int, layer_indices: list[int] | None = None
    ) -> dict[str, Any]:
        if layer_indices is None:
            _, layer_indices = self.kv_group2layeridx[group_idx]
        layer_index_set = set(layer_indices)
        num_attn_module = (
            2
            if self.vllm_config.model_config.hf_text_config.model_type
            == "longcat_flash"
            else 1
        )
        from vllm.v1.worker.utils import extract_layer_index

        def layer_in_group(layer_name: str) -> bool:
            if "mtp" in layer_name:
                return any(
                    layer_idx >= self.num_layers for layer_idx in layer_index_set
                )
            return extract_layer_index(layer_name, num_attn_module) in layer_index_set

        return {
            layer_name: layer_cache
            for layer_name, layer_cache in self.kv_caches.items()
            if layer_in_group(layer_name)
        }

    @staticmethod
    def _get_kv_cache_dims_from_tensors(
        kv_caches: dict[str, Any],
    ) -> tuple[int, int, int]:
        """Return (num_kv_heads, k_head_dim, v_head_dim) from registered KV cache tensors."""
        k_cache, v_cache = next(iter(kv_caches.values()))
        return int(k_cache.shape[-2]), int(k_cache.shape[-1]), int(v_cache.shape[-1])

    def reformat_kv_cache_with_fused_op(
        self,
        block_ids: list[list[int]],
        tp_num_need_pulls: int,
        kv_caches: dict[str, Any] | None = None,
    ):
        if kv_caches is None:
            kv_caches = self.kv_caches
        k_cache = list(kv_caches.values())[0][0]
        device = k_cache.device
        num_kv_head, head_dim, _ = self._get_kv_cache_dims_from_tensors(kv_caches)
        block_size = self.vllm_config.cache_config.block_size
        layers = len(kv_caches)
        flat_block_ids = [item for sublist in block_ids for item in sublist]
        block_ids_tensor = torch.tensor(
            flat_block_ids, dtype=torch.int64, device=device
        )

        k_caches = []
        v_caches = []
        for _, (k_cache_layer, v_cache_layer) in kv_caches.items():
            k_caches.append(k_cache_layer)
            v_caches.append(v_cache_layer)

        torch.ops._C_ascend.transpose_kv_cache_by_block(
            k_caches,
            v_caches,
            block_ids_tensor,
            block_size,
            num_kv_head,
            head_dim,
            tp_num_need_pulls,
            layers,
        )

    def reformat_kv_cache(
        self,
        block_ids: list[list[int]],
        tp_num_need_pulls: int,
        need_cat_cache: bool = False,
        need_nz_cache: bool = False,
        kv_caches: dict[str, Any] | None = None,
    ):
        if kv_caches is None:
            kv_caches = self.kv_caches
        k_cache = list(kv_caches.values())[0][0]
        dtype = k_cache.dtype
        device = k_cache.device
        num_kv_heads, k_head_dim, v_head_dim = self._get_kv_cache_dims_from_tensors(
            kv_caches
        )

        flat_block_ids = [item for sublist in block_ids for item in sublist]
        block_ids_tensor = torch.tensor(
            flat_block_ids, dtype=torch.int32, device=device
        )
        num_blocks = len(flat_block_ids)
        num_tokens = num_blocks * self.block_size

        # Create device tensors for copy operations
        block_table = block_ids_tensor.view(1, -1)
        block_len_tensor = torch.tensor([num_tokens], dtype=torch.int32, device=device)
        seq_start_tensor = torch.tensor([0], dtype=torch.int32, device=device)

        k_buffer = torch.empty(
            (num_tokens, num_kv_heads, k_head_dim), dtype=dtype, device=device
        )
        v_buffer = torch.empty(
            (num_tokens, num_kv_heads, v_head_dim), dtype=dtype, device=device
        )

        # Create slot mapping for reshape operations
        block_offsets = torch.arange(
            0, self.block_size, dtype=torch.int32, device=device
        )
        slot_mapping = (
            block_offsets.reshape((1, self.block_size))
            + block_ids_tensor.reshape((num_blocks, 1)) * self.block_size
        ).flatten()

        # Required for correctness: without this device synchronization the
        # reformat path below crashes in GQA scenarios.
        torch.npu.synchronize()

        # Process each layer in the KV cache
        for _, (k_cache_layer, v_cache_layer) in kv_caches.items():
            # Load cache data into buffers
            torch_npu.atb.npu_paged_cache_load(
                k_cache_layer,
                v_cache_layer,
                block_table,
                block_len_tensor,
                seq_starts=seq_start_tensor,
                key=k_buffer,
                value=v_buffer,
            )
            if need_cat_cache:
                self._cat_kv_cache(
                    k_cache_layer,
                    v_cache_layer,
                    k_buffer,
                    v_buffer,
                    tp_num_need_pulls,
                    num_blocks,
                    num_tokens,
                    slot_mapping,
                    num_kv_heads,
                )
            if need_nz_cache:
                self._nz_kv_cache(
                    k_cache_layer,
                    v_cache_layer,
                    k_buffer,
                    v_buffer,
                    slot_mapping,
                    num_kv_heads,
                    k_head_dim,
                    v_head_dim,
                )
        # Clean up buffers
        del k_buffer, v_buffer

    def _cat_kv_cache(
        self,
        k_cache_layer,
        v_cache_layer,
        k_buffer,
        v_buffer,
        tp_num_need_pulls,
        num_blocks,
        num_tokens,
        slot_mapping,
        num_kv_heads: int,
    ):
        def _transpose_kv_cache_between_head(buffer: torch.Tensor) -> torch.Tensor:
            buffer = buffer.view(num_blocks, tp_num_need_pulls, self.block_size, -1)
            buffer.transpose_(1, 2)
            return buffer.contiguous().view(num_tokens, num_kv_heads, -1)

        # Transpose KV cache
        k_buffer = _transpose_kv_cache_between_head(k_buffer)
        v_buffer = _transpose_kv_cache_between_head(v_buffer)

        # Reshape and cache the processed buffers
        torch_npu._npu_reshape_and_cache(
            key=k_buffer,
            value=v_buffer,
            key_cache=k_cache_layer,
            value_cache=v_cache_layer,
            slot_indices=slot_mapping,
        )

    def _nz_kv_cache(
        self,
        k_cache_layer,
        v_cache_layer,
        k_buffer,
        v_buffer,
        slot_mapping,
        num_kv_heads: int,
        k_head_dim: int,
        v_head_dim: int,
    ):
        nz_fmt_last_dim = 16
        k_cache_layer = k_cache_layer.view(
            -1,
            k_head_dim * num_kv_heads // nz_fmt_last_dim,
            self.block_size,
            nz_fmt_last_dim,
        )
        v_cache_layer = v_cache_layer.view(
            -1,
            v_head_dim * num_kv_heads // nz_fmt_last_dim,
            self.block_size,
            nz_fmt_last_dim,
        )
        torch_npu.npu_scatter_pa_kv_cache(
            k_buffer, v_buffer, k_cache_layer, v_cache_layer, slot_mapping
        )

    def _get_remote_metadata(
        self,
        remote_host: str,
        remote_handshake_port: int,
        expected_engine_id: str,
    ) -> None:
        """Get the metadata from the remote host."""
        sock: zmq.Socket | None = None  # type: ignore
        try:
            sock = self._get_remote_socket(remote_host, remote_handshake_port)
            ensure_zmq_send(
                sock,
                self.encoder.encode((GET_META_MSG, "")),
                f"{remote_host}:{remote_handshake_port}",
            )
            metadata_bytes = ensure_zmq_recv(
                sock, f"{remote_host}:{remote_handshake_port}"
            )
            agent_meta = self.decoder.decode(metadata_bytes)
            engine_id = agent_meta.engine_id
            if engine_id == self.local_engine_id:
                raise RuntimeError(
                    f"Conflict engine id {engine_id} with local engine id "
                    f"{self.local_engine_id}; the P and D sides must use "
                    "different engine_id values."
                )
            if engine_id != expected_engine_id:
                # The caches are keyed by the routed remote_engine_id while the
                # metadata is stored under the engine the peer reports; a
                # mismatch (stale routing, reused engine_id) would silently
                # index a wrong or auto-created empty entry, so fail the pull.
                raise RuntimeError(
                    "Remote metadata engine_id does not match the routed engine. "
                    f"expected={expected_engine_id}, reported={engine_id}, "
                    f"peer={remote_host}:{remote_handshake_port}. "
                    "Check: verify kv_transfer_params routing and that every "
                    "P/D engine uses a unique engine_id."
                )
            if agent_meta.kv_group2layeridx != self.kv_group2layeridx:
                # Layer ids index the remote base addresses below; with a
                # mismatched layout a descriptor can land inside another
                # registered segment and silently read wrong memory, so fail
                # the pull instead of continuing with the local layout.
                raise RuntimeError(
                    "Remote kv_group2layeridx is inconsistent with the local layout. "
                    f"remote_engine_id={engine_id}, "
                    f"remote_layout={agent_meta.kv_group2layeridx}, "
                    f"local_layout={self.kv_group2layeridx}. "
                    "Check: ensure both sides run the same model and connector "
                    "version with identical layer partitioning."
                )
            with self.remote_metadata_lock:
                self.remote_kv_group2layeridx.get_or_create(engine_id)[
                    remote_handshake_port
                ] = agent_meta.kv_group2layeridx
                self.kv_caches_base_addr.get_or_create(engine_id)[
                    remote_handshake_port
                ] = agent_meta.kv_caches_base_addr
                self.remote_transfer_port.get_or_create(engine_id)[
                    remote_handshake_port
                ] = agent_meta.listen_port
                self.remote_block_size_scale.get_or_create(engine_id)[
                    remote_handshake_port
                ] = agent_meta.block_size_scale
                self.remote_block_stride_per_addr.get_or_create(engine_id)[
                    remote_handshake_port
                ] = agent_meta.block_strides
        except Exception:
            if isinstance(sock, zmq.Socket):  # type: ignore
                sock.close()
                sock = None
            raise
        finally:
            if sock is not None:
                self._return_remote_socket(sock, remote_host, remote_handshake_port)
                logger.debug(
                    "Returned socket to pool for %s:%d",
                    remote_host,
                    remote_handshake_port,
                )

    def _send_done_recv_signal(
        self,
        request_id: str,
        remote_host: str,
        remote_handshake_port: int,
        remote_port_send_num: dict[int, RemotePortInfo],
    ):
        logger.debug(
            "Sending done recving signal for request %s to %s:%d",
            request_id,
            remote_host,
            remote_handshake_port,
        )
        sock: zmq.Socket | None = None  # type: ignore
        try:
            sock = self._get_remote_socket(remote_host, remote_handshake_port)
            data_bytes = self.encoder.encode(
                (DONE_RECVING_MSG, request_id, remote_port_send_num)
            )
            ensure_zmq_send(sock, data_bytes, f"{remote_host}:{remote_handshake_port}")
            resp = ensure_zmq_recv(sock, f"{remote_host}:{remote_handshake_port}")
            if logger.isEnabledFor(logging.DEBUG):
                logger.debug(
                    "Received response for request %s: %s",
                    request_id,
                    resp.decode("utf-8"),
                )
            if resp != b"ACK":
                logger.error(
                    "Failed to receive ACK for request. request_id=%s, source=%s:%d.",
                    request_id,
                    remote_host,
                    remote_handshake_port,
                )
                raise RuntimeError(
                    f"Failed to receive ACK, resp: {resp.decode('utf-8')}"
                )
        except RuntimeError as e:
            if isinstance(sock, zmq.Socket):  # type: ignore
                sock.close()
                sock = None
            # Deliberately not re-raising: this runs in a finally block where a
            # raise would mask the original transfer error. The prefill side
            # still frees the blocks via the delayed-free timeout, so log an
            # error to make the missed notification visible.
            logger.error(
                "Failed to send done-recving signal; the prefill side will "
                "force-free this request only after the delayed-free timeout. "
                "error=%s.",
                e,
            )
        finally:
            if sock is not None:
                self._return_remote_socket(sock, remote_host, remote_handshake_port)
                logger.debug(
                    "Returned socket to pool for %s:%d",
                    remote_host,
                    remote_handshake_port,
                )

    def _get_remote_socket(
        self, remote_host: str, remote_handshake_port: int
    ) -> zmq.Socket:  # type: ignore
        """Get a socket to the remote host."""
        remote_path = make_zmq_path("tcp", remote_host, remote_handshake_port)
        with self.remote_sockets_lock:
            if self.remote_sockets[remote_path]:
                return self.remote_sockets[remote_path].popleft()

            if self._remote_zmq_ctx is None:
                self._remote_zmq_ctx = zmq.Context()  # type: ignore[assignment]
            ctx = self._remote_zmq_ctx
            sock = make_zmq_socket(
                ctx=ctx,
                path=remote_path,
                socket_type=zmq.REQ,  # type: ignore
                bind=False,
            )
            sock.setsockopt(
                zmq.SNDTIMEO,  # type: ignore
                int(self.timeout * 1000),
            )
            sock.setsockopt(
                zmq.RCVTIMEO,  # type: ignore
                int(self.timeout * 1000),
            )
            return sock

    def _return_remote_socket(
        self,
        sock: zmq.Socket,  # type: ignore
        remote_host: str,
        remote_handshake_port: int,
    ) -> None:
        """Return the remote socket to the pool."""
        remote_path = make_zmq_path("tcp", remote_host, remote_handshake_port)
        with self.remote_sockets_lock:
            self.remote_sockets[remote_path].append(sock)

    def _close_remote_sockets(self) -> None:
        """Close every pooled remote socket and destroy the shared context."""
        with self.remote_sockets_lock:
            for sockets in self.remote_sockets.values():
                while sockets:
                    sockets.popleft().close(linger=0)
            self.remote_sockets.clear()
            ctx = self._remote_zmq_ctx
            self._remote_zmq_ctx = None
        if ctx is not None:
            ctx.destroy(linger=0)
