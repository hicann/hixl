#!/usr/bin/env python3
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

import ctypes
import logging
import unittest

import hixl

logging.basicConfig(format="%(asctime)s %(message)s", level=logging.INFO)


_DEFAULT_OPTIONS = {
    hixl.OPTION_AUTO_CONNECT: "1",
}


def _alloc_cpu_mem(size):
    buf = ctypes.create_string_buffer(size)
    addr = ctypes.addressof(buf)
    return hixl.MemDesc(addr, size), buf


def _alloc_npu_mem(size):
    buf = ctypes.create_string_buffer(size)
    addr = ctypes.addressof(buf)
    return hixl.MemDesc(addr, size), buf


def _create_engine():
    engine = hixl.Hixl()
    ret = engine.initialize("127.0.0.1:0", _DEFAULT_OPTIONS)
    assert ret == hixl.SUCCESS, f"initialize failed: {ret}"
    return engine


class HixlModuleImportTest(unittest.TestCase):
    def test_status_constants(self):
        self.assertEqual(hixl.SUCCESS, 0)
        self.assertEqual(hixl.FAILED, 503900)
        self.assertEqual(hixl.PARAM_INVALID, 103900)
        self.assertEqual(hixl.TIMEOUT, 103901)
        self.assertEqual(hixl.NOT_CONNECTED, 103902)
        self.assertEqual(hixl.ALREADY_CONNECTED, 103903)
        self.assertEqual(hixl.NOTIFY_FAILED, 103904)
        self.assertEqual(hixl.UNSUPPORTED, 103905)
        self.assertEqual(hixl.RESOURCE_EXHAUSTED, 203900)

    def test_enum_constants(self):
        self.assertEqual(int(hixl.MemType.MEM_DEVICE), 0)
        self.assertEqual(int(hixl.MemType.MEM_HOST), 1)
        self.assertEqual(int(hixl.TransferOp.READ), 0)
        self.assertEqual(int(hixl.TransferOp.WRITE), 1)
        self.assertEqual(int(hixl.FeatureType.AUTO_CONNECT), 0)
        self.assertEqual(int(hixl.FeatureType.CLIENT_SERVER_COMM), 1)
        self.assertEqual(hixl.MEM_DEVICE, 0)
        self.assertEqual(hixl.MEM_HOST, 1)

        self.assertEqual(hixl.READ, 0)
        self.assertEqual(hixl.WRITE, 1)

        self.assertEqual(hixl.AUTO_CONNECT, 0)
        self.assertEqual(hixl.CLIENT_SERVER_COMM, 1)

    def test_transfer_status_enum(self):
        self.assertEqual(int(hixl.TransferStatus.WAITING), 0)
        self.assertEqual(int(hixl.TransferStatus.COMPLETED), 1)
        self.assertEqual(int(hixl.TransferStatus.TIMEOUT), 2)
        self.assertEqual(int(hixl.TransferStatus.FAILED), 3)

    def test_async_connect_status_enum(self):
        self.assertEqual(int(hixl.AsyncConnectStatus.NOT_CONNECT), 0)
        self.assertEqual(int(hixl.AsyncConnectStatus.CONNECT_PENDING), 1)
        self.assertEqual(int(hixl.AsyncConnectStatus.CONNECTING), 2)
        self.assertEqual(int(hixl.AsyncConnectStatus.CONNECTED), 3)
        self.assertEqual(int(hixl.AsyncConnectStatus.CONNECT_FAILED), 4)
        self.assertEqual(int(hixl.AsyncConnectStatus.DISCONNECT_PENDING), 5)
        self.assertEqual(int(hixl.AsyncConnectStatus.DISCONNECTING), 6)

    def test_feature_support_constants(self):
        self.assertEqual(hixl.FEATURE_SUPPORTED, 1)
        self.assertEqual(hixl.FEATURE_NOT_SUPPORTED, 0)

    def test_option_constants(self):
        self.assertIsInstance(hixl.OPTION_ENABLE_USE_FABRIC_MEM, str)
        self.assertIsInstance(hixl.OPTION_RDMA_TRAFFIC_CLASS, str)
        self.assertIsInstance(hixl.OPTION_RDMA_SERVICE_LEVEL, str)
        self.assertIsInstance(hixl.OPTION_BUFFER_POOL, str)
        self.assertIsInstance(hixl.OPTION_GLOBAL_RESOURCE_CONFIG, str)
        self.assertIsInstance(hixl.OPTION_AUTO_CONNECT, str)
        self.assertIsInstance(hixl.OPTION_LOCAL_COMM_RES, str)


class HixlDataClassesTest(unittest.TestCase):
    def test_mem_desc(self):
        desc = hixl.MemDesc(addr=0x1000, len=8192)
        self.assertEqual(desc.addr, 0x1000)
        self.assertEqual(desc.len, 8192)
        desc.addr = 0x2000
        desc.len = 4096
        self.assertEqual(desc.addr, 0x2000)
        self.assertEqual(desc.len, 4096)

    def test_transfer_op_desc(self):
        op = hixl.TransferOpDesc(local_addr=0x1000, remote_addr=0x2000, len=4096)
        self.assertEqual(op.local_addr, 0x1000)
        self.assertEqual(op.remote_addr, 0x2000)
        self.assertEqual(op.len, 4096)
        op.local_addr = 0x3000
        self.assertEqual(op.local_addr, 0x3000)

    def test_transfer_args(self):
        args = hixl.TransferArgs()
        self.assertEqual(args.user_data, 0)
        args.user_data = 123
        self.assertEqual(args.user_data, 123)

    def test_get_transfer_status_args(self):
        args = hixl.GetTransferStatusArgs()
        self.assertEqual(args.max_query_count, 4294967295)
        self.assertEqual(args.skip_waiting, False)
        args = hixl.GetTransferStatusArgs(max_query_count=100, skip_waiting=True)
        self.assertEqual(args.max_query_count, 100)
        self.assertEqual(args.skip_waiting, True)

    def test_transfer_result(self):
        result = hixl.TransferResult()
        self.assertEqual(result.req, 0)
        self.assertEqual(result.user_data, 0)
        self.assertEqual(result.status, hixl.TransferStatus.WAITING)
        self.assertEqual(int(result.status), 0)

    def test_notify_desc(self):
        notify = hixl.NotifyDesc(name="signal", notify_msg="hello")
        self.assertEqual(notify.name, "signal")
        self.assertEqual(notify.notify_msg, "hello")
        notify.name = "updated"
        notify.notify_msg = "world"
        self.assertEqual(notify.name, "updated")
        self.assertEqual(notify.notify_msg, "world")


class HixlInitializeFinalizeTest(unittest.TestCase):
    def setUp(self):
        logging.info("Begin %s", self._testMethodName)
        self.engine = None

    def tearDown(self):
        logging.info("End %s", self._testMethodName)
        if self.engine is not None:
            try:
                self.engine.finalize()
            except Exception:
                logging.exception("finalize failed during tearDown")

    def test_two_step_init_success(self):
        self.engine = hixl.Hixl()
        ret = self.engine.initialize("127.0.0.1:0", _DEFAULT_OPTIONS)
        self.assertEqual(ret, hixl.SUCCESS)

    def test_init_default_options(self):
        self.engine = hixl.Hixl()
        ret = self.engine.initialize("127.0.0.1:0")
        self.assertEqual(ret, hixl.SUCCESS)

    def test_finalize_returns_none(self):
        self.engine = _create_engine()
        result = self.engine.finalize()
        self.assertIsNone(result)

    def test_finalize_no_crash(self):
        self.engine = _create_engine()
        self.engine.finalize()
        self.engine.finalize()

    def test_create_multiple_engines(self):
        engine1 = _create_engine()
        engine2 = _create_engine()
        self.assertIsNotNone(engine1)
        self.assertIsNotNone(engine2)
        engine1.finalize()
        engine2.finalize()

    def test_repeated_initialize_returns_success(self):
        self.engine = _create_engine()
        ret = self.engine.initialize("127.0.0.1:0", _DEFAULT_OPTIONS)
        self.assertEqual(ret, hixl.SUCCESS)


class HixlRegisterMemTest(unittest.TestCase):
    def setUp(self):
        logging.info("Begin %s", self._testMethodName)
        self.engine = _create_engine()
        self._cpu_buf = None
        self._npu_buf = None
        self._cpu_handle = 0
        self._npu_handle = 0

    def tearDown(self):
        logging.info("End %s", self._testMethodName)
        if self._cpu_handle != 0:
            try:
                self.engine.deregister_mem(self._cpu_handle)
            except Exception:
                logging.exception(
                    "deregister_mem failed for _cpu_handle during tearDown"
                )
        if self._npu_handle != 0:
            try:
                self.engine.deregister_mem(self._npu_handle)
            except Exception:
                logging.exception(
                    "deregister_mem failed for _npu_handle during tearDown"
                )
        self._cpu_buf = None
        self._npu_buf = None
        try:
            self.engine.finalize()
        except Exception:
            logging.exception("finalize failed during tearDown")

    def test_register_mem_cpu(self):
        mem_desc, self._cpu_buf = _alloc_cpu_mem(8192)
        ret, handle = self.engine.register_mem(mem_desc, hixl.MemType.MEM_HOST)
        self._cpu_handle = handle
        self.assertEqual(ret, hixl.SUCCESS)
        self.assertNotEqual(handle, 0)

    def test_register_mem_npu(self):
        mem_desc, self._npu_buf = _alloc_npu_mem(4096)
        ret, handle = self.engine.register_mem(mem_desc, hixl.MemType.MEM_DEVICE)
        self._npu_handle = handle
        self.assertEqual(ret, hixl.SUCCESS)
        self.assertNotEqual(handle, 0)

    def test_deregister_mem(self):
        mem_desc, self._cpu_buf = _alloc_cpu_mem(4096)
        ret, handle = self.engine.register_mem(mem_desc, hixl.MemType.MEM_HOST)
        self._cpu_handle = handle
        if ret == hixl.SUCCESS:
            dereg_ret = self.engine.deregister_mem(handle)
            self.assertEqual(dereg_ret, hixl.SUCCESS)
            self._cpu_handle = 0

    def test_register_mem_returns_tuple(self):
        mem_desc, self._cpu_buf = _alloc_cpu_mem(4096)
        result = self.engine.register_mem(mem_desc, hixl.MemType.MEM_HOST)
        self._cpu_handle = result[1] if isinstance(result, tuple) else 0
        self.assertIsInstance(result, tuple)
        self.assertEqual(len(result), 2)


class _HixlEngineTestCase(unittest.TestCase):
    def setUp(self):
        logging.info("Begin %s", self._testMethodName)
        self.engine = _create_engine()

    def tearDown(self):
        logging.info("End %s", self._testMethodName)
        try:
            self.engine.finalize()
        except Exception:
            logging.exception("finalize failed during tearDown")


class HixlConnectDisconnectTest(_HixlEngineTestCase):
    def test_connect_no_server_returns_error(self):
        ret = self.engine.connect("127.0.0.1:19999", timeout_in_millis=1000)
        self.assertIn(
            ret, [hixl.TIMEOUT, hixl.FAILED, hixl.NOT_CONNECTED, hixl.PARAM_INVALID]
        )

    def test_disconnect_not_connected_returns_error(self):
        ret = self.engine.disconnect("127.0.0.1:19999", timeout_in_millis=1000)
        self.assertIn(ret, [hixl.NOT_CONNECTED, hixl.FAILED, hixl.PARAM_INVALID])

    def test_connect_default_timeout_returns_error(self):
        ret = self.engine.connect("127.0.0.1:19999")
        self.assertIn(
            ret, [hixl.TIMEOUT, hixl.FAILED, hixl.NOT_CONNECTED, hixl.PARAM_INVALID]
        )

    def test_disconnect_default_timeout_returns_error(self):
        ret = self.engine.disconnect("127.0.0.1:19999")
        self.assertIn(ret, [hixl.NOT_CONNECTED, hixl.FAILED, hixl.PARAM_INVALID])


class HixlAsyncConnectTest(_HixlEngineTestCase):
    def test_connect_async_no_server_returns_error(self):
        ret = self.engine.connect_async("127.0.0.1:19999", timeout_in_millis=1000)
        self.assertIn(
            ret,
            [
                hixl.SUCCESS,
                hixl.TIMEOUT,
                hixl.FAILED,
                hixl.NOT_CONNECTED,
                hixl.PARAM_INVALID,
            ],
        )

    def test_disconnect_async_not_connected_returns_error(self):
        ret = self.engine.disconnect_async("127.0.0.1:19999", timeout_in_millis=1000)
        self.assertIn(
            ret, [hixl.SUCCESS, hixl.NOT_CONNECTED, hixl.FAILED, hixl.PARAM_INVALID]
        )

    def test_get_async_connect_status_returns_enum(self):
        ret, status = self.engine.get_async_connect_status("127.0.0.1:19999")
        self.assertIsInstance(ret, int)
        self.assertIn(
            status,
            [
                hixl.AsyncConnectStatus.NOT_CONNECT,
                hixl.AsyncConnectStatus.CONNECT_PENDING,
                hixl.AsyncConnectStatus.CONNECTING,
                hixl.AsyncConnectStatus.CONNECTED,
                hixl.AsyncConnectStatus.CONNECT_FAILED,
                hixl.AsyncConnectStatus.DISCONNECT_PENDING,
                hixl.AsyncConnectStatus.DISCONNECTING,
            ],
        )

    def test_get_all_async_connect_status_returns_dict(self):
        ret, statuses = self.engine.get_all_async_connect_status()
        self.assertIsInstance(ret, int)
        self.assertIn(
            ret, [hixl.PARAM_INVALID, hixl.FAILED, hixl.NOT_CONNECTED, hixl.SUCCESS]
        )
        self.assertIsInstance(statuses, dict)
        for remote, status in statuses.items():
            self.assertIsInstance(remote, str)
            self.assertIsInstance(status, hixl.AsyncConnectStatus)


class HixlTransferTest(unittest.TestCase):
    def setUp(self):
        logging.info("Begin %s", self._testMethodName)
        self.engine = _create_engine()
        self._local_buf = None
        self._remote_buf = None
        self._local_handle = 0
        self._remote_handle = 0

    def tearDown(self):
        logging.info("End %s", self._testMethodName)
        if self._local_handle != 0:
            try:
                self.engine.deregister_mem(self._local_handle)
            except Exception:
                logging.exception(
                    "deregister_mem failed for _local_handle during tearDown"
                )
        if self._remote_handle != 0:
            try:
                self.engine.deregister_mem(self._remote_handle)
            except Exception:
                logging.exception(
                    "deregister_mem failed for _remote_handle during tearDown"
                )
        self._local_buf = None
        self._remote_buf = None
        try:
            self.engine.finalize()
        except Exception:
            logging.exception("finalize failed during tearDown")

    def test_transfer_sync_no_connection_returns_error(self):
        local_desc, self._local_buf = _alloc_cpu_mem(4096)
        op_desc = hixl.TransferOpDesc(
            local_addr=local_desc.addr, remote_addr=0x2000, len=4096
        )
        ret = self.engine.transfer_sync(
            "127.0.0.1:19999", hixl.TransferOp.READ, [op_desc], timeout_in_millis=1000
        )
        self.assertIn(
            ret, [hixl.NOT_CONNECTED, hixl.FAILED, hixl.TIMEOUT, hixl.PARAM_INVALID]
        )

    def test_transfer_sync_default_args_and_timeout(self):
        local_desc, self._local_buf = _alloc_cpu_mem(4096)
        op_desc = hixl.TransferOpDesc(
            local_addr=local_desc.addr, remote_addr=0x2000, len=4096
        )
        ret = self.engine.transfer_sync(
            "127.0.0.1:19999", hixl.TransferOp.READ, [op_desc]
        )
        self.assertIn(
            ret, [hixl.NOT_CONNECTED, hixl.FAILED, hixl.TIMEOUT, hixl.PARAM_INVALID]
        )

    def test_transfer_async_no_connection_returns_error(self):
        local_desc, self._local_buf = _alloc_cpu_mem(4096)
        op_desc = hixl.TransferOpDesc(
            local_addr=local_desc.addr, remote_addr=0x2000, len=4096
        )
        args = hixl.TransferArgs()
        ret, req_id = self.engine.transfer_async(
            "127.0.0.1:19999", hixl.TransferOp.READ, [op_desc], args
        )
        self.assertIsInstance(ret, int)
        self.assertIn(
            ret, [hixl.NOT_CONNECTED, hixl.FAILED, hixl.TIMEOUT, hixl.PARAM_INVALID]
        )
        self.assertEqual(req_id, 0)

    def test_transfer_async_default_args(self):
        local_desc, self._local_buf = _alloc_cpu_mem(4096)
        op_desc = hixl.TransferOpDesc(
            local_addr=local_desc.addr, remote_addr=0x2000, len=4096
        )
        ret, req_id = self.engine.transfer_async(
            "127.0.0.1:19999", hixl.TransferOp.READ, [op_desc]
        )
        self.assertIsInstance(ret, int)
        self.assertEqual(req_id, 0)

    def test_get_transfer_status_returns_enum(self):
        ret, status = self.engine.get_transfer_status(0)
        self.assertIsInstance(ret, int)
        self.assertIsInstance(status, hixl.TransferStatus)
        self.assertEqual(status, hixl.TransferStatus.FAILED)

    def test_get_all_transfer_status_returns_struct_list(self):
        query_args = hixl.GetTransferStatusArgs(max_query_count=100, skip_waiting=False)
        ret, results = self.engine.get_all_transfer_status(query_args)
        self.assertIsInstance(ret, int)
        self.assertIsInstance(results, list)
        for r in results:
            self.assertIsInstance(r, hixl.TransferResult)

    def test_get_all_transfer_status_default_args(self):
        ret, results = self.engine.get_all_transfer_status()
        self.assertIsInstance(ret, int)
        self.assertIsInstance(results, list)


class HixlNotifyTest(_HixlEngineTestCase):
    def test_send_notify_not_connected_returns_error(self):
        notify = hixl.NotifyDesc(name="signal_name", notify_msg="message_content")
        ret = self.engine.send_notify("127.0.0.1:19999", notify, timeout_in_millis=1000)
        self.assertIn(
            ret,
            [hixl.NOT_CONNECTED, hixl.FAILED, hixl.NOTIFY_FAILED, hixl.PARAM_INVALID],
        )

    def test_send_notify_default_timeout_returns_error(self):
        notify = hixl.NotifyDesc(name="signal_name", notify_msg="message_content")
        ret = self.engine.send_notify("127.0.0.1:19999", notify)
        self.assertIn(
            ret,
            [hixl.NOT_CONNECTED, hixl.FAILED, hixl.NOTIFY_FAILED, hixl.PARAM_INVALID],
        )

    def test_get_notifies_returns_struct_list(self):
        ret, notifies = self.engine.get_notifies()
        self.assertIsInstance(ret, int)
        self.assertEqual(ret, hixl.SUCCESS)
        self.assertIsInstance(notifies, list)
        self.assertEqual(notifies, [])


class HixlCapabilityTest(unittest.TestCase):
    def setUp(self):
        logging.info("Begin %s", self._testMethodName)

    def tearDown(self):
        logging.info("End %s", self._testMethodName)

    def test_get_capability_auto_connect(self):
        ret, value = hixl.get_capability(hixl.FeatureType.AUTO_CONNECT)
        self.assertIsInstance(ret, int)
        self.assertEqual(ret, hixl.SUCCESS)
        self.assertIn(value, [hixl.FEATURE_SUPPORTED, hixl.FEATURE_NOT_SUPPORTED])

    def test_get_capability_client_server_comm(self):
        ret, value = hixl.get_capability(hixl.FeatureType.CLIENT_SERVER_COMM)
        self.assertIn(value, [hixl.FEATURE_SUPPORTED, hixl.FEATURE_NOT_SUPPORTED])

    def test_get_capability_returns_known_status(self):
        result = hixl.get_capability(hixl.FeatureType.AUTO_CONNECT)
        self.assertIsInstance(result, tuple)
        self.assertEqual(len(result), 2)


class HixlFinalizedTest(unittest.TestCase):
    def setUp(self):
        logging.info("Begin %s", self._testMethodName)
        self.engine = _create_engine()
        self.engine.finalize()

    def tearDown(self):
        logging.info("End %s", self._testMethodName)

    def test_connect_after_finalize(self):
        ret = self.engine.connect("127.0.0.1:19999", timeout_in_millis=1000)
        self.assertEqual(ret, hixl.FAILED)

    def test_register_mem_after_finalize(self):
        desc = hixl.MemDesc(addr=0, len=4096)
        ret, handle = self.engine.register_mem(desc, hixl.MemType.MEM_DEVICE)
        self.assertEqual(ret, hixl.FAILED)
        self.assertEqual(handle, 0)

    def test_transfer_sync_after_finalize(self):
        op_desc = hixl.TransferOpDesc(local_addr=0, remote_addr=0, len=4096)
        ret = self.engine.transfer_sync(
            "127.0.0.1:19999", hixl.TransferOp.READ, [op_desc]
        )
        self.assertEqual(ret, hixl.FAILED)

    def test_get_async_connect_status_after_finalize(self):
        ret, status = self.engine.get_async_connect_status("127.0.0.1:19999")
        self.assertEqual(ret, hixl.FAILED)

    def test_get_transfer_status_after_finalize(self):
        ret, status = self.engine.get_transfer_status(0)
        self.assertEqual(ret, hixl.FAILED)

    def test_get_notifies_after_finalize(self):
        ret, notifies = self.engine.get_notifies()
        self.assertEqual(ret, hixl.FAILED)
        self.assertEqual(notifies, [])

    def test_get_all_transfer_status_after_finalize(self):
        ret, results = self.engine.get_all_transfer_status()
        self.assertEqual(ret, hixl.FAILED)
        self.assertEqual(results, [])

    def test_get_all_async_connect_status_after_finalize(self):
        ret, statuses = self.engine.get_all_async_connect_status()
        self.assertEqual(ret, hixl.FAILED)
        self.assertEqual(statuses, {})


class HixlTransferStatusEnumTest(_HixlEngineTestCase):
    def test_get_transfer_status_returns_enum_not_string(self):
        ret, status = self.engine.get_transfer_status(0)
        self.assertIsInstance(status, hixl.TransferStatus)
        self.assertEqual(status, hixl.TransferStatus.FAILED)
        self.assertEqual(int(status), 3)

    def test_async_connect_status_returns_enum_not_string(self):
        ret, status = self.engine.get_async_connect_status("127.0.0.1:19999")
        self.assertIsInstance(status, hixl.AsyncConnectStatus)


if __name__ == "__main__":
    unittest.main()
