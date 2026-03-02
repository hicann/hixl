#!/usr/bin/env python
# -*- coding: utf-8 -*-
# -----------------------------------------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# -----------------------------------------------------------------------------------------------------------
#
#

import binascii
import os
import struct
import sys
from dataclasses import dataclass
from tools import cal_bin_hash, to_bytes


@dataclass
class HeaderConfig:
    """参数封装：__construct_header函数的参数"""
    n_buf: bytes
    e_buf: bytes
    hash_buf: bytes
    code_len: int
    suffix: bool
    head_type: int
    version: str
    nvcnt: str
    tag: str
    certtype: int
    before_header: bool = False
    large_packet: bool = False
    enc: bool = False
    pss: bool = False
    bcm: bool = False
    gcm: bool = False
    gm: bool = False


@dataclass
class HeaderWriteParams:
    """参数封装：__write_header函数的参数"""
    suffix: bool = False
    head_type: int = 0
    code_len: int = 0
    before_header: bool = False


@dataclass
class HeaderHashParams:
    """参数封装：__write_header_hash函数的参数"""
    suffix: bool = False
    head_type: int = 0
    sm: bool = False
    code_len: int = 0
    before_header: bool = False


@dataclass
class MagicNumberParams:
    """参数封装：__add_magic_number_and_file_size函数的参数"""
    cms_flag: bool
    suffix: bool = False
    code_len: int = 0
    before_header: bool = False
    large_packet: bool = False


# ==================== 私有函数 (相当于类的私有方法) ====================

def __init_header_values(config: HeaderConfig):
    """初始化头部基本值"""
    code_len = 0 if config.before_header and config.large_packet else config.code_len
    header_base = 0x1000 if config.head_type else 0
    zero_bytes_32 = int(0).to_bytes(32, "big") if sys.version > "3" else to_bytes(0, 32)
    return code_len, header_base, zero_bytes_32


def __get_basic_header_fields(config, code_len):
    """获取基本头部字段"""
    header_base = 0x1000 if config.head_type else 0
    return (
        0x55AA55AA,  # preamble
        int(0).to_bytes(20, "big"),  # rev0
        0x600,  # head_len
        0x0,  # user_len
        int(0).to_bytes(32, "big"),  # user_define_data
        config.hash_buf,  # code_hash
        0x600 + header_base,  # sub_key_cert_offset
        0x618,  # sub_cert_len
        0x0,  # uw_rootkey_alg
        0x8010000,  # img_sign_algo
        512,  # root_pubkey_len
    )


def __get_offset_fields(config, code_len):
    """获取偏移字段"""
    header_base = 0x1000 if config.head_type else 0
    return (
        0 if config.before_header else 0x2000,  # img_offset
        code_len,  # img_sign_obj_len
        header_base + 0xE00,  # sign_offset
    )


def __get_security_fields(zero_bytes_32):
    """获取安全相关字段"""
    return (
        0xFFFFFFFF,  # code_encrypt_flag
        0x2,  # code_encrypt_algo
        zero_bytes_32,  # derive_seed
        1000,  # km_ireation_cnt
        zero_bytes_32[:16],  # code_encrypt_iv
        zero_bytes_32[:16],  # code_encrypt_tag
        zero_bytes_32[:16],  # code_encrypt_add
    )


def __get_bcm_fields(config):
    """获取BCM相关字段"""
    if config.bcm:
        return 0x41544941, 0x800, 0x1000  # h2c_enable, h2c_cert_len, h2c_cert_offset
    return 0xA5A55555, 0, 0


def __get_tag_fields(config, zero_bytes_32):
    """获取标签相关字段"""
    code_tag = int(0).to_bytes(16, "big") if config.tag is None else bytes(str(config.tag), "ascii")
    ver_padding = binascii.a2b_hex("ff" * 0x10)
    padding = binascii.a2b_hex("ff" * 0x44)
    return code_tag, bytes(config.version, "ascii"), ver_padding, padding


def __build_header_fields(config: HeaderConfig, code_len, zero_bytes_32):
    """构建头部字段值"""
    # 基本字段
    preamble, rev0, head_len, user_len, user_define_data, code_hash = (
        0x55AA55AA,
        int(0).to_bytes(20, "big"),
        0x600,
        0x0,
        int(0).to_bytes(32, "big"),
        config.hash_buf,
    )

    header_base = 0x1000 if config.head_type else 0
    sub_key_cert_offset = header_base + 0x600
    sub_cert_len = 0x618
    uw_rootkey_alg = 0x0
    img_sign_algo = 0x8010000
    root_pubkey_len = 512

    # 偏移字段
    img_offset, img_sign_obj_len, sign_offset = __get_offset_fields(config, code_len)

    # 安全字段
    sign_len, code_encrypt_flag, code_encrypt_algo, derive_seed, \
            km_ireation_cnt, code_encrypt_iv, code_encrypt_tag, code_encrypt_add = (
        512, 0xFFFFFFFF, 0x2, zero_bytes_32, 1000,
        zero_bytes_32[:16], zero_bytes_32[:16], zero_bytes_32[:16]
    )

    # 保留字段
    rsv1 = int(0).to_bytes(88, "big")
    rsv2 = int(0).to_bytes(20, "big")

    # BCM字段
    h2c_enable, h2c_cert_len, h2c_cert_offset = __get_bcm_fields(config)

    # 其他字段
    root_pubkeyinfo = 0
    head_magic = 0x33CC33CC
    head_hash = zero_bytes_32
    cms_flag = int(0).to_bytes(16, "big")
    code_nvcnt = int(0).to_bytes(8, "big")

    # 标签字段
    code_tag, ver_value, ver_padding, padding = __get_tag_fields(config, zero_bytes_32)

    return (
        preamble, rev0, head_len, user_len, user_define_data, code_hash,
        sub_key_cert_offset, sub_cert_len, uw_rootkey_alg, img_sign_algo,
        root_pubkey_len, config.n_buf, config.e_buf, img_offset,
        img_sign_obj_len, sign_offset, sign_len, code_encrypt_flag,
        code_encrypt_algo, derive_seed, km_ireation_cnt, code_encrypt_iv,
        code_encrypt_tag, code_encrypt_add, rsv1, h2c_enable, h2c_cert_len,
        h2c_cert_offset, root_pubkeyinfo, rsv2, head_magic, head_hash,
        cms_flag, code_nvcnt, code_tag, ver_value, ver_padding,
        config.certtype, padding
    )


def __construct_header(config: HeaderConfig):
    """构建头部信息

    Args:
        config: 头部配置参数对象

    Returns:
        bytes: 打包后的头部数据
    """
    code_len, _, zero_bytes_32 = __init_header_values(config)
    pack_list = __build_header_fields(config, code_len, zero_bytes_32)

    s = struct.Struct(
        "I20sII32s32sIIIII512s512sIIIIII32sI16s16s16s88sIIII20sI32s16s8s16s16s16sI52s"
    )
    header = s.pack(*pack_list)
    return header


def __get_filelen(f):
    f.seek(0, 2)
    length = f.tell()
    f.seek(0)
    return length


def __write_header(out, header, params: HeaderWriteParams = None):
    if params is None:
        params = HeaderWriteParams()
    header_base = 0x1000 if params.head_type else 0
    header_base = (header_base + params.code_len) if params.before_header else header_base
    offset = (0xC000 + header_base) if params.suffix else header_base
    out.seek(offset)
    out.write(header)


def __write_header_hash(out, params: HeaderHashParams = None):
    if params is None:
        params = HeaderHashParams()
    header_base = 0x1000 if params.head_type else 0
    offset = header_base if (params.suffix or not params.before_header) else header_base + params.code_len
    out.seek(offset)
    header = out.read(0x560)
    if params.sm == False:
        out.write(cal_bin_hash(header))
    else:
        out.write(sm3_cal(header))


def __write_raw_img(out, raw, suffix=False, before_header=False):
    offset = 0 if suffix or before_header else 0x2000
    out.seek(offset)
    with open(raw, "rb") as raw_file:
        for byte_block in iter(lambda: raw_file.read(4096), b""):
            out.write(byte_block)


def __add_in_tail(offset, in_file, out_file, header, length):
    out_file.seek(offset, 1)
    out_file.write(header)
    out_file.write(in_file.read())
    out_file.seek(-(length + 16), 1)


def __construct_cms_header(tag, length):
    s = struct.Struct("12sI")
    if len(tag) > 11:
        raise RuntimeError("name too long")
    value = (tag.encode(), length)
    header = s.pack(*value)
    return header


def __write_cms(out, cms, aligned_bytes):
    with open(cms, "rb") as cf:
        length = __get_filelen(cf)
        header = __construct_cms_header("cms", length)
        __add_in_tail(aligned_bytes, cf, out, header, length)


def __write_ini(out, ini):
    with open(ini, "rb") as ifile:
        length = __get_filelen(ifile)
        header = __construct_cms_header("ini", length)
        __add_in_tail(16 * 1024, ifile, out, header, length)


def __write_crl(out, crl):
    with open(crl, "rb") as af:
        length = __get_filelen(af)
        header = __construct_cms_header("crl", length)
        __add_in_tail(2 * 1024, af, out, header, length)


def __write_single_header(args, out, hash_buf, code_len, head_type=0):
    if sys.version > "3":
        n_buf, e_buf = int(0).to_bytes(512, "big"), int(0).to_bytes(512, "big")
    else:
        n_buf, e_buf = to_bytes(0, 512), to_bytes(0, 512)

    before_header = True if (args.position == "before_header") else False
    large_packet = True if (args.pkt_type == "large_pkt") else False

    # 创建头部配置对象
    header_config = HeaderConfig(
        n_buf=n_buf,
        e_buf=e_buf,
        hash_buf=hash_buf,
        code_len=code_len,
        suffix=args.S,
        head_type=head_type,
        version=args.ver,
        nvcnt=args.nvcnt,
        tag=args.tag,
        certtype=args.certtype,
        before_header=before_header,
        large_packet=large_packet,
        enc=False,
        pss=False,
        bcm=False,
        gcm=False,
        gm=False,
    )
    header = __construct_header(header_config)
    __write_header(out, header, HeaderWriteParams(args.S, head_type, code_len, before_header))


def __add_magic_number_and_file_size(args, out, params: MagicNumberParams):
    if params.cms_flag and params.suffix:
        raise RuntimeError(
            "Invalid Param: --addcms and -S can't input in the same time."
        )

    out.seek(0, 2)
    # if before_header img code_len >= 4G, img_len = 0 (not used)
    file_size = 0 if params.before_header and (out.tell() >= 0x100000000) else out.tell()
    s = struct.Struct("QI")
    if params.cms_flag:
        value = (0xABCD1234AA55AA55, file_size)
    else:
        value = (0x0, file_size)
    stream = s.pack(*value)
    offset = params.code_len + 0x580 if params.before_header else 0x580
    out.seek(offset, 0)
    out.write(stream)

    # Write additional nvcnt to head at offset 0x590
    if args.nvcnt:
        s = struct.Struct("II")
        nvcnt_magic = 0x5A5AA5A5
        pack_list = (nvcnt_magic, int(args.nvcnt))
        nvcnt_s = s.pack(*pack_list)
        nvcnt_offset = params.code_len + 0x590 if params.before_header else 0x590
        out.seek(nvcnt_offset)
        out.write(nvcnt_s)

    if params.before_header:
        offset = params.code_len + 0x4E0
        out.seek(offset, 0)
        out.write(params.code_len.to_bytes(8, "little"))


# ==================== 公共函数 ====================

def write_header_huawei(args, out, hash_buf, code_len):
    __write_single_header(args, out, hash_buf, code_len)


def write_image(args, out):
    __write_raw_img(out, args.raw, args.S, False)
    __write_header_hash(out, HeaderHashParams(args.S, 0, False, 0, False))


def write_cms(args, out, code_len):
    if args.addcms:
        out.seek(code_len + 0x2000)
        if args.position == "before_header":
            __write_cms(out, args.cms, 0)
        else:
            __write_cms(out, args.cms, 32 - code_len % 16)
        __write_ini(out, args.ini)
        __write_crl(out, args.crl)


def write_extern(args, out, data_list):
    before_header = True if (args.position == "before_header") else False
    code_len = data_list[0] if before_header else 0
    if before_header:
        __write_header_hash(out, HeaderHashParams(args.S, 0, args.sm, code_len, before_header))
    __add_magic_number_and_file_size(
        args, out, MagicNumberParams(args.addcms, False, code_len, before_header)
    )
    return


def write_hash_tree(args, out, code_len):
    hash_tree_offset = code_len + 0x20000 - 0x100  # 128K
    out.seek(hash_tree_offset)
    hash_tree_path = os.path.join(os.path.dirname(args.raw), "hashtree")
    with open(hash_tree_path, "rb") as hash_tree_file:
        hash_tree_content = hash_tree_file.read()
        out.write(hash_tree_content)


def write_header_huawei_address(args, out, code_len):
    partition_size = int(args.partition_size) * 1024 * 1024
    if args.pkt_type == "large_pkt":
        header_huawei_address_offset = partition_size - 0xC
        out.seek(header_huawei_address_offset)
        out.write(code_len.to_bytes(8, "little"))
    else:
        header_huawei_address_offset = partition_size - 0x8
        out.seek(header_huawei_address_offset)
        out.write(code_len.to_bytes(4, "little"))


def write_version(args, out, code_len):
    partition_size = int(args.partition_size) * 1024 * 1024
    version_offset = partition_size - 0x4
    out.seek(version_offset)
    if args.pkt_type == "large_pkt":
        version = int(1279739216).to_bytes(4, "little")  # magic LGEP(large packet)
    else:
        version = int(0).to_bytes(4, "little")
    out.write(version)
