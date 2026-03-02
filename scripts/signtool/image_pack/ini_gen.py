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

import xml.etree.ElementTree as ET
import hashlib
import argparse
import textwrap
import os
import logging

logging.basicConfig(
    format="[%(asctime)s] [%(levelname)s] [%(pathname)s] [line:%(lineno)d] %(message)s",
    level=logging.INFO,
)


def get_args():
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=textwrap.dedent(
            """
                                     A tool to generate a image ini file"""
        ),
    )
    parser.add_argument(
        "-in_xml", required=True, dest="inFilePath", help="The xml for get image list"
    )
    parser.add_argument(
        "--hash_list",
        required=False,
        help="gen cms image hash_list file",
        action="store_true",
    )
    parser.add_argument(
        "-hash_dest",
        required=False,
        dest="hash_list_path",
        help="hash_list file dest address",
    )
    parser.add_argument(
        "--hash_update",
        required=False,
        dest="new_image_name",
        help="update image hash to hashlist",
    )
    parser.add_argument(
        "-hash_list_img",
        required=False,
        dest="hash_list_img_path",
        help="hash_list img file path to add new hash",
    )
    return parser.parse_args()


def cal_image_hash(filepath):
    sha256_hash = hashlib.sha256()
    with open(filepath, "rb") as f:
        # Read and update hash string value in blocks of 4K
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()


def cal_fs_image_hash(filepath, roothash):
    sha256_hash = hashlib.sha256()
    with open(filepath, "rb") as f:
        # Read and update hash string value in blocks of 4K
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    hash_val = sha256_hash.hexdigest() + ";dm-roothash," + roothash
    logging.info("calculated fs hash: %s", hash_val)
    return hash_val


def _get_hash_for_elem(elem):
    """Calculate hash for an element based on its position attribute."""
    position = elem.get("position", "after_header")
    if position == "before_header":
        roothash = elem.get("roothash")
        return cal_fs_image_hash(elem.attrib["path"], roothash)
    else:
        return cal_image_hash(elem.attrib["path"])


def gen_ini():
    args = get_args()
    tree = ET.ElementTree(file=args.inFilePath)
    if tree.getroot().tag != "image_info":
        logging.error("error in input xml file")
    if args.hash_list:
        hash_list_path = os.path.join(
            args.hash_list_path, ("{}.img".format("hash-list"))
        )
        if os.path.exists(hash_list_path):
            os.remove(hash_list_path)
        for elem in tree.iter(tag="image"):
            if elem.attrib["tag"] == "hashlist":
                continue
            hash_val = _get_hash_for_elem(elem)
            with open(hash_list_path, "a+") as f:
                line_elem = [elem.attrib["tag"], hash_val]
                line = "{};".format(",".join(line_elem))
                f.write(line)
    else:
        for elem in tree.iter(tag="image"):
            hash_val = _get_hash_for_elem(elem)
            if hash_val == "":
                return -1
            if "ini_name" in elem.attrib:
                file_name = os.path.join(
                    elem.attrib["out"], f'{elem.attrib["ini_name"]}.ini'
                )
            else:
                file_name = os.path.join(
                    elem.attrib["out"], ("{}.ini".format(elem.attrib["tag"]))
                )
            with open(file_name, "w+") as f:
                line_elem = [elem.attrib["tag"], hash_val]
                line = "{};\n".format(",   ".join(line_elem))
                f.write(line)
    return 0


def _write_hash_with_logging(elem, hash_list_path):
    """写入哈希值并记录日志"""
    hash_val = cal_image_hash(elem.attrib["path"])
    with open(hash_list_path, "a+") as f:
        line_elem = [elem.attrib["tag"], hash_val]
        line = "{};".format(",".join(line_elem))
        f.write(line)
    logging.info("add %s hash val %s to %s", elem.attrib["tag"], hash_val, hash_list_path)


def _find_and_update_image_hash(tree, new_image_name, hash_list_path):
    """查找并更新指定镜像的哈希值"""
    for elem in tree.iter(tag="image"):
        if elem.attrib["tag"] == new_image_name:
            _write_hash_with_logging(elem, hash_list_path)
            return True
    return False


def update_hash():
    """更新镜像哈希值到hashlist文件"""
    args = get_args()
    tree = ET.ElementTree(file=args.inFilePath)
    logging.info("update_hash")

    if tree.getroot().tag != "image_info":
        logging.error("error in input xml file")
        return 1

    if not args.new_image_name:
        return 0

    hash_list_path = args.hash_list_img_path
    if not os.path.exists(hash_list_path):
        logging.error("input hashlist file not exist")
        return 1

    if not _find_and_update_image_hash(tree, args.new_image_name, hash_list_path):
        logging.warning("image %s not found in xml", args.new_image_name)

    return 0


def main():
    args = get_args()
    if args.new_image_name:
        update_hash()
    else:
        gen_ini()


if __name__ == "__main__":
    main()
