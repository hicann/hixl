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

__all__ = ['CacheDesc', 'CacheKey', 'CacheKeyByIdAndIndex', 'KvCache', 'BlocksCacheKey',
           'Placement', 'CacheTask', 'TransferConfig', 'LayerSynchronizer', 'Cache']

from abc import abstractmethod, ABC
from enum import Enum
from typing import Any, List, Optional, Tuple, Union

from llm_datadist import llm_datadist_wrapper as llm_wrapper, LLMException
from llm_datadist.data_type import DataType
from llm_datadist.status import raise_if_false, LLMStatusCode, raise_if_true
from llm_datadist.utils import log
from llm_datadist.utils.utils import (check_isinstance, check_uint32, check_int32,
                                      check_uint64, check_int64, check_list_int64, check_list_uint64)

_INVALID_ID = 2 ** 64 - 1


class PushType(Enum):
    NO_CACHE_KEY = 0
    BLOCKS_CACHE_KEY = 1
    CACHE_KEY_BY_ID = 2


class RegisterMemStatus(Enum):
    OK = 0
    PREPARING = 1
    FAILED = 2


class Memtype(Enum):
    MEM_TYPE_DEVICE = 0
    MEM_TYPE_HOST = 1


class MemInfo(object):
    def __init__(self, mem_type: Memtype, addr: int, size: int):
        """
        初始化
        Args:
            mem_type: 内存类型
            addr: 数据地址
            size: 数据大小
        """
        check_isinstance("mem_type", mem_type, Memtype)
        check_isinstance("addr", addr, int)
        check_isinstance("size", size, int)
        self._mem_type = mem_type
        self._addr = addr
        self._size = size

    @property
    def mem_type(self):
        return self._mem_type

    @property
    def addr(self):
        return self._addr

    @property
    def size(self):
        return self._size

    def __str__(self):
        return f"MemInfo(mem_type={str(self.mem_type)}, addr={str(self.addr)}, size={str(self.size)})"

    def __repr__(self):
        return self.__str__()


int_to_mem_status_dict = {0: RegisterMemStatus.OK, 1: RegisterMemStatus.PREPARING, 2: RegisterMemStatus.FAILED}


class Placement(Enum):
    HOST = 0
    DEVICE = 1


class CacheDesc(object):
    """
    Cache描述

    Args:
        num_tensors: Cache中tensor的个数, 操作Cache时, 所有tensor会做同样的操作
        shape: tensor shape
        data_type: data type
        placement: 标识kv cache所在的设备
        batch_dim_index (Optional): batch dim的index, 默认为0
        seq_len_dim_index (Optional): seq_len dim的index, 高阶API pull_kv时需要该字段判断是否能够按实际大小拉取
        kv_tensor_format (Optional): kv tensor的data format

    Examples:
        >>> from llm_datadist import CacheDesc
        >>> tensor_num = 80
        >>> tensor_shape = [4, 256]
        >>> tensor_data_type = DataType.DT_FLOAT16
        >>> cache_desc_1 = CacheDesc(tensor_num, tensor_shape, tensor_data_type)
        >>> # 指定 batch_dim_index = 0, seq_len_dim_index = 1
        >>> cache_desc_2 = CacheDesc(tensor_num, tensor_shape, tensor_data_type, Placement.DEVICE, 0, 1)
    """

    def __init__(self,
                 num_tensors: int,
                 shape: Union[Tuple[int], List[int]],
                 data_type: DataType,
                 placement: Placement = Placement.DEVICE,
                 batch_dim_index: int = 0,
                 seq_len_dim_index: int = -1,
                 kv_tensor_format: str = None):
        self._num_tensors = num_tensors
        self._shape = shape
        check_uint32('num_tensors', num_tensors)
        check_isinstance('shape', shape, [tuple, list], int)
        check_isinstance('data_type', data_type, DataType)
        check_isinstance('placement', placement, Placement)
        check_isinstance('batch_dim_index', batch_dim_index, int)
        check_int32('seq_len_dim_index', seq_len_dim_index)
        check_isinstance('kv_tensor_format', kv_tensor_format, str)
        raise_if_false(0 <= batch_dim_index < len(shape),
                       "batch_dim_index {0} out of range, [0, {1})",
                       batch_dim_index, len(shape))
        raise_if_false(seq_len_dim_index == -1 or 0 <= seq_len_dim_index < len(shape),
                       "seq_len_dim_index {0} is invalid, should be -1 or in range [0, {1})",
                       seq_len_dim_index, len(shape))
        check_list_int64("shape", shape)
        self._data_type = data_type
        self._batch_dim = batch_dim_index
        self._batch_size = shape[batch_dim_index]
        self._seq_len_dim_index = seq_len_dim_index
        self._size = -1
        self._kv_tensor_format = kv_tensor_format
        self._placement = placement
        self._is_blocks = False

    def __repr__(self):
        return (f'CacheDesc(num_tensors={self.num_tensors}, '
                f'shape={self.shape}, '
                f'data_type={self.data_type}, '
                f'placement={self.placement}, '
                f'batch_dim_index={self.batch_dim}, '
                f'seq_len_dim_index={self._seq_len_dim_index},'
                f'kv_tensor_format={self.kv_tensor_format})')

    @property
    def num_tensors(self) -> int:
        return self._num_tensors

    @property
    def shape(self) -> List[int]:
        return self._shape

    def update_dim(self, dim_index: int, dim_value: int) -> None:
        self._shape[dim_index] = dim_value
        self._size = -1

    @property
    def data_type(self) -> DataType:
        return self._data_type

    @property
    def batch_dim(self) -> int:
        return self._batch_dim

    @property
    def batch_size(self) -> int:
        return self._batch_size

    @property
    def seq_len_dim_index(self) -> int:
        return self._seq_len_dim_index

    @property
    def size(self) -> int:
        if self._size == -1:
            self._size = llm_wrapper.calc_tensor_size(self.shape, self._data_type.value)
            if self._size < 0:
                raise LLMException(f'Failed to calc tensor size, shape = {self.shape}, data_type = {self.data_type}')
        return self._size

    @property
    def kv_tensor_format(self) -> str:
        return self._kv_tensor_format

    @property
    def placement(self) -> Placement:
        return self._placement


class Cache(object):
    """
    Cache, 由CacheManager.register_cache/allocate_cache/allocate_blocks_cache创建，用户不应直接构造
    """

    def __init__(self,
                 cache_id: int,
                 cache_desc: CacheDesc,
                 tensor_addrs: List[int],
                 cache_manager,
                 is_registered,
                 is_blocks_cache
                 ):
        self._cache_id = cache_id
        check_int64("cache_id", cache_id)
        self._cache_desc = cache_desc
        self._tensor_addrs = tensor_addrs
        self._valid = True
        self._cache_manager = cache_manager
        self._is_registered = is_registered
        self._is_blocks_cache = is_blocks_cache

    @property
    def cache_id(self) -> int:
        return self._cache_id

    @property
    def cache_desc(self) -> CacheDesc:
        return self._cache_desc

    @classmethod
    def create_cpu_cache(cls, cache_desc: CacheDesc, addrs: List[int]):
        check_isinstance('cache_desc', cache_desc, CacheDesc)
        check_isinstance('addrs', addrs, list, int)
        raise_if_false(cache_desc.placement == Placement.HOST, "cache_desc placement must be HOST")
        raise_if_false(cache_desc.num_tensors == len(addrs),
                       f"cache_desc num_tensors:{cache_desc.num_tensors} not equal addrs num:{len(addrs)}")
        return cls(-1, cache_desc, addrs, None, False, True)

    @property
    def tensor_addrs(self) -> List[int]:
        return self._tensor_addrs

    @property
    def is_blocks_cache(self):
        return self._is_blocks_cache

    def __str__(self):
        return (f'Cache(cache_id = {self._cache_id}, device_tensor_addrs = {len(self._tensor_addrs)}, '
                f'is_blocks_cache = {self._is_blocks_cache})')

    def __repr__(self):
        return self.__str__()


class BlocksCacheKey(object):
    """
    BlocksCacheKey, PROMPT allocate blocks cache时用于建立索引, DECODER pull_kv时作为索引传入

    Args:
        prompt_cluster_id or cluster_id: remote cluster id, 用于pull_kv时需要准确填写, allocate_cache时会忽略该字段
        model_id (Optional): model id, 默认为0, 在投机等同时加载多个model的场景需要按需设置

    Raises:
        TypeError: if `prompt_cluster_id` or `cluster_id` is not int
        TypeError: if `model_id` is not int

    Example:
        >>> from llm_datadist import BlocksCacheKey
        >>> cluster_id = 1
        >>> # model_id取默认值
        >>> cache_key = BlocksCacheKey(cluster_id)
        >>> # 指定model_id
        >>> model_id = 1
        >>> cache_key_1 = BlocksCacheKey(cluster_id, model_id)
    """

    def __init__(self, *args, **kwargs):
        raise_if_false((len(args) + len(kwargs)) <= 2, "Param num is over limit")
        if len(args) > 0:
            kwargs["cluster_id"] = args[0]
        if len(args) > 1:
            kwargs["model_id"] = args[1]
        raise_if_false("prompt_cluster_id" in kwargs or "cluster_id" in kwargs,
                       "Param prompt_cluster_id or cluster_id is required")
        valid_keys = ["prompt_cluster_id", "cluster_id", "model_id"]
        for k in kwargs.keys():
            raise_if_false(k in valid_keys, "Unsupported param:{}", k)
        self._cluster_id = kwargs["cluster_id"] if "cluster_id" in kwargs else kwargs["prompt_cluster_id"]
        self._cluster_id_key = "cluster_id" if "cluster_id" in kwargs else "prompt_cluster_id"
        self._model_id = kwargs["model_id"] if "model_id" in kwargs else 0
        check_isinstance("cluster_id", self._cluster_id, int, extra_fmt="The first param ")
        check_isinstance("model_id", self._model_id, int, extra_fmt="The second param ")

    @property
    def prompt_cluster_id(self) -> int:
        return self._cluster_id

    @property
    def cluster_id(self) -> int:
        return self._cluster_id

    @property
    def model_id(self) -> int:
        return self._model_id

    def __repr__(self):
        return (f'BlocksCacheKey({self._cluster_id_key}={self.cluster_id}, '
                f'model_id={self.model_id})')


class CacheKey(object):
    """
    CacheKey, REMOTE allocate_cache时用于建立索引, LOCAL pull_cache时作为索引传入

    Args:
        prompt_cluster_id or cluster_id: remote cluster id, 用于pull_kv时需要准确填写, allocate_cache时会忽略该字段
        req_id: request id
        model_id (Optional): model id, 默认为0, 在投机等同时加载多个model的场景需要按需设置
        prefix_id (Optional): prefix id, 默认为2 ** 64 - 1

    Raises:
        TypeError: if `prompt_cluster_id` or `cluster_id` is not int
        TypeError: if `req_id` is not int
        TypeError: if `model_id` is not int
        TypeError: if `prefix_id` is not int

    Example:
        >>> from llm_datadist import CacheKey
        >>> cluster_id = 1
        >>> request_id = 1
        >>> # model_id取默认值
        >>> cache_key = CacheKey(cluster_id, request_id)
        >>> # 指定model_id
        >>> cache_key_1 = CacheKey(cluster_id, request_id, 2)
    """

    def __init__(self, *args, **kwargs):
        raise_if_false((len(args) + len(kwargs)) <= 4, "Param num is over limit")
        if len(args) > 0:
            kwargs["cluster_id"] = args[0]
        if len(args) > 1:
            kwargs["req_id"] = args[1]
        if len(args) > 2:
            kwargs["model_id"] = args[2]
        if len(args) > 3:
            kwargs["prefix_id"] = args[3]
        raise_if_false("prompt_cluster_id" in kwargs or "cluster_id" in kwargs,
                       "Param prompt_cluster_id or cluster_id is required")
        raise_if_false("req_id" in kwargs or "req_id" in kwargs,
                       "Param req_id is required")
        valid_keys = ["prompt_cluster_id", "cluster_id", "req_id", "model_id", "prefix_id"]
        for k in kwargs.keys():
            raise_if_false(k in valid_keys, "Unsupported param:{}", k)
        self._cluster_id = kwargs["cluster_id"] if "cluster_id" in kwargs else kwargs["prompt_cluster_id"]
        self._req_id = kwargs["req_id"]
        self._model_id = kwargs["model_id"] if "model_id" in kwargs else 0
        self._prefix_id = kwargs["prefix_id"] if "prefix_id" in kwargs else _INVALID_ID
        check_uint64("cluster_id", self._cluster_id)
        check_uint64("req_id", self._req_id)
        check_uint64("model_id", self._model_id)
        check_uint64("prefix_id", self._prefix_id)

    @property
    def prompt_cluster_id(self) -> int:
        return self._cluster_id

    @property
    def cluster_id(self) -> int:
        return self._cluster_id

    @property
    def req_id(self) -> int:
        return self._req_id

    @property
    def prefix_id(self) -> int:
        return self._prefix_id

    @property
    def model_id(self) -> int:
        return self._model_id

    def __repr__(self):
        return (f'CacheKey(cluster_id={self.cluster_id}, '
                f'req_id={self.req_id}, '
                f'prefix_id={self.prefix_id}, '
                f'model_id={self.model_id})')


class CacheKeyByIdAndIndex(object):
    """
    索引Prompt中kv cache的单个batch index, 用于Decoder pull_cache时作为索引传入

    Args:
        cluster_id: kv所在的prompt的cluster_id
        cache_id: kv cache的id
        batch_index (Optional): batch index, 默认为0, 在投机等同时加载多个model的场景需要按需设置

    Raises:
        TypeError: if `cluster_id` is not int
        TypeError: if `cache_id` is not int
        TypeError: if `batch_index` is not int
    """

    def __init__(self, cluster_id: int, cache_id: int, batch_index=0):
        check_uint64("cluster_id", cluster_id)
        check_int64("cache_id", cache_id)
        check_uint32("batch_index", batch_index)
        self._prompt_cluster_id = cluster_id
        self._prompt_cache_id = cache_id
        self._prompt_batch_index = batch_index
        self._req_id = _INVALID_ID

    @property
    def cluster_id(self) -> int:
        return self._prompt_cluster_id

    @property
    def cache_id(self) -> int:
        return self._prompt_cache_id

    @property
    def batch_index(self) -> int:
        return self._prompt_batch_index

    def __repr__(self):
        return (f'CacheKeyByIdAndIndex(cluster_id={self.cluster_id}, '
                f'cache_id={self.cache_id}, '
                f'batch_index={self.batch_index})')


class KvCache(object):
    """
    Kv Cache, 由KvCacheManager.allocate_cache创建，用户不应直接构造
    """

    def __init__(self,
                 cache_id: int,
                 cache_desc: CacheDesc,
                 per_device_tensor_addrs: List[List[int]],
                 kv_cache_manager):
        self._cache_id = cache_id
        check_int64("cache_id", cache_id)
        self._cache_desc = cache_desc
        self._per_device_tensor_addrs = per_device_tensor_addrs
        self._valid = True
        self._kv_cache_manager = kv_cache_manager

    def __del__(self):
        # 保底释放kv cache, 但更推荐主动通过调用kv_cache_manager.deallocate_cache来释放kv cache，而不应该遗留到此处自动释放
        if self._valid and self._kv_cache_manager is not None and self._kv_cache_manager.is_initialized():
            log.info('auto deallocate kv cache: %s', self.cache_id)
            self._kv_cache_manager.deallocate_cache(self)

    @property
    def cache_id(self) -> int:
        return self._cache_id

    @property
    def cache_desc(self) -> CacheDesc:
        return self._cache_desc

    @property
    def per_device_tensor_addrs(self) -> List[List[int]]:
        return self._per_device_tensor_addrs

    def __str__(self):
        return f'KvCache(cache_id = {self._cache_id}, num_devices = {len(self._per_device_tensor_addrs)})'

    def __repr__(self):
        return self.__str__()

    @classmethod
    def create_cpu_cache(cls, cache_desc: CacheDesc, addrs: Union[List[int], List[List[int]]]):
        check_isinstance('cache_desc', cache_desc, CacheDesc)
        raise_if_false(cache_desc.placement == Placement.HOST, "cache_desc placement must be HOST")
        check_isinstance('addrs', addrs, list)
        raise_if_false(len(addrs) > 0, "addrs length should be bigger than zero.")
        last_type = type(addrs[0])
        for addr in addrs:
            check_isinstance('the internal element of addrs', addr, [list, int])
            if isinstance(addr, list):
                check_isinstance('the internal element of addrs', addr, list, int)
                raise_if_false(cache_desc.num_tensors == len(addr),
                               f"cache_desc num_tensors:{cache_desc.num_tensors} "
                               f"should be equal to size of the internal element of addrs:{len(addr)}")
            raise_if_false(isinstance(addr, last_type),
                           'the type of the internal element of addrs should be consistent.')
            last_type = type(addr)
        if last_type == int:
            raise_if_false(cache_desc.num_tensors == len(addrs),
                           f"cache_desc num_tensors:{cache_desc.num_tensors} "
                           f"should be equal to size of addrs:{len(addrs)}")
        return cls(-1, cache_desc, [addrs] if last_type == int else addrs, None)

class LayerSynchronizer(ABC):
    @abstractmethod
    def synchronize_layer(self, layer_index: int, timeout_in_millis: Optional[int]) -> bool:
        """
        阻塞等待指定层计算完成

        Args:
            layer_index(int): 要同步的layer的index
            timeout_in_millis(Optional[int]): 超时时间, 不配置则不超时

        Returns:
            True: 同步成功, False: 同步失败
        """


def check_layer_range(name, layer_range: Optional[range], allow_none=True):
    if allow_none and layer_range is None:
        return
    check_isinstance(name, layer_range, range, allow_none=allow_none)
    raise_if_false(layer_range.step == 1,
                   f'check {name}.step == 1 failed')
    raise_if_false(0 <= layer_range.start < layer_range.stop,
                   'check 0 <= range.start < range.stop failed, {0} = {1}', name, layer_range)


class TransferWithCacheKeyConfig:
    def __init__(self,
                 cache_key: Union[BlocksCacheKey, CacheKeyByIdAndIndex],
                 src_layer_range: range = None,
                 dst_layer_range: range = None,
                 src_batch_index: int = 0):
        check_isinstance('cache_key', cache_key, [BlocksCacheKey, CacheKeyByIdAndIndex], allow_none=False)
        self._cache_key = cache_key
        check_layer_range('src_layer_range', src_layer_range, allow_none=False)
        self._src_layer_range = src_layer_range
        check_layer_range('dst_layer_range', dst_layer_range, allow_none=False)
        self._dst_layer_range = dst_layer_range
        raise_if_false((src_layer_range.stop - src_layer_range.start) == (dst_layer_range.stop - src_layer_range.start),
                       f'src_layer_range size shoulde be equal to dst_layer_range size')
        check_uint32('src_batch_index', src_batch_index)

        raise_if_true(isinstance(cache_key, BlocksCacheKey) and src_batch_index != 0,
                      "src_batch_index shoulde be 0 when cache_key is BlocksCacheKey.")
        self._src_batch_index = src_batch_index
        self.dst_cluster_id = cache_key.cluster_id

    def __repr__(self) -> str:
        return (f"TransferWithCacheKeyConfig(cache_key={self._cache_key},"
                f" src_layer_range={self._src_layer_range},"
                f" dst_layer_range={self._dst_layer_range},"
                f" src_batch_index={self._src_batch_index})")

    @property
    def cache_key(self):
        return self._cache_key

    @cache_key.setter
    def cache_key(self, cache_key: Union[BlocksCacheKey, CacheKeyByIdAndIndex]) -> None:
        check_isinstance('cache_key', cache_key, [BlocksCacheKey, CacheKeyByIdAndIndex])
        self._cache_key = cache_key

    @property
    def src_layer_range(self) -> Optional[range]:
        return self._src_layer_range

    @src_layer_range.setter
    def src_layer_range(self, src_layer_range: Optional[range]) -> None:
        check_layer_range('src_layer_range', src_layer_range)
        self._src_layer_range = src_layer_range

    @property
    def dst_layer_range(self) -> Optional[range]:
        return self._dst_layer_range

    @dst_layer_range.setter
    def dst_layer_range(self, dst_layer_range: Optional[range]) -> None:
        check_layer_range('dst_layer_range', dst_layer_range)
        self._dst_layer_range = dst_layer_range

    @property
    def src_batch_index(self) -> int:
        return self._src_batch_index

    @src_batch_index.setter
    def src_batch_index(self, src_batch_index: int) -> None:
        raise_if_true(isinstance(self.cache_key, BlocksCacheKey) and src_batch_index != 0,
                      "src_batch_index shoulde be 0 when cache_key is BlocksCacheKey.")
        self._check_src_batch_index(src_batch_index)
        self._src_batch_index = src_batch_index

    @staticmethod
    def _check_src_batch_index(src_batch_index: int):
        check_uint32("src_batch_index", src_batch_index)


class TransferConfig:
    def __init__(self,
                 dst_cluster_id: int,
                 dst_addrs: List[int],
                 src_layer_range: Optional[range] = None,
                 src_batch_index: int = 0):
        self._check_dst_cluster_id(dst_cluster_id)
        self._check_dst_addrs(dst_addrs)
        check_layer_range('src_layer_range', src_layer_range)
        self._check_src_batch_index(src_batch_index)
        self._dst_cluster_id = dst_cluster_id
        self._dst_addrs = dst_addrs
        self._src_layer_range = src_layer_range
        self._src_batch_index = src_batch_index
        self.dst_layer_range = None

    def __repr__(self) -> str:
        return (f"TransferConfig(src_cluster_id={self._dst_cluster_id},"
                f" dst_addrs={self._dst_addrs},"
                f" src_layer_range={self._src_layer_range},"
                f" src_batch_index={self._src_batch_index})")

    def __str__(self):
        return self.__repr__()

    @property
    def dst_cluster_id(self) -> int:
        return self._dst_cluster_id

    @property
    def dst_addrs(self) -> List[int]:
        return self._dst_addrs

    @property
    def src_layer_range(self) -> Optional[range]:
        return self._src_layer_range

    @property
    def src_batch_index(self) -> int:
        return self._src_batch_index

    @dst_cluster_id.setter
    def dst_cluster_id(self, cluster_id: int) -> None:
        self._check_dst_cluster_id(cluster_id)
        self._dst_cluster_id = cluster_id

    @dst_addrs.setter
    def dst_addrs(self, dst_addrs: List[int]) -> None:
        self._check_dst_addrs(dst_addrs)
        self._dst_addrs = dst_addrs

    @src_layer_range.setter
    def src_layer_range(self, layer_range: Optional[range]) -> None:
        check_layer_range('src_layer_range', layer_range)
        self._src_layer_range = layer_range

    @src_batch_index.setter
    def src_batch_index(self, batch_index: int) -> None:
        self._check_src_batch_index(batch_index)
        self._src_batch_index = batch_index

    @staticmethod
    def _check_dst_cluster_id(dst_cluster_id: int):
        check_uint64("dst_cluster_id", dst_cluster_id)

    @staticmethod
    def _check_dst_addrs(dst_addrs: List[int]):
        check_isinstance("dst_addrs", dst_addrs, [list, tuple], int, allow_none=False)
        check_list_uint64("dst_addrs", dst_addrs)

    @staticmethod
    def _check_src_batch_index(src_batch_index: int):
        check_uint32("src_batch_index", src_batch_index)


class CacheTask:
    def __init__(self, transfer_async_task):
        self._transfer_async_task = transfer_async_task

    def synchronize(self, timeout_in_millis: Optional[int] = None) -> LLMStatusCode:
        timeout = None
        if timeout_in_millis is not None:
            check_uint32('timeout_in_millis', timeout_in_millis)
            timeout = timeout_in_millis / 1000
        return self._transfer_async_task.get(timeout)

    def get_results(self, timeout_in_millis: Optional[int] = None) -> List[LLMStatusCode]:
        timeout = None
        if timeout_in_millis is not None:
            check_uint32('timeout_in_millis', timeout_in_millis)
            timeout = timeout_in_millis / 1000
        return self._transfer_async_task.get_results(timeout)
