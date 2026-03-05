#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2025 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

import os
import sys

current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
sys.path.insert(0, parent_dir)

import time
import logging
import torch
import torch_npu

from mooncake_sample_common import create_parser, setup_environment, validate_schema, ALIGNMENT
from mooncake_sample_base import MooncakeSampleBase
from config import Config

logging.basicConfig(format="%(message)s", level=logging.INFO)


class BandwidthBenchmark(MooncakeSampleBase):
    def __init__(self, args, config):
        super().__init__(args, config)
        self.registered_addrs = []
        self.global_iter_counter = 0
    
    def run(self):
        self._log_startup_info()
        
        if self.args.register_size_gb is not None:
            logging.info(f"Custom register size specified: {self.args.register_size_gb} GB (stress testing mode)")
        
        max_block_size_kb = max(self.args.block_sizes)
        self.store = self.init_mooncake_store()
        self._prepare_buffers(max_block_size_kb)
        addr, remote_addr = self._register_all_buffers()
        self.barrier()
        
        results = {}
        for block_size in self.args.block_sizes:
            results[block_size] = self._run_benchmark(block_size, addr, remote_addr)
            
        self._unregister_all_buffers()
        self._cleanup()
        self._print_summary(results)
    
    def _log_startup_info(self):
        logging.info(f"Starting bandwidth benchmark")
        logging.info(f"Schema: {self.args.schema}")
        logging.info(f"Transfer mode: {self.args.transfer_mode}")
        logging.info(f"World size: {self.config.world_size}")
    
    def _prepare_buffers(self, block_size_kb):
        buffer_size = block_size_kb * 1024 * self.args.num_blocks
        
        if self.args.register_size_gb is not None:
            tensor_size = int(self.args.register_size_gb * 1024 * 1024 * 1024) + ALIGNMENT * 2
            logging.info(
                f"Creating tensor of size {tensor_size} bytes "
                f"({tensor_size / (1024 ** 3):.3f} GB) for stress testing")
        else:
            tensor_size = buffer_size + ALIGNMENT * 2
        
        is_host = self.args.schema.startswith("h")
        target_is_host = self.args.schema.endswith("h")
        
        self.tensor = self._create_single_tensor(tensor_size, 1, is_host)
        self.target_tensor = self._create_single_tensor(tensor_size, 0, target_is_host)
    
    def _create_single_tensor(self, size, fill_value, is_host):
        if is_host:
            return torch.full((size,), fill_value, dtype=torch.int8, pin_memory=True).cpu()
        else:
            return torch.full((size,), fill_value, dtype=torch.int8).npu()
    
    def _register_all_buffers(self):
        addr = self._register_buffer(self.tensor, "write")
        remote_addr = self._register_buffer(self.target_tensor, "read")
        return addr, remote_addr
    
    def _register_buffer(self, tensor, buffer_type):
        data_ptr = tensor.data_ptr()
        aligned_addr = self._align_address(data_ptr)
        total_size = tensor.numel() * tensor.element_size()
        
        if self.args.register_size_gb is not None:
            register_size = int(self.args.register_size_gb * 1024 * 1024 * 1024)
        else:
            register_size = total_size - (aligned_addr - data_ptr)
        
        if register_size <= 0:
            raise ValueError(f"Invalid register size: {register_size}, data_ptr={data_ptr}, aligned={aligned_addr}")
        
        self.store.register_buffer(aligned_addr, register_size)
        self.registered_addrs.append(aligned_addr)
        
        register_gb = register_size / (1024 ** 3)
        logging.info(
            f"Registered {buffer_type} buffer: addr={data_ptr}, "
            f"aligned={aligned_addr}, size={register_size} ({register_gb:.3f} GB)")
        return aligned_addr
    
    def _align_address(self, addr):
        return (addr + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
    
    def _unregister_all_buffers(self):
        for addr in self.registered_addrs:
            try:
                self.store.unregister_buffer(addr)
                logging.info(f"Unregistered buffer: {addr}")
            except Exception as e:
                logging.warning(f"Failed to unregister buffer {addr}: {e}")
        self.registered_addrs = []
    
    def _run_benchmark(self, block_size_kb, addr, remote_addr):
        self._log_benchmark_config(block_size_kb)
        
        metrics = self._run_one_put_multiple_gets(block_size_kb, addr, remote_addr)
        
        return self._compute_bandwidth(block_size_kb, metrics)
    
    def _log_benchmark_config(self, block_size_kb):
        logging.info(f"\n{'='*80}")
        logging.info(f"Benchmark Configuration:")
        logging.info(f"  Block Size: {block_size_kb} KB")
        logging.info(f"  Number of Blocks: {self.args.num_blocks}")
        logging.info(f"  Iterations: {self.args.num_iters}")
        logging.info(f"  Transfer Mode: {self.args.transfer_mode}")
        logging.info(f"{'='*80}\n")
    
    def _run_one_put_multiple_gets(self, block_size_kb, addr, remote_addr):
        iter_id = self._get_next_iter_id()
        
        logging.info("Performing warmup iteration...")
        put_time = self._timed_put_operation(block_size_kb, addr, remote_addr, iter_id)
        self.barrier()
        self._timed_get_operation(block_size_kb, addr, remote_addr, iter_id)
        self.barrier()
        logging.info("Warmup completed")
        
        get_times = []
        for _ in range(self.args.num_iters):
            get_time = self._timed_get_operation(block_size_kb, addr, remote_addr, iter_id)
            get_times.append(get_time)
        
        return {
            'put_time': put_time,
            'get_times': get_times
        }
    
    def _get_next_iter_id(self):
        iter_id = self.global_iter_counter
        self.global_iter_counter += 1
        return iter_id
    
    def _timed_put_operation(self, block_size_kb, addr, remote_addr, iter_id):
        ops = {
            'pairwise': self._timed_put_pairwise,
            'full_mesh': self._timed_put_full_mesh,
            'one_to_many': self._timed_put_one_to_many
        }
        
        op = ops.get(self.args.transfer_mode)
        if not op:
            raise ValueError(f"Unknown transfer mode: {self.args.transfer_mode}")
        
        return op(block_size_kb, addr, remote_addr, iter_id)
    
    def _timed_get_operation(self, block_size_kb, addr, remote_addr, iter_id):
        ops = {
            'pairwise': self._timed_get_pairwise,
            'full_mesh': self._timed_get_full_mesh,
            'one_to_many': self._timed_get_one_to_many
        }
        
        op = ops.get(self.args.transfer_mode)
        if not op:
            raise ValueError(f"Unknown transfer mode: {self.args.transfer_mode}")
        
        return op(block_size_kb, addr, remote_addr, iter_id)
    
    def _compute_bandwidth(self, block_size_kb, metrics):
        put_time = metrics['put_time']
        get_times = metrics['get_times']
        
        avg_get = self._compute_average(get_times)
        
        total_bytes = block_size_kb * 1024 * self.args.num_blocks
        total_gb = total_bytes / (1024 ** 3)
        
        data_multiplier = 1
        if self.args.transfer_mode == 'full_mesh':
            data_multiplier = self.config.world_size - 1
        
        put_data_gb = total_gb * data_multiplier
        get_data_gb = total_gb * data_multiplier
        
        put_bw = put_data_gb / put_time if put_time > 0 else 0
        get_bw = get_data_gb / avg_get if avg_get > 0 else 0
        
        logging.info(f"\n{'='*80}")
        logging.info(f"Results for {block_size_kb} KB:")
        logging.info(f"  Total Data per operation: {total_gb:.3f} GB")
        logging.info(f"  Put: {put_time:.6f}s => {put_bw:.3f} GB/s ({put_data_gb:.3f} GB)")
        logging.info(
            f"  Get (avg over {self.args.num_iters} iters): {avg_get:.6f}s"
            f" => {get_bw:.3f} GB/s ({get_data_gb:.3f} GB)")
        logging.info(f"{'='*80}\n")
        
        return {'put_bandwidth': put_bw, 'get_bandwidth': get_bw}
    
    def _compute_average(self, times):
        return sum(times) / len(times) if times else 0
    
    def _timed_put_pairwise(self, block_size_kb, addr, remote_addr, iter_id):
        put_keys = self._generate_keys(self.config.rank, iter_id, self.args.num_blocks)
        local_addrs = self._generate_addrs(addr, block_size_kb, self.args.num_blocks)
        sizes = [block_size_kb * 1024] * self.args.num_blocks
        
        start = time.time()
        self.store.batch_put_from(put_keys, local_addrs, sizes)
        return time.time() - start
    
    def _timed_get_pairwise(self, block_size_kb, addr, remote_addr, iter_id):
        target_rank = (self.config.rank + 1) % self.config.world_size
        get_keys = self._generate_keys(target_rank, iter_id, self.args.num_blocks)
        remote_addrs = self._generate_addrs(remote_addr, block_size_kb, self.args.num_blocks)
        sizes = [block_size_kb * 1024] * self.args.num_blocks
        
        start = time.time()
        results = self.store.batch_get_into(get_keys, remote_addrs, sizes)
        end = time.time()
        
        self._validate_results(get_keys, results)
        return end - start
    
    def _timed_put_full_mesh(self, block_size_kb, addr, remote_addr, iter_id):
        my_rank = self.config.rank
        put_keys = self._generate_keys(my_rank, iter_id, self.args.num_blocks)
        local_addrs = self._generate_addrs(addr, block_size_kb, self.args.num_blocks)
        sizes = [block_size_kb * 1024] * self.args.num_blocks
        
        start = time.time()
        self.store.batch_put_from(put_keys, local_addrs, sizes)
        return time.time() - start
    
    def _timed_get_full_mesh(self, block_size_kb, addr, remote_addr, iter_id):
        my_rank = self.config.rank
        world_size = self.config.world_size
        
        prepared_data = []
        for src_rank in range(world_size):
            if src_rank != my_rank:
                get_keys = self._generate_keys(src_rank, iter_id, self.args.num_blocks)
                remote_addrs = self._generate_addrs(remote_addr, block_size_kb, self.args.num_blocks)
                sizes = [block_size_kb * 1024] * self.args.num_blocks
                prepared_data.append((get_keys, remote_addrs, sizes))
        
        total_time = 0
        for get_keys, remote_addrs, sizes in prepared_data:
            start = time.time()
            results = self.store.batch_get_into(get_keys, remote_addrs, sizes)
            total_time += time.time() - start
            self._validate_results(get_keys, results)
        return total_time
    
    def _timed_put_one_to_many(self, block_size_kb, addr, remote_addr, iter_id):
        if self.config.rank != 0:
            return 0
        
        put_keys = self._generate_keys(0, iter_id, self.args.num_blocks)
        local_addrs = self._generate_addrs(addr, block_size_kb, self.args.num_blocks)
        sizes = [block_size_kb * 1024] * self.args.num_blocks
        
        start = time.time()
        self.store.batch_put_from(put_keys, local_addrs, sizes)
        return time.time() - start
    
    def _timed_get_one_to_many(self, block_size_kb, addr, remote_addr, iter_id):
        if self.config.rank == 0:
            return 0
        
        get_keys = self._generate_keys(0, iter_id, self.args.num_blocks)
        remote_addrs = self._generate_addrs(remote_addr, block_size_kb, self.args.num_blocks)
        sizes = [block_size_kb * 1024] * self.args.num_blocks
        
        start = time.time()
        results = self.store.batch_get_into(get_keys, remote_addrs, sizes)
        end = time.time()
        
        self._validate_results(get_keys, results)
        return end - start
    
    def _generate_keys(self, rank, iter_id, num_blocks):
        return [f"block_{rank}_{iter_id}_{i}" for i in range(num_blocks)]
    
    def _generate_addrs(self, base_addr, block_size_kb, num_blocks):
        block_size = block_size_kb * 1024
        return [base_addr + i * block_size for i in range(num_blocks)]
    
    def _validate_results(self, keys, results):
        failed = [(k, r) for k, r in zip(keys, results) if r <= 0]
        if failed:
            logging.warning(f"Failed to retrieve {len(failed)} blocks")
            for key, ret in failed[:5]:
                logging.warning(f"  Key: {key}, Return code: {ret}")
            if len(failed) > 5:
                logging.warning(f"  ... and {len(failed) - 5} more")
    
    def _cleanup(self):
        self.barrier()
        if self.store:
            self.close_store()
        del self.target_tensor
        del self.tensor
        torch.npu.empty_cache()
        import gc
        gc.collect()
    
    def _print_summary(self, results):
        logging.info(f"\n{'='*80}")
        logging.info("SUMMARY - Bandwidth Results")
        logging.info(f"Mode: {self.args.transfer_mode}")
        logging.info(f"World Size: {self.config.world_size}")
        logging.info(f"{'='*80}")
        logging.info(f"{'Block (KB)':<15} {'Put (GB/s)':<15} {'Get (GB/s)':<15}")
        logging.info(f"{'-'*45}")
        
        for size in sorted(results.keys()):
            r = results[size]
            logging.info(f"{size:<15} {r['put_bandwidth']:<15.3f} {r['get_bandwidth']:<15.3f}")
        
        logging.info(f"{'='*80}\n")


def main():
    parser = create_parser("Bandwidth Benchmark")
    
    parser.add_argument("--transfer_mode", type=str, default="pairwise",
                        choices=["pairwise", "full_mesh", "one_to_many"],
                        help="Transfer mode")
    parser.add_argument("--block_sizes", type=str, default="1,4,16,64,144,256,512,1024",
                        help="Comma-separated block sizes in KB")
    parser.add_argument("--num_blocks", type=int, default=100,
                        help="Number of blocks per iteration")
    parser.add_argument("--num_iters", type=int, default=10,
                        help="Number of GET iterations after PUT")
    parser.add_argument("--register_size_gb", type=float, default=None,
                        help="Custom memory size to register in GB (for stress testing)")
    
    config = Config()
    args = config.parse_args(parser)
    args.schema = args.schema.lower()
    args.block_sizes = [int(x.strip()) for x in args.block_sizes.split(",")]
    
    runner = BandwidthBenchmark(args, config)
    validate_schema(args.schema)
    setup_environment(args)
    runner.init_process_group()
    runner.run()


if __name__ == "__main__":
    main()
