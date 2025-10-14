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

from typing import List, Optional, Tuple, Union, Dict

from llm_datadist.status import handle_llm_status, raise_if_false, raise_if_true
from llm_datadist.utils import log
from llm_datadist.utils.utils import check_isinstance, check_dict, check_uint32, check_int64, check_uint64, check_uint8
from llm_datadist.v2.llm_types import CacheDesc, Cache, CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey, Placement, \
    TransferConfig, CacheTask, LayerSynchronizer, TransferWithCacheKeyConfig, PushType, check_layer_range, MemInfo, \
    Memtype
from llm_datadist.v2.llm_utils import (
    pack_cache_desc, pack_cache_key, pack_block_cache_key, pack_mem_info, \
    pack_cache_key_by_id, transfer_cache_async, TransferCacheParameters, layer_range_to_tensor_indices, \
    is_invalid_id, is_valid_id
)

_NUM_TENSORS_PER_LAYER = 2
_INVALID_ID = 2 ** 64 - 1


class CacheManager(object):
    """
    提供了一组Cache的操作函数, 在LLMEngine初始化后通过LLMEngine获取实例

    Examples:
        >>> from llm_datadist import LLMRole, LLMDataDist
        >>> # init LLMDataDist
        >>> cluster_id = 0
        >>> datadist = LLMDataDist(LLMRole.PROMPT, cluster_id)
        >>> engine_options = {}
        >>> datadist.init(engine_options)
        >>> # get CacheManager
        >>> cache_manager = datadist.cache_manager
    """

    def __init__(self, llm_datadist, options) -> None:
        self._llm_datadist = llm_datadist
        self._initialized = True
        self._is_call_linked = False
        self._enabled_mem_pool = "llm.MemPoolConfig" in options and len(options["llm.MemPoolConfig"]) > 0
        self._enable_host_mem_pool = "llm.HostMemPoolConfig" in options and len(options["llm.HostMemPoolConfig"]) > 0
        self._enable_remote_cache_accessible = options.get("llm.EnableRemoteCacheAccessible", '0') == '1'
        self._enable_local_comm_res = "llm.LocalCommRes" in options

    @staticmethod
    def check_cache_key(cache_key: CacheKey) -> None:
        raise_if_true(is_invalid_id(cache_key.req_id) and is_invalid_id(cache_key.prefix_id),
                      f'one of req id and prefix id should contain valid value:[0, 2**64-1), '
                      f'req id:{cache_key.req_id},prefix id{cache_key.prefix_id}.')
        raise_if_true(is_valid_id(cache_key.req_id) and is_valid_id(cache_key.prefix_id),
                      'only one of req id and prefix id should contain valid value:[0, 2**64-1), '
                      f'req id:{cache_key.req_id}, prefix id{cache_key.prefix_id}.')

    def allocate_blocks_cache(self,
                              cache_desc: CacheDesc,
                              blocks_cache_key: Optional[BlocksCacheKey] = None) -> Cache:
        """
        分配Blocks Cache, cache分配成功后
        需通过deallocate_blocks_cache释放

        Args:
            cache_desc: Cache描述
            blocks_cache_key(Optional): 仅当LLMRole为PROMPT时可设置, 用于在DECODER拉取KV
        Returns:
            Cache

        """
        check_isinstance("cache_desc", cache_desc, CacheDesc)
        check_isinstance("blocks_cache_key", blocks_cache_key, BlocksCacheKey)
        raise_if_false(cache_desc.num_tensors > 0, "num_tensors should be bigger than zero.")
        raise_if_false((self._enabled_mem_pool and cache_desc.placement == Placement.DEVICE) or (
                    self._enable_host_mem_pool and cache_desc.placement == Placement.HOST),
                       'you should config memory pool firstly and then set placement that matches it.')
        wrapped_cache_keys = [pack_block_cache_key(blocks_cache_key)] if blocks_cache_key is not None else []
        cache_desc._is_blocks = True
        ret, cache_id_and_addr = self._llm_datadist.allocate_cache_v2(pack_cache_desc(cache_desc),
                                                                      wrapped_cache_keys)
        handle_llm_status(ret, '[allocate_blocks_cache]', f'cache_desc = {cache_desc}')
        cache = Cache(cache_id_and_addr[0], cache_desc, cache_id_and_addr[1][0], self, False, True)
        log.info('[allocate_blocks_cache] success, cache_id = %d', cache.cache_id)
        return cache

    def allocate_cache(self,
                       cache_desc: CacheDesc,
                       cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = ()) -> Cache:
        """
        分配Cache, cache分配成功后, 会同时被cache_id与cache_keys(如果传了)引用, 只有当这些引用都解除后, cache所占用的资源才会实际释放
        cache_id的引用需通过deallocate_cache解除
        cache_keys的引用则可以通过以下2种方式解除:
            1. DECODER调用pull_kv接口, pull_kv成功后解除
            2. PROMPT调用remove_cache_key接口

        Args:
            cache_desc: Cache描述
            cache_keys(Optional): 仅当LLMRole为PROMPT时可设置, 用于在DECODER拉取KV
                如果Cache的batch size > 1, 则需要提供相同数量的CacheKey, 分别引用一组kv tensor
                如果当次推理的batch未占用满，即存在无效batch index，则需要插入特殊的CacheKey(req_id = UINT64_MAX)占位,
                如果空闲的batch_index在末尾，则可以省略
        Returns:
            Cache
        """
        check_isinstance("cache_desc", cache_desc, CacheDesc)
        check_isinstance("cache_keys", cache_keys, [list, tuple], CacheKey)
        raise_if_false(cache_desc.num_tensors > 0, "num_tensors should be bigger than zero.")
        raise_if_false((self._enabled_mem_pool and cache_desc.placement == Placement.DEVICE) or (
                self._enable_host_mem_pool and cache_desc.placement == Placement.HOST),
                       'you should config memory pool firstly and then set placement that matches it.')
        log.info('[allocate_cache] start, cache_desc = %s, cache_keys = %s', cache_desc, cache_keys)
        wrapped_cache_keys = [pack_cache_key(cache_key) for cache_key in cache_keys]
        ret, cache_id_and_addr = self._llm_datadist.allocate_cache_v2(pack_cache_desc(cache_desc),
                                                                      wrapped_cache_keys)
        handle_llm_status(ret, '[allocate_cache]', f'cache_desc = {cache_desc}')
        cache = Cache(cache_id_and_addr[0], cache_desc, cache_id_and_addr[1][0], self, False, False)
        log.info('[allocate_cache] success, cache_id = %d', cache.cache_id)
        return cache

    def copy_blocks(self, cache: Cache, copy_block_info: Dict[int, List[int]]):
        """
        Args:
            cache: 目标缓存
            copy_block_info: 拷贝信息, (int, List[int])代表(原始block index, 目标block index)
        """
        check_isinstance("cache", cache, Cache)
        check_isinstance("copy_block_info", copy_block_info, dict)
        check_dict("copy_block_info", copy_block_info, int, list, int)
        raise_if_false(cache.is_blocks_cache, 'param check failed, cache should be blocks cache.')
        copy_block_infos = [(src_block, dst_block) for src_block, dst_blocks in copy_block_info.items()
                            for dst_block in dst_blocks]
        param = (cache.cache_id, cache.cache_id, 0, 0, 0, -1, _INVALID_ID, copy_block_infos)
        ret = self._llm_datadist.copy_cache_v2(param)
        handle_llm_status(ret, '[copy_blocks]', "cache id is:%s" % cache.cache_id)
        log.info('[copy_blocks] success')

    def copy_cache(self,
                   dst: Cache,
                   src: Cache,
                   dst_batch_index: int = 0,
                   src_batch_index: int = 0,
                   offset: int = 0,
                   size: int = -1,
                   req_id: Optional[int] = None) -> None:
        """
        拷贝KV, src/dst的CacheDesc需要匹配

        Args:
            dst: 目标Cache
            src: 源Cache
            dst_batch_index: 目标Cache的batch_index
            src_batch_index: 源Cache的batch_index
            offset: 每个tensor的偏移
            size: 每个tensor拷贝的大小
            req_id(Optional): 本次操作关联的req_id, 仅用于维测
        """
        check_isinstance("dst", dst, Cache)
        check_isinstance("src", src, Cache)
        check_uint32("dst_batch_index", dst_batch_index)
        check_uint32("src_batch_index", src_batch_index)
        check_uint64("offset", offset)
        check_int64("size", size)
        raise_if_true(src.is_blocks_cache, 'param check failed, src cache can not be blocks cache.')
        raise_if_true(dst.is_blocks_cache, 'param check failed, dst cache can not be blocks cache.')
        raise_if_false(size == -1 or size > 0,
                       '[copy_cache] param check failed, size ({0}) is invalid, should be = -1 or > 0', size)
        user_param = (dst.cache_id, src.cache_id, dst_batch_index, src_batch_index, offset, size, req_id, [])
        log.info('[copy_cache] start, param = %s', user_param)
        if req_id is not None:
            check_uint64("req_id", req_id)
        else:
            req_id = _INVALID_ID
        param = (dst.cache_id, src.cache_id, dst_batch_index, src_batch_index, offset, size, req_id, [])
        ret = self._llm_datadist.copy_cache_v2(param)
        handle_llm_status(ret, '[copy_cache]', f'param = {param}')
        log.info('[copy_cache] success')

    def deallocate_blocks_cache(self, cache: Cache) -> None:
        check_isinstance("cache", cache, Cache)
        raise_if_false(cache.is_blocks_cache, 'param check failed, cache should be blocks cache.')
        log.info('[deallocate_blocks_cache] start, cache_id = %d', cache.cache_id)
        ret = self._llm_datadist.deallocate_cache_v2(cache.cache_id)
        handle_llm_status(ret, '[deallocate_blocks_cache]', f'cache_id = {cache.cache_id}')
        cache._tensor_addrs = []
        cache._valid = False
        log.info('[deallocate_blocks_cache] success')

    def deallocate_cache(self, cache: Cache) -> None:
        """
        释放Cache, 如果该Cache在Allocate时关联了CacheKey, 则实际的释放会延后到所有的CacheKey被拉取或执行了remove_cache_key
        释放之后，不应再对该Cache做任何操作

        Args:
            cache: Cache

        Examples:
            see examples of allocate_cache
        """
        check_isinstance("cache", cache, Cache)
        raise_if_true(cache.is_blocks_cache, 'param check failed, cache can not be blocks cache.')
        log.info('[deallocate_cache] start, cache_id = %d', cache.cache_id)
        ret = self._llm_datadist.deallocate_cache_v2(cache.cache_id)
        handle_llm_status(ret, '[deallocate_cache]', f'cache_id = {cache.cache_id}')
        cache._tensor_addrs = []
        cache._valid = False
        log.info('[deallocate_cache] success')

    def pull_blocks(self, src_cache_key: Union[CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey],
                    dst_cache: Cache, src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
                    dst_blocks: Union[Tuple[int], List[int]] = (),
                    **kwargs):
        """
        PA模式下拉取KV
        Args:
            src_cache_key: prompt缓存key
            dst_cache: decoder目标缓存
            src_blocks: prompt block列表
            dst_blocks: decoder block列表
            **kwargs:
                src_layer_range: 源层范围
                dst_layer_range: 目的层范围
                tensor_num_per_layer: 每层tensor数量
        """
        src_layer_range = kwargs.get("src_layer_range")
        dst_layer_range = kwargs.get("dst_layer_range")
        tensor_num_per_layer = kwargs.get("tensor_num_per_layer", _NUM_TENSORS_PER_LAYER)
        check_isinstance("src_cache_key", src_cache_key, [CacheKey, CacheKeyByIdAndIndex, BlocksCacheKey])
        check_isinstance("dst_cache", dst_cache, Cache)
        check_isinstance("src_blocks", src_blocks, [list, tuple], int)
        check_isinstance("dst_blocks", dst_blocks, [list, tuple], int)
        raise_if_false(dst_cache.is_blocks_cache, 'param check failed, dst_cache should be blocks cache.')
        raise_if_false(len(dst_blocks) > 0, "dst_blocks can not be empty.")
        check_uint32("tensor_num_per_layer", tensor_num_per_layer)
        raise_if_false(tensor_num_per_layer > 0,
                       '[pull_blocks] param check failed, tensor_num_per_layer ({0}) is invalid, should [1, {1}]',
                       tensor_num_per_layer, dst_cache.cache_desc.num_tensors)
        if isinstance(src_cache_key, BlocksCacheKey):
            raise_if_false(len(src_blocks) > 0, "src_blocks can not be empty.")
            packed_cache_key = pack_block_cache_key(src_cache_key)
        else:
            if isinstance(src_cache_key, CacheKey):
                CacheManager.check_cache_key(src_cache_key)
            raise_if_false(len(src_blocks) == 0,
                           "src_blocks should be empty when src_cache_key is not instance of BlocksCacheKey.")
            packed_cache_key = pack_cache_key(src_cache_key) if isinstance(src_cache_key, CacheKey) \
                else pack_cache_key_by_id(src_cache_key)

        log.info('[pull_blocks] start, target cache_id = %d, cache_key = %s, '
                 'src_layer_range = %s, dst_layer_range = %s, tensor_num_per_layer = %d',
                 dst_cache.cache_id, src_cache_key, src_layer_range, dst_layer_range, tensor_num_per_layer)
        src_tensor_indices, dst_tensor_indices = layer_range_to_tensor_indices(src_layer_range, dst_layer_range,
                                                                               tensor_num_per_layer)
        param = (-1, 0, src_blocks, dst_blocks, src_tensor_indices, dst_tensor_indices, -1, -1, tensor_num_per_layer)
        ret = self._llm_datadist.pull_cache_v2(dst_cache.cache_id, packed_cache_key, param)
        handle_llm_status(ret, '[pull_blocks]', f'src_cache_key = {src_cache_key}')
        log.info('[pull_blocks] success')

    def pull_cache(self,
                   cache_key: Union[CacheKey, CacheKeyByIdAndIndex],
                   cache: Cache,
                   batch_index: int = 0,
                   size: int = -1,
                   **kwargs) -> None:
        """
        Args:
            cache_key: CacheKey或CacheKeyByIdAndIndex
            cache: 目标Cache
            batch_index: batch index
            size: 拉取的tensor大小, -1表示拉取全部大小
            **kwargs:
                src_layer_range: 源层范围
                dst_layer_range: 目的层范围
                tensor_num_per_layer: 每层tensor数量
        """
        if self._enable_remote_cache_accessible:
            check_isinstance("cache_key", cache_key, CacheKeyByIdAndIndex)
        else:
            check_isinstance("cache_key", cache_key, [CacheKey, CacheKeyByIdAndIndex])
        src_layer_range = kwargs.get("src_layer_range")
        dst_layer_range = kwargs.get("dst_layer_range")
        tensor_num_per_layer = kwargs.get("tensor_num_per_layer", _NUM_TENSORS_PER_LAYER)
        check_isinstance("cache", cache, Cache)
        check_uint32("batch_index", batch_index)
        check_int64("size", size)
        raise_if_true(cache.is_blocks_cache, 'param check failed, cache can not be blocks cache.')
        raise_if_false(size == -1 or size > 0,
                       '[pull_cache] param check failed, size ({0}) is invalid, should be = -1 or > 0', size)
        check_uint32("tensor_num_per_layer", tensor_num_per_layer)
        raise_if_false(tensor_num_per_layer > 0,
                       '[pull_cache] param check failed, tensor_num_per_layer ({0}) is invalid, should [1, {1}]',
                       tensor_num_per_layer, cache.cache_desc.num_tensors)
        if isinstance(cache_key, CacheKey):
            CacheManager.check_cache_key(cache_key)
            packed_cache_key = pack_cache_key(cache_key)
        else:
            packed_cache_key = pack_cache_key_by_id(cache_key)
    
        log.info('[pull_cache] start, cache_id = %d, batch_index = %d, size = %d, cache_key = %s, '
                 'src_layer_range = %s, dst_layer_range = %s, tensor_num_per_layer = %d',
                 cache.cache_id, batch_index, size, cache_key, src_layer_range, dst_layer_range, tensor_num_per_layer)
        src_tensor_indices, dst_tensor_indices = layer_range_to_tensor_indices(src_layer_range, dst_layer_range,
                                                                               tensor_num_per_layer)
        param = (size, batch_index, [], [], src_tensor_indices, dst_tensor_indices, -1, -1, tensor_num_per_layer)
        ret = self._llm_datadist.pull_cache_v2(cache.cache_id, packed_cache_key, param)
        handle_llm_status(ret, '[pull_cache]', f'cache_key = {cache_key}')
        log.info('[pull_cache] success')

    def register_cache(self, cache_desc: CacheDesc, addrs: List[int],
                       cache_keys: Union[Tuple[CacheKey], List[CacheKey]] = (),
                       remote_accessible: Optional[bool] = None) -> Cache:
        """
        Args:
            cache_desc: Cache描述
            addrs: device addrs of cache tensors
            cache_keys: cache keys
            remote_accessible(Optional): whether remote cache is accessible
        """
        check_isinstance("cache_desc", cache_desc, CacheDesc)
        check_isinstance("addrs", addrs, list, int)
        check_isinstance("cache_keys", cache_keys, [tuple, list], CacheKey)
        check_isinstance("remote_accessible", remote_accessible, bool)
        raise_if_true(self._is_call_linked and remote_accessible,
                      "you can not register remote accessible cache after link.")
        raise_if_false(len(addrs) > 0, "addrs can not be empty.")
        raise_if_false(len(addrs) == cache_desc.num_tensors,
                       "addrs length should be equal to num_tensors.")
        for addr in addrs:
            raise_if_false(addr != 0, "addr can not be zero.")
        wrapped_cache_keys = [pack_cache_key(cache_key) for cache_key in cache_keys]
        inner_remote_accessible = self._get_remote_accessible('register_cache', cache_desc, remote_accessible)
        ret, cache_id_and_addr = self._llm_datadist.register_cache(pack_cache_desc(cache_desc), addrs,
                                                                   wrapped_cache_keys, inner_remote_accessible)
        handle_llm_status(ret, '[register_cache]', f'cache_desc = {cache_desc}')
        cache = Cache(cache_id_and_addr[0], cache_desc, cache_id_and_addr[1][0], self, True, False)
        log.info('[register_cache] success, cache_id = %d.', cache.cache_id)
        return cache

    def register_blocks_cache(self, cache_desc: CacheDesc, addrs: List[int],
                              blocks_cache_key: Optional[BlocksCacheKey] = None,
                              remote_accessible: Optional[bool] = None) -> Cache:
        """
        Args:
            cache_desc: Cache描述
            addrs: device addrs of cache tensors
            blocks_cache_key(Optional): BlocksCacheKey
            remote_accessible(Optional): whether remote cache is accessible
        """
        check_isinstance("cache_desc", cache_desc, CacheDesc)
        check_isinstance("addrs", addrs, list, int)
        check_isinstance("blocks_cache_key", blocks_cache_key, BlocksCacheKey)
        check_isinstance("remote_accessible", remote_accessible, bool)
        raise_if_true(self._is_call_linked and remote_accessible == True,
                      "you can not register remote accessible cache after link.")
        raise_if_false(len(addrs) > 0, "addrs can not be empty.")
        cache_desc._is_blocks = True
        cache_keys = [pack_block_cache_key(blocks_cache_key)] if blocks_cache_key is not None else []
        inner_remote_accessible = self._get_remote_accessible('register_blocks_cache', cache_desc, remote_accessible)
        ret, cache_id_and_addr = self._llm_datadist.register_cache(pack_cache_desc(cache_desc), addrs,
                                                                   cache_keys, inner_remote_accessible)
        handle_llm_status(ret, '[register_blocks_cache]', f'cache_desc = {cache_desc}')
        cache = Cache(cache_id_and_addr[0], cache_desc, cache_id_and_addr[1][0], self, True, True)
        log.info('[register_blocks_cache] success, cache_id = %d', cache.cache_id)
        return cache

    def unregister_cache(self, cache_id: int) -> None:
        """
        Cache 解注册, 需要在所有与当前datadist链路断链之后调用

        Args:
            cache_id: 待解注册的Cache id
        """
        check_isinstance("cache_id", cache_id, int)
        check_int64("cache_id", cache_id)
        ret = self._llm_datadist.unregister_cache(cache_id)
        handle_llm_status(ret, '[unregister_cache]', f'cache_id = {cache_id}')
        log.info('[unregister_cache] success, cache_id = %d', cache_id)

    def remove_cache_key(self, cache_key: CacheKey) -> None:
        """
        移除CacheKey后, 该Cache将无法再被pull_cache拉取

        Args:
            cache_key: CacheKey

        Examples:
            see examples of allocate_cache
        """
        check_isinstance("cache_key", cache_key, CacheKey)
        log.info('[remove_cache_key] start, cache_key = %s', cache_key)
        ret = self._llm_datadist.remove_cache_key_v2(pack_cache_key(cache_key))
        handle_llm_status(ret, '[remove_cache_key]', f'cache_key = {cache_key}')
        log.info('[remove_cache_key] success')

    def remap_registered_memory(self, mem_infos: Union[MemInfo, list[MemInfo]]) -> None:
        """
        内存发生故障后, 使用该接口重新映射物理地址

        Args:
            mem_infos: Union[MemInfo, list[MemInfo]]
        """
        raise_if_false(isinstance(mem_infos, list) or isinstance(mem_infos, MemInfo),
                       f"mem_infos type only support list of MemInfo or MemInfo, but got {format(type(mem_infos))}.")
        if not isinstance(mem_infos, list):
            mem_info_list = [mem_infos]
        else:
            mem_info_list = mem_infos

        for i, mem_info in enumerate(mem_info_list):
            raise_if_false(isinstance(mem_info, MemInfo),
                           f"mem_infos[{i}] type only support MemInfo, but got {format(type(mem_info))}.")
            raise_if_false(mem_info.mem_type == Memtype.MEM_TYPE_DEVICE,
                           f'check mem_info.mem_type failed, only support Memtype.MEM_TYPE_DEVICE, index={i}.')
            raise_if_false(mem_info.addr != 0,
                           f'check mem_info.addr failed, addr can not be zero, index={i}.')
            raise_if_false(mem_info.size > 0,
                           f'check mem_info.size failed, size must be > 0, index={i}.')

        log.info('[remap_registered_memory] start, mem_infos size = %s', len(mem_info_list))
        wrapped_mem_infos = [pack_mem_info(mem_info) for mem_info in mem_info_list]
        ret = self._llm_datadist.remap_registered_memory(wrapped_mem_infos)
        handle_llm_status(ret, '[remap_registered_memory]', f'mem_info_list = {mem_info_list}')
        log.info('[remap_registered_memory] success')

    def set_is_call_linked(self):
        self._is_call_linked = True

    @staticmethod
    def _verify_caches(src_cache: Cache, dst_cache: Cache, src_to_dst: Dict[int, int]):
        check_isinstance("src", src_cache, Cache)
        check_isinstance("dst", dst_cache, Cache)
        raise_if_false(src_cache.is_blocks_cache, 'param check failed, src cache should be blocks cache.')
        raise_if_false(dst_cache.is_blocks_cache, 'param check failed, dst cache should be blocks cache.')
        check_isinstance("src_to_dst", src_to_dst, dict, int)

        src_block_size = src_cache.cache_desc.size // src_cache.cache_desc.batch_size
        dst_block_size = dst_cache.cache_desc.size // dst_cache.cache_desc.batch_size
        raise_if_false(src_block_size == dst_block_size,
                       f"src block size:{src_block_size} and dst block size:{dst_block_size} must be equal")
        log.info("src and dst cache block size:%d", src_block_size)

        src_num_tensors = src_cache.cache_desc.num_tensors
        dst_num_tensors = dst_cache.cache_desc.num_tensors
        raise_if_false(src_num_tensors == dst_num_tensors,
                       f"src num_tensors:{src_num_tensors} and dst num_tensors:{dst_num_tensors} must be equal")
        log.info("src and dst cache num:%d", src_num_tensors)

        src_block_num = src_cache.cache_desc.batch_size
        dst_block_num = dst_cache.cache_desc.batch_size
        log.info("src num block:%d, dst num block:%d", src_block_num, dst_block_num)
        for src_block_index, dst_block_index in src_to_dst.items():
            raise_if_false(0 <= src_block_index < src_block_num,
                           f"src_block_index:{src_block_index} must be in [0, {src_block_num})")
            raise_if_false(0 <= dst_block_index < dst_block_num,
                           f"dst_block_index:{dst_block_index} must be in [0, {dst_block_num})")

    def swap_blocks(self, src_cache: Cache, dst_cache: Cache, src_to_dst: Dict[int, int]) -> None:
        """
        交换blocks

        Args:
            src: 源Cache
            dst: 目的Cache
            src_to_dst: block index的字典
        """
        self._verify_caches(src_cache, dst_cache, src_to_dst)
        src_placement = src_cache.cache_desc.placement
        dst_placement = dst_cache.cache_desc.placement
        is_swap_in = (src_placement == Placement.HOST) and (dst_placement == Placement.DEVICE)
        is_swap_out = (src_placement == Placement.DEVICE) and (dst_placement == Placement.HOST)
        raise_if_false(is_swap_in or is_swap_out,
                       f"swap src placement:{src_placement} to dst placement:{dst_placement} is not support")

        # 0标识swap in，1标识swap out
        swap_type = 0 if is_swap_in else 1
        block_size = src_cache.cache_desc.size // src_cache.cache_desc.batch_size
        default_cache_id = -1
        ret = self._llm_datadist.swap_blocks_v2((default_cache_id, [src_cache.tensor_addrs]),
                                                (default_cache_id, [dst_cache.tensor_addrs]),
                                                block_size, swap_type, self._llm_datadist.dict_to_vector(src_to_dst))
        handle_llm_status(ret, '[swap_blocks]', 'swap blocks failed')
        log.info('[swap_blocks] success')

    def transfer_cache_async(self,
                             src_cache: Cache,
                             layer_synchronizer: LayerSynchronizer,
                             transfer_configs: Union[List[Union[TransferConfig, TransferWithCacheKeyConfig]],
                                                     Tuple[Union[TransferConfig, TransferWithCacheKeyConfig]]],
                             src_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                             dst_block_indices: Optional[Union[List[int], Tuple[int]]] = None,
                             dst_block_memory_size: Optional[int] = None) -> CacheTask:
        check_isinstance("src_cache", src_cache, Cache, allow_none=False)
        raise_if_true(src_cache.cache_desc.placement == Placement.HOST, "src cache not support placement HOST")
        params = TransferCacheParameters(src_cache,
                                         transfer_configs,
                                         src_block_indices,
                                         dst_block_indices,
                                         dst_block_memory_size)
        log.info('[transfer_cache_async] start, params = %s', params)
        return transfer_cache_async(params, layer_synchronizer, self._llm_datadist.transfer_cache_v2,
                                    enable_remote_cache=self._enable_remote_cache_accessible)

    def push_cache(self,
                   dst_cache_key: CacheKeyByIdAndIndex,
                   src_cache: Cache,
                   src_batch_index: int = 0,
                   src_layer_range: range = None,
                   dst_layer_range: range = None,
                   size: int = -1,
                   tensor_num_per_layer = _NUM_TENSORS_PER_LAYER) -> None:
        """
        从本地向远端推送cache

        Args:
            dst_cache_key: 目的端的cache索引
            src_cache: 本地的cache
            src_batch_index: 本地的batch_index,默认为0
            size: 传输大小
            src_layer_range: 源层范围
            dst_layer_range: 目的层范围
            tensor_num_per_layer: 每层tensor数量
        """
        # only C2C
        raise_if_false(self._enable_remote_cache_accessible,
                       "push_cache is only supported while enable_remote_cache_accessible is True")
        check_isinstance("dst_cache_key", dst_cache_key, [CacheKeyByIdAndIndex], allow_none=False)
        check_isinstance("src_cache", src_cache, Cache, allow_none=False)
        check_uint32("src_batch_index", src_batch_index)
        raise_if_false(not src_cache.is_blocks_cache, 'param check failed, cache can not be blocks cache.')

        check_uint32("tensor_num_per_layer", tensor_num_per_layer)
        raise_if_false(tensor_num_per_layer > 0,
                       '[push_cache] param check failed, tensor_num_per_layer ({0}) is invalid, should [1, {1}]',
                       tensor_num_per_layer, src_cache.cache_desc.num_tensors)
        check_layer_range('src_layer_range', src_layer_range, True)
        check_layer_range('dst_layer_range', dst_layer_range, True)
        if src_layer_range is None :
            src_layer_range = range(0, src_cache.cache_desc.num_tensors // tensor_num_per_layer)
        if dst_layer_range is None :
            dst_layer_range = range(0, src_cache.cache_desc.num_tensors // tensor_num_per_layer)
        raise_if_false(len(src_layer_range) == len(dst_layer_range),
                       "src_layer_range size should be equal to size of dst_layer_range size.")
        check_int64("size", size)
        raise_if_false(size == -1,
                       '[push_cache] param check failed, size ({0}) is invalid, should be = -1.', size)

        log.info('[push_cache] start, cache_id = %d, src_batch_index = %d, dst_cache_key = %s, '
                 'src_layer_range = %s, dst_layer_range = %s, size = %d, tensor_num_per_layer = %d.',
                 src_cache.cache_id, src_batch_index, dst_cache_key, src_layer_range, dst_layer_range, size, tensor_num_per_layer)
        for i, src_layer_index in enumerate(list(src_layer_range)):
            dst_layer_index = dst_layer_range[i]
            transfer_config = (src_cache.cache_id, src_batch_index, src_layer_index, [],
                               dst_cache_key.cluster_id, dst_cache_key.cache_id,
                               dst_cache_key.batch_index, PushType.CACHE_KEY_BY_ID.value,
                               dst_layer_index, tensor_num_per_layer)
            block_config = (0, [], [])
            ret = self._llm_datadist.transfer_cache_v2(0, transfer_config, block_config)
            handle_llm_status(ret, '[push_cache]', f'dst_cache_key = {dst_cache_key}')
        log.info('[push_cache] success')

    def push_blocks(self,
                    dst_cache_key: BlocksCacheKey,
                    src_cache: Cache,
                    src_blocks: Optional[Union[Tuple[int], List[int]]] = (),
                    dst_blocks: Union[Tuple[int], List[int]] = (),
                    src_layer_range: range = None,
                    dst_layer_range: range = None,
                    tensor_num_per_layer = _NUM_TENSORS_PER_LAYER):
        """
        PA模式下推送KV

        Args:
            dst_cache_key: 目的端的cache索引
            src_cache: 本地的cache
            src_blocks: 本地的block列表
            dst_blocks: 目的端的block列表
            src_layer_range: 源层范围
            dst_layer_range: 目的层范围
            tensor_num_per_layer: 每层tensor数量
        """
        # C2B or B2B
        raise_if_false(self._enable_remote_cache_accessible,
                       "push_blocks is only supported while enable_remote_cache_accessible is True")
        check_isinstance("dst_cache_key", dst_cache_key, [BlocksCacheKey])
        check_isinstance("src_cache", src_cache, Cache)
        check_isinstance("src_blocks", src_blocks, [list, tuple], int)
        check_isinstance("dst_blocks", dst_blocks, [list, tuple], int)
        raise_if_false(src_cache.is_blocks_cache, 'param check failed, cache should be blocks cache.')
        raise_if_false(len(dst_blocks) > 0, "dst_blocks can not be empty.")

        check_uint32("tensor_num_per_layer", tensor_num_per_layer)
        raise_if_false(tensor_num_per_layer > 0,
                       '[push_blocks] param check failed, tensor_num_per_layer ({0}) is invalid, should [1, {1}]',
                       tensor_num_per_layer, src_cache.cache_desc.num_tensors)
        check_layer_range('src_layer_range', src_layer_range, True)
        check_layer_range('dst_layer_range', dst_layer_range, True)
        if src_layer_range is None :
            src_layer_range = range(0, src_cache.cache_desc.num_tensors // tensor_num_per_layer)
        if dst_layer_range is None :
            dst_layer_range = range(0, src_cache.cache_desc.num_tensors // tensor_num_per_layer)
        raise_if_false(len(src_layer_range) == len(dst_layer_range),
                       "src_layer_range size should be equal to size of dst_layer_range size.")

        log.info('[push_blocks] start, cache_id = %d, dst_cache_key = %s, '
                 'src_layer_range = %s, dst_layer_range = %s, tensor_num_per_layer = %d',
                 src_cache.cache_id, dst_cache_key, src_layer_range, dst_layer_range, tensor_num_per_layer)
        for i, src_layer_index in enumerate(list(src_layer_range)):
            dst_layer_index = dst_layer_range[i]
            transfer_config = (src_cache.cache_id, 0, src_layer_index, [],
                               dst_cache_key.cluster_id, dst_cache_key.model_id,
                               0, PushType.BLOCKS_CACHE_KEY.value, dst_layer_index, tensor_num_per_layer)
            block_config = (0,
                            src_blocks if src_blocks is not None else [],
                            dst_blocks if dst_blocks is not None else [])
            ret = self._llm_datadist.transfer_cache_v2(0, transfer_config, block_config)
            handle_llm_status(ret, '[push_blocks]', f'dst_cache_key = {dst_cache_key}')
        log.info('[push_blocks] success')

    def _get_remote_accessible(self, func_name, cache_desc: CacheDesc, remote_accessible: Optional[bool] = None):
        if remote_accessible is None:
            inner_remote_accessible = True if cache_desc.placement == Placement.DEVICE else False
            inner_remote_accessible = False if self._is_call_linked else inner_remote_accessible
            log.info('[%s] start, set default remote_accessible = %s when cache placement = %s%s.',
                     func_name, inner_remote_accessible, cache_desc.placement,
                     ' after link' if self._is_call_linked else '')
        else:
            inner_remote_accessible = remote_accessible
            log.info('[%s] start, set remote_accessible = %s.', func_name, inner_remote_accessible)
        return inner_remote_accessible