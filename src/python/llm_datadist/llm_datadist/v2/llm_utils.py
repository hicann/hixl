#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# This program is free software, you can redistribute it and/or modify it.
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This file is a part of the CANN Open Software.
# Licensed under CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------
from dataclasses import dataclass
from threading import Thread
from typing import Dict, List, Optional, Union, Tuple
from llm_datadist.utils import log
from llm_datadist.utils.utils import check_isinstance, check_list_uint64, check_uint64, check_uint32
from llm_datadist.status import LLMException, LLMStatusCode, raise_if_false, code_2_status, raise_if_true
from llm_datadist.data_type import DataType, python_dtype_2_dwrapper_dtype
from llm_datadist.v2.llm_types import CacheDesc, KvCache, CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey, Placement, \
    CacheTask, TransferConfig, LayerSynchronizer, Cache, TransferWithCacheKeyConfig, PushType, MemInfo
from llm_datadist import llm_datadist_wrapper

# UINT64_MAX
_INVALID_ID = 2 ** 64 - 1
_MAX_DISPLAYED_REQ_ID_COUNT = 8
_NUM_TENSORS_PER_LAYER = 2


def clone_cache_desc(cache_desc: CacheDesc) -> CacheDesc:
    return CacheDesc(cache_desc.num_tensors,
                     cache_desc.shape[:],
                     cache_desc.data_type,
                     cache_desc.placement,
                     cache_desc.batch_dim,
                     cache_desc.seq_len_dim_index,
                     cache_desc.kv_tensor_format)


def verify_cache_shape(shape: List[int]) -> None:
    raise_if_false(len(shape) > 0, "scalar is not supported")
    raise_if_false(0 not in shape, f'empty dimension is not supported, shape = {shape}')
    for dim in shape:
        if dim < -1:
            raise LLMException(f"dim {dim} is not supported, shape = {shape}")


def to_data_type(data_type_str: str) -> DataType:
    for dtype in DataType:
        if str(dtype.value) == data_type_str:
            return dtype
    raise LLMException(f'unsupported dtype: {data_type_str}', status_code=LLMStatusCode.LLM_PARAM_INVALID)


def is_invalid_id(req_id_or_prefix_id: int) -> bool:
    return (req_id_or_prefix_id < 0) or (req_id_or_prefix_id == _INVALID_ID)


def is_valid_id(req_id_or_prefix_id: int) -> bool:
    return not is_invalid_id(req_id_or_prefix_id)


def calc_tensor_size(shape: List[int], data_type: DataType) -> int:
    ge_data_type = python_dtype_2_dwrapper_dtype.get(data_type)
    tensor_size = llm_datadist_wrapper.calc_tensor_size(shape, ge_data_type)
    raise_if_false(tensor_size >= 0,
                   'Failed to calc tensor size, shape = {0}, data_type = {1}', shape, data_type)
    return tensor_size


class CacheDescParser(object):
    @staticmethod
    def parse_by_options(options: Dict[str, str]) -> CacheDesc:
        raise_if_false('llm.RefInputShapes' in options, 'llm.RefInputShapes not set')
        kv_shape_str = options['llm.RefInputShapes']
        kv_shapes = kv_shape_str.split(';')
        raise_if_false(len(set(kv_shapes)) == 1,
                       f'zero or multiple different shapes is not supported, llm.RefInputShapes = {kv_shapes}')

        raise_if_false('llm.RefInputDtypes' in options, 'llm.RefInputDtypes not set')
        kv_data_type_str = options['llm.RefInputDtypes']
        kv_data_types = kv_data_type_str.split(';')
        raise_if_false(len(set(kv_data_types)) == 1,
                       f'zero or multiple different data types is not supported, llm.RefInputDtypes = {kv_data_types}')
        raise_if_false(len(kv_shapes) == len(kv_data_types),
                       f'kv_shapes num ({len(kv_shapes)}) mismatches that of kv_data_type ({len(kv_data_types)})')
        kv_shape = list((int(dim) for dim in kv_shapes[0].split(',')))
        verify_cache_shape(kv_shape)
        if 'llm.RefInputSeqLenDimIndex' in options:
            seq_len_dim_index = int(options['llm.RefInputSeqLenDimIndex'])
            log.info(f'get seq_len_dim_index from option, value = {seq_len_dim_index}')
            raise_if_false(0 <= seq_len_dim_index < len(kv_shape),
                           f'seq_len_dim_index ({seq_len_dim_index}) out of range, kv_shape = {kv_shape}')
        elif -1 in kv_shape[1:]:
            raise_if_false(kv_shape[1:].count(-1) == 1,
                           f'can only have one dynamic dim apart from batch_dim(0), '
                           f'but got {kv_shape[1:].count(-1)}, shape = {kv_shape}')
            seq_len_dim_index = kv_shape[1:].index(-1) + 1
            log.info(f'seq_len_dim_index inferred by shape, value = {seq_len_dim_index}')
        else:
            seq_len_dim_index = -1
            log.info(f'llm.RefInputSeqLenDimIndex not set and can not infer by shape,'
                     f'seq_len_dim_index = {seq_len_dim_index}')
        kv_tensor_format = options.get("llm.kvTensorFormat", None)
        kv_data_type = to_data_type(kv_data_types[0])
        cache_desc = CacheDesc(len(kv_shapes), kv_shape, kv_data_type, Placement.DEVICE, 0, seq_len_dim_index,
                               kv_tensor_format)
        log.info(f'parse cache_desc from option, value = {cache_desc}')
        return cache_desc

def pack_cache_desc(cache_desc: CacheDesc) -> Tuple[int, int, int, List[int], int, int]:
    return (cache_desc.num_tensors, cache_desc.data_type.value, cache_desc.seq_len_dim_index, cache_desc.shape,
            cache_desc.placement.value, cache_desc._is_blocks)


def pack_cache_key(cache_key: CacheKey) -> Tuple[int, int, int, int, int, int, bool]:
    return cache_key.prompt_cluster_id, -1, 0, cache_key.req_id, cache_key.prefix_id, cache_key.model_id, False


def pack_cache_key_by_id(cache_key: CacheKeyByIdAndIndex) -> Tuple[int, int, int, int, int, int, bool]:
    return cache_key.cluster_id, cache_key.cache_id, cache_key.batch_index, _INVALID_ID, _INVALID_ID, 0, False


def pack_block_cache_key(cache_key: BlocksCacheKey) -> Tuple[int, int, int, int, int, int, bool]:
    return cache_key.prompt_cluster_id, -1, 0, _INVALID_ID, _INVALID_ID, cache_key.model_id, True

def pack_mem_info(mem_info: MemInfo) -> Tuple[int, int, int]:
    return (mem_info.mem_type.value, mem_info.addr, mem_info.size)


@dataclass
class TransferCacheParameters:
    src_cache: Union[Cache, KvCache]
    transfer_configs: Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]],
                            Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]]
    src_block_indices: Optional[List[int]] = None
    dst_block_indices: Optional[List[int]] = None
    dst_block_memory_size: Optional[int] = None


class TransferCacheJob:
    task_id = 0

    def __init__(self, params: TransferCacheParameters,
                 layer_synchronizer: LayerSynchronizer,
                 transfer_cache_func):
        self._transfer_configs = params.transfer_configs
        self._src_block_indices = params.src_block_indices
        self._dst_block_indices = params.dst_block_indices
        self._dst_block_memory_size = params.dst_block_memory_size
        self._cache_id = params.src_cache.cache_id
        self._cache_desc = params.src_cache.cache_desc
        self._num_layers = 0
        self._rets: Dict[int, LLMStatusCode] = {}
        self._timeout_in_millis: Optional[int] = None
        self._is_data_cache_engine = isinstance(params.src_cache, Cache)
        self._layer_synchronizer = layer_synchronizer
        self._transfer_cache_func = transfer_cache_func

    def init(self):
        raise_if_false(self._cache_desc.num_tensors % 2 == 0, "cache_desc.num_tensors ({0}) is not even",
                       self._cache_desc.num_tensors)
        self._num_layers = self._cache_desc.num_tensors // 2
        for transfer_config in self._transfer_configs:
            if transfer_config.src_layer_range is None:
                transfer_config.src_layer_range = range(0, self._num_layers)
            self.check_transfer_config(transfer_config)

    def transfer_layers(self):
        for layer_i, src_layer_index in enumerate(range(self._num_layers)):
            to_transfer = [config for config in self._transfer_configs if src_layer_index in config.src_layer_range]
            if not to_transfer:
                log.info('src_layer %d sends to no destination', src_layer_index)
                continue
            if not self._layer_synchronizer.synchronize_layer(src_layer_index, self._timeout_in_millis):
                log.error(f'Failed to synchronize layer {src_layer_index}')
                for config in to_transfer:
                    # 错误码待添加
                    self._rets[config.dst_cluster_id] = LLMStatusCode.LLM_PARAM_INVALID
                return
            for config in to_transfer:
                dst_layer_index = src_layer_index if config.dst_layer_range is None \
                    else list(config.dst_layer_range)[layer_i]
                ret = self.transfer_layer(src_layer_index, dst_layer_index, config)
                if ret != LLMStatusCode.LLM_SUCCESS:
                    log.error(f'Failed to transfer layer {src_layer_index} to dst_cluster_id={config.dst_cluster_id}')
                    self._rets[config.dst_cluster_id] = ret
                    return
                log.info(f'transfer layer {src_layer_index} to dst_cluster_id={config.dst_cluster_id} success')
                if src_layer_index == config.src_layer_range.stop - 1:
                    self._rets[config.dst_cluster_id] = LLMStatusCode.LLM_SUCCESS
                    log.info(f'transfer all layers to dst_cluster_id={config.dst_cluster_id} finished')
        log.info('transfer all layers finished')

    def transfer_layer(self, src_layer_index: int, dst_layer_idx,
                       transfer_config: Union[TransferConfig, TransferWithCacheKeyConfig]) -> LLMStatusCode:
        if isinstance(transfer_config, TransferConfig):
            dst_layer_index = src_layer_index - transfer_config.src_layer_range.start
            dst_addrs = transfer_config.dst_addrs[dst_layer_index * _NUM_TENSORS_PER_LAYER:
                                                  dst_layer_index * _NUM_TENSORS_PER_LAYER + _NUM_TENSORS_PER_LAYER]
            transfer_config = (self._cache_id, transfer_config.src_batch_index, src_layer_index, dst_addrs,
                               transfer_config.dst_cluster_id, 0, 0, PushType.NO_CACHE_KEY.value, src_layer_index, 2)
        else:
            if isinstance(transfer_config.cache_key, BlocksCacheKey):
                transfer_config = (self._cache_id, 0, src_layer_index, [], transfer_config.cache_key.cluster_id,
                                   transfer_config.cache_key.model_id, 0, PushType.BLOCKS_CACHE_KEY.value,
                                   dst_layer_idx, 2)
            elif isinstance(transfer_config.cache_key, CacheKeyByIdAndIndex):
                transfer_config = (self._cache_id, transfer_config.src_batch_index, src_layer_index, [],
                                   transfer_config.cache_key.cluster_id, transfer_config.cache_key.cache_id,
                                   transfer_config.cache_key.batch_index, PushType.CACHE_KEY_BY_ID.value,
                                   dst_layer_idx, 2)
        block_config = (self._dst_block_memory_size if self._dst_block_memory_size is not None else 0,
                        self._src_block_indices if self._src_block_indices is not None else [],
                        self._dst_block_indices if self._dst_block_indices is not None else [])
        ret = self._transfer_cache_func(TransferCacheJob.task_id, transfer_config, block_config)
        TransferCacheJob.task_id += 1
        return code_2_status(ret)

    def check_transfer_config(self, transfer_config: Union[TransferConfig, TransferWithCacheKeyConfig]):
        if self._src_block_indices:
            raise_if_false(transfer_config.src_batch_index == 0,
                           'Invalid TransferConfig, src_batch_index ({0}) != 0 while src is blocks',
                           transfer_config.src_batch_index)
            raise_if_false(0 <= transfer_config.src_batch_index < self._cache_desc.batch_size,
                           'Invalid TransferConfig, src_batch_index ({0}) out of range: [0, {1})',
                           transfer_config.src_batch_index, self._cache_desc.batch_size)
        raise_if_false(
            0 <= transfer_config.src_layer_range.start < transfer_config.src_layer_range.stop <= self._num_layers,
            "src_layer_range: {0} out of range, src_layer_num = {1}",
            transfer_config.src_layer_range, self._num_layers)
        num_tensors_to_transfer = ((transfer_config.src_layer_range.stop - transfer_config.src_layer_range.start)
                                   * _NUM_TENSORS_PER_LAYER)
        if isinstance(transfer_config, TransferConfig):
            raise_if_false(len(transfer_config.dst_addrs) == num_tensors_to_transfer,
                           "expect {0} dst_addrs, but len(dst_addrs) = {1}, range = {2}",
                           num_tensors_to_transfer, len(transfer_config.dst_addrs), transfer_config.src_layer_range)

    def get_results(self) -> List[LLMStatusCode]:
        rets = []
        for config in self._transfer_configs:
            ret = self._rets.get(config.dst_cluster_id)
            rets.append(ret)
        return rets

    def num_transfer_configs(self):
        return len(self._transfer_configs)


class TransferAsyncThread(Thread):
    def __init__(self, transfer_job: TransferCacheJob, default_err_code=LLMStatusCode.LLM_TIMEOUT):
        super().__init__()
        self._transfer_job = transfer_job
        self._rets = []
        self._default_err_code = default_err_code

    def run(self):
        self._transfer_job.transfer_layers()

    def get_results(self, timeout) -> List[LLMStatusCode]:
        self.join(timeout)
        if self.is_alive():
            return [self._default_err_code] * self._transfer_job.num_transfer_configs()
        return self._transfer_job.get_results()

    def get(self, timeout) -> LLMStatusCode:
        self.join(timeout)
        if self.is_alive():
            return self._default_err_code
        rets = self._transfer_job.get_results()
        for ret in rets:
            if ret != LLMStatusCode.LLM_SUCCESS and ret is not None:
                return ret
        return LLMStatusCode.LLM_SUCCESS


def _check_block_indices(arg_name, arg_value):
    if arg_value is not None:
        check_isinstance(arg_name, arg_value, [list, tuple], int)
        check_list_uint64(arg_name, arg_value)


def transfer_cache_async(params: TransferCacheParameters,
                         layer_synchronizer: LayerSynchronizer,
                         transfer_cache_func,
                         default_error_code=LLMStatusCode.LLM_TIMEOUT,
                         enable_remote_cache=False) -> CacheTask:
    _check_block_indices("dst_block_indices", params.dst_block_indices)
    _check_block_indices("src_block_indices", params.src_block_indices)
    if params.dst_block_memory_size is not None:
        check_uint64("dst_block_memory_size", params.dst_block_memory_size)
    if params.src_block_indices:  # src is blocks
        raise_if_false(params.dst_block_indices, "transfer from blocks to cache is not supported")
        raise_if_false(len(params.src_block_indices) == len(params.dst_block_indices),
                       "num_block_indices mismatches, src_num = {0}, dst_num = {1}",
                       len(params.src_block_indices), len(params.dst_block_indices))
    else:  # src is cache
        try:
            from llm_datadist_v1 import llm_wrapper
            raise_if_true((transfer_cache_func == llm_wrapper.transfer_cache) and (params.dst_block_indices is not None),
                      "transfer from cache to blocks is not supported")
        except ModuleNotFoundError:
            pass
        if params.dst_block_indices:
            raise_if_false(params.dst_block_memory_size is not None,
                           "dst_block_memory_size must be set when transfer from cache to blocks")
    check_isinstance("layer_synchronizer", layer_synchronizer, LayerSynchronizer, allow_none=False)
    if not enable_remote_cache:
        check_isinstance("transfer_configs", params.transfer_configs, [list, tuple], TransferConfig, allow_none=False)
    else:
        check_isinstance("transfer_configs", params.transfer_configs, [list, tuple], TransferWithCacheKeyConfig,
                         'While enable_remote_cache_accessible is True, ', allow_none=False)
    raise_if_false(params.dst_block_indices or params.dst_block_memory_size in (None, 0),
                   "dst_block_memory_size ({0}) is neither None nor 0 while dst is not blocks",
                   params.dst_block_memory_size)
    transfer_job = TransferCacheJob(params, layer_synchronizer, transfer_cache_func)
    transfer_job.init()
    transfer_thread = TransferAsyncThread(transfer_job, default_error_code)
    transfer_thread.start()
    log.info('[transfer_cache_async] async task start')
    cache_task = CacheTask(transfer_thread)
    return cache_task


def layer_range_to_tensor_indices(src_layer_range: range, dst_layer_range: range, tensor_num_per_layer: int = _NUM_TENSORS_PER_LAYER):
    check_isinstance("src_layer_range", src_layer_range, range)
    check_isinstance("dst_layer_range", dst_layer_range, range)
    raise_if_true((src_layer_range is not None) and (src_layer_range.step != 1),
                  'param check failed, src_layer_range step must be 1.')
    raise_if_true((dst_layer_range is not None) and (dst_layer_range.step != 1),
                  'param check failed, dst_layer_range step must be 1.')
    src_layer_indices = [] if src_layer_range is None else list(src_layer_range)
    dst_layer_indices = [] if dst_layer_range is None else list(dst_layer_range)

    # 默认一层有两个tensor
    one_layer_tensor_num = tensor_num_per_layer
    src_tensor_indices = []
    if len(src_layer_indices) != 0:
        raise_if_true(src_layer_indices[0] < 0, "src_layer_range is invalid, the start value:{0} is < 0",
                      src_layer_indices[0])
        check_uint32('src_layer_range', src_layer_indices[0])
        check_uint32('src_layer_range', src_layer_indices[-1])
        src_tensor_start_index = src_layer_indices[0] * one_layer_tensor_num
        for i in range(len(src_layer_indices) * one_layer_tensor_num):
            src_tensor_indices.append(src_tensor_start_index + i)

    dst_tensor_indices = []
    if len(dst_layer_indices) != 0:
        raise_if_true(dst_layer_indices[0] < 0, "dst_layer_range is invalid, the start value:{0} is < 0",
                      dst_layer_indices[0])
        check_uint32('dst_layer_range', dst_layer_indices[0])
        check_uint32('dst_layer_range', dst_layer_indices[-1])
        dst_tensor_start_index = dst_layer_indices[0] * one_layer_tensor_num
        for i in range(len(dst_layer_indices) * one_layer_tensor_num):
            dst_tensor_indices.append(dst_tensor_start_index + i)

    return src_tensor_indices, dst_tensor_indices

def parse_listen_ip_info(listen_ip_info: str) -> (str, int):
    check_isinstance('listen_ip_info', listen_ip_info, [str])
    ip_and_port = listen_ip_info.split(':')
    raise_if_false(len(ip_and_port) == 2,
                    f'llm.listenIpInfo "{listen_ip_info}" is invalid, format should be "ip:port"')
    ip = ip_and_port[0]
    port = int(ip_and_port[1])
    return ip, port