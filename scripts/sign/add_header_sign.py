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
"""
#
#
#**************************************************************
# 文件名    ：add_sign_header_cann.py
# 版本号    ：初稿
# 生成日期  ：2025年11月25日
# 功能描述  ：根据bios_check_cfg.xml配置文件，根据其中配置属性，对各文件进行制作cms签名并绑定
# 使用方法  ：python add_bios_header.py $(DEVICE_RELEASE_DIR) $(DAVINCI_TOPDIR) $(PRODUCT_NAME) $(CHIP_NAME) $(signature_tag)
# 输入参数  ：DEVICE_RELEASE_DIR：待签名文件的根目录
#            DAVINCI_TOPDIR：工程根路径
#            PRODUCT_NAME：待扫描的产品名
#            CHIP_NAME：芯片名称
#            signature_tag：是否需要数字签名
#            蓝区签名步骤 1、加esbc头; 2、生成ini文件; 3、进行签名（参数控制，暂不启动）; 4、签名结果写入文件头
# 返回值    ：0:成功，-1:失败
# 修改历史  ：
# 日期    ：2025年11月25日
# 修改内容  ：创建文件
"""
import shlex
import argparse
import logging
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import namedtuple
from dataclasses import dataclass
from typing import Dict, List, Tuple, Optional

import common_log as COMM_LOG


def _run_cmd(cmd: str) -> Tuple[int, str]:
    """Run command without shell=True, returns (exitcode, output)."""
    try:
        result = subprocess.run(
            shlex.split(cmd),
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        return (result.returncode, result.stdout)
    except Exception as e:
        return (1, str(e))

logging.basicConfig(
    format="[%(asctime)s] [%(levelname)s] [%(pathname)s] [line:%(lineno)d] %(message)s",
    level=logging.INFO,
)

THIS_FILE_NAME = __file__
THIS_FILE_PATH = os.path.realpath(THIS_FILE_NAME)
MY_PATH = os.path.dirname(THIS_FILE_PATH)

PATH_SEPARATOR = "/"


class AddHeaderConfig:
    """加头工具命令行配置类"""

    def __init__(
        self,
        inputfile,
        output,
        version,
        fw_version,
        inputtype,
        tag,
        rootrsa,
        subrsa,
        additional,
        sign_alg,
        encrypt_alg,
        encrypt_type,
        nvcnt,
        rsatag,
        position,
        image_pack_version,
        bist_flag,
    ):
        self.input = inputfile
        self.output = output
        self.version = version
        self.fw_version = fw_version
        self.rootrsa = rootrsa
        self.subrsa = subrsa
        self.additional = additional
        self.type = inputtype
        self.tag = tag
        self.sign_alg = sign_alg
        self.encrypt_alg = encrypt_alg
        self.encrypt_type = encrypt_type
        self.nvcnt = nvcnt
        self.rsatag = rsatag
        self.position = position
        self.image_pack_version = image_pack_version
        self.bist_flag = bist_flag


# 加nvcnt头配置类
AddNvcntHeaderConfig = namedtuple("AddNvcntHeaderConfig", ["inputfile", "nvcnt"])


@dataclass
class BuildIniParams:
    """参数封装：build_inifile函数的参数"""
    item_size_set: Dict
    sign_file_dir: str
    bios_tool_path: str
    sign_tmp_path: str
    product_delivery_path: str
    add_sign: str


@dataclass
class BuildSignParams:
    """参数封装：build_sign函数的参数"""
    item_size_set: Dict
    sign_file_dir: str
    sign_tool_path: str
    sign_tmp_path: str
    root_dir: str
    product_delivery_path: str


@dataclass
class AddHeaderParams:
    """参数封装：add_bios_header函数的参数"""
    item_size_set: Dict
    sign_file_dir: str
    bios_tool_path: str
    sign_tool_path: str
    root_dir: str
    add_sign: str


@dataclass
class CmsSignCmdParams:
    """参数封装：_build_cms_sign_cmd函数的参数"""
    cmd: str
    input_file: str
    conf_item: AddHeaderConfig
    sign_path: str
    input_name: str
    der_file: str


@dataclass
class ImageCommandParams:
    """参数封装：_build_image_command函数的参数"""
    bios_tool_path: str
    add_sign: str
    input_file: str
    sign_path: str
    input_name: str
    der_file: str
    conf_item: AddHeaderConfig


def read_xml(in_path):
    """
    功能：读取XML
    """
    tree = ET.ElementTree()
    tree.parse(in_path)
    return tree


def check_config_item(node) -> bool:
    """校验节点必需属性"""
    if "input" not in node.attrib or "output" not in node.attrib:
        COMM_LOG.cilog_error(
            THIS_FILE_NAME, "bios_check_cfg.xml config format is invalid"
        )
        return False

    if "type" in node.attrib:
        if "cms" in node.attrib["type"].split("/") and "tag" not in node.attrib:
            COMM_LOG.cilog_error(
                THIS_FILE_NAME,
                "when bios_check_cfg.xml has cms type, it must has 'tag' attribute",
            )
            return False

    return True


def _get_xml_attr(node, attr_name, default=None):
    """从XML节点获取属性值，如果不存在则返回默认值"""
    if default is None:
        default = ""
    return node.attrib.get(attr_name, default)


def parse_item(node):
    """解析XML节点，返回AddHeaderConfig配置对象"""
    tag_type = _get_xml_attr(node, "type")
    sign_alg = _get_xml_attr(node, "sign_alg", "PKCSv1.5")
    encrypt_alg = _get_xml_attr(node, "encrypt_alg")
    encrypt_type = _get_xml_attr(node, "encrypt_type")
    add_para = _get_xml_attr(node, "additional")
    add_tag = _get_xml_attr(node, "tag", [])
    nvcnt = _get_xml_attr(node, "nvcnt")
    rsatag = _get_xml_attr(node, "rsatag")
    position = _get_xml_attr(node, "position")
    image_pack_version = _get_xml_attr(node, "image_pack", "1.0")
    rootrsa = _get_xml_attr(node, "rootrsa", "default_rsa_rootkey")
    subrsa = _get_xml_attr(node, "subrsa", "default_rsa_subkey")
    bist_flag = _get_xml_attr(node, "bist_flag")

    cur_conf = AddHeaderConfig(
        node.attrib["input"],
        node.attrib["output"],
        node.attrib["version"],
        node.attrib.get("fw_version", ""),
        tag_type,
        add_tag,
        rootrsa,
        subrsa,
        add_para,
        sign_alg,
        encrypt_alg,
        encrypt_type,
        nvcnt,
        rsatag,
        position,
        image_pack_version,
        bist_flag,
    )
    return cur_conf


def get_item_set(config_file, sign_file_dir, version) -> Tuple[int, Dict, List]:
    """
    功能：解析xml配置文件
    """
    item_size_set = {}
    ini_size_set = {}
    tree = read_xml(config_file)
    origin_nodes = tree.findall("item")
    ini_nodes = tree.findall("ini")

    for node in origin_nodes:
        if "version" not in node.attrib:
            node.attrib["version"] = version
        if not check_config_item(node):
            return -1, None, None, None

    # 排除不存在的文件
    nodes = []
    for node in origin_nodes:
        input_file = os.path.join(sign_file_dir, node.attrib["input"])

        if os.path.exists(input_file):
            nodes.append(node)
        else:
            COMM_LOG.cilog_warning(
                THIS_FILE_NAME, "Image file:%s not exits!\n\t", input_file
            )
            continue

    for node in nodes:
        cur_conf = parse_item(node)
        item_size_set[cur_conf.input] = cur_conf

    for ini_node in ini_nodes:
        cur_conf = parse_item(ini_node)
        ini_size_set[cur_conf.input] = cur_conf

    nvcnt_configs = []
    for node in nodes:
        if "nvcnt" in node.attrib:
            inputfile = os.path.join(sign_file_dir, node.attrib["input"])
            nvcnt = node.attrib["nvcnt"]

            config = AddNvcntHeaderConfig(inputfile, nvcnt)
            nvcnt_configs.append(config)

    return 0, item_size_set, ini_size_set, nvcnt_configs


# 生成摘要文件，每个待签名文件生成一个，生成文件相关的参数放在image_info.xml文件中
def _prepare_output_directory(sign_tmp_path, relative_path):
    """准备输出目录，返回输出路径"""
    output_path = os.path.dirname(
        os.path.join(sign_tmp_path, relative_path)
    )
    output_path = os.path.realpath(output_path)
    if not os.path.isdir(output_path):
        os.makedirs(output_path)
    return output_path


def _should_write_cms_config(conf_item, inputfile):
    """判断是否需要写入CMS配置"""
    return (
        "cms" in conf_item.type.split("/")
        and conf_item.tag != ""
        and os.path.isfile(inputfile)
    )


def _write_image_info_config(read_cfg, item_size_set, sign_file_dir,
                              sign_tmp_path, product_delivery_path):
    """写入image_info.xml配置文件内容，返回是否有CMS标志"""
    cms_flag = False
    for infile, conf_item in list(item_size_set.items()):
        inputfile = os.path.join(sign_file_dir, infile)
        relative_path = inputfile.replace(
            product_delivery_path + PATH_SEPARATOR, ""
        )
        output_path = _prepare_output_directory(sign_tmp_path, relative_path)

        if _should_write_cms_config(conf_item, inputfile):
            cms_flag = True
            read_cfg.write(
                '<image path="%s" out="%s" tag="%s" ini_name="%s"/>\n'
                % (
                    inputfile,
                    output_path,
                    conf_item.tag,
                    os.path.basename(infile),
                )
            )
    return cms_flag


def build_inifile(params: BuildIniParams) -> int:
    """
    功能：根据从bios_check_cfg.xml读取的配置，生成ini工具(ini_gen.py)的配置文件，
    然后调用ini工具读取该配置文件生成每个文件对应的ini文件
    输入：params: BuildIniParams封装的参数
    返回：-1:失败，0：成功
    """
    item_size_set = params.item_size_set
    sign_file_dir = params.sign_file_dir
    bios_tool_path = params.bios_tool_path
    sign_tmp_path = params.sign_tmp_path
    product_delivery_path = params.product_delivery_path
    add_sign = params.add_sign
    cms_flag = False

    if add_sign == "true":
        inicfg = os.path.join(sign_tmp_path, "image_info.xml")
        with open(inicfg, "w+", encoding="utf-8") as read_cfg:
            read_cfg.write("<image_info>\n")
            cms_flag = _write_image_info_config(
                read_cfg, item_size_set, sign_file_dir,
                sign_tmp_path, product_delivery_path
            )
            read_cfg.write("</image_info>\n")
        gen_tool = os.path.join(bios_tool_path, "ini_gen.py")
        cmd = "%s %s -in_xml %s" % (os.environ["HI_PYTHON"], gen_tool, inicfg)

    if add_sign == "true" and cms_flag:
        COMM_LOG.cilog_info(THIS_FILE_NAME, "------------------------------------")
        COMM_LOG.cilog_info(THIS_FILE_NAME, "execute:%s", cmd)
        ret = _run_cmd(cmd)
        if ret[0] != 0:
            COMM_LOG.cilog_error(
                THIS_FILE_NAME, "build inifile failed!\n\t%s", (ret[1])
            )
            return -1
    return 0


def _collect_sign_files(item_size_set, sign_file_dir):
    """收集需要签名的文件，返回sign_dict"""
    sign_dict = {}
    sign_dict["cms"] = []
    for infile, conf_item in list(item_size_set.items()):
        input_path = os.path.join(sign_file_dir, infile)
        if not os.path.exists(input_path):
            COMM_LOG.cilog_error(THIS_FILE_NAME, "infile is not exist:%s", input_path)
            return {}, -1

        cmd = "ls {}".format(input_path)
        ret = _run_cmd(cmd)
        if ret[0] != 0:
            COMM_LOG.cilog_warning(
                THIS_FILE_NAME,
                "can not find %s in %s \n\t%s",
                input_path,
                sign_file_dir,
                ret[1],
            )
            continue

        for sign in conf_item.type.split("/"):
            if sign in sign_dict:
                sign_dict[sign].append(infile)
    return sign_dict, 0


def _prepare_sign_file(file, sign_file_dir, sign_tmp_path, product_delivery_path):
    """准备签名文件：拷贝到临时目录并返回ini文件路径"""
    file_with_path = os.path.join(sign_file_dir, file)
    relative_path = file_with_path.replace(
        ("{}" + PATH_SEPARATOR).format(product_delivery_path), ""
    )
    file_sign_des = os.path.realpath(os.path.join(sign_tmp_path, relative_path))
    sign_path = os.path.dirname(file_sign_des)

    if not os.path.isdir(sign_path):
        os.makedirs(sign_path)
    if os.path.isfile(file_with_path):
        COMM_LOG.cilog_info(
            THIS_FILE_NAME, "copy %s --> %s", file_with_path, file_sign_des
        )
        shutil.copy(file_with_path, file_sign_des)
        if not os.path.isfile(file_sign_des):
            COMM_LOG.cilog_error(
                THIS_FILE_NAME, "copy %s --> %s fail", file_with_path, file_sign_des
            )
            return None, -1
    else:
        COMM_LOG.cilog_error(THIS_FILE_NAME, "can not find src:%s", file_with_path)
        return None, -1

    ini_file = "{}.ini".format(os.path.join(sign_path, os.path.basename(file)))
    return ini_file, 0


def _build_sign_command(sign_tool_path, root_dir, ini_files):
    """构建签名命令"""
    cmd = "{} {} {}".format(
        os.environ["HI_PYTHON"], sign_tool_path, root_dir
    )
    for ini_file in ini_files:
        cmd = "{} {}".format(cmd, ini_file)
    return cmd


def build_sign(params: BuildSignParams) -> int:
    """
    功能：制作签名文件
    输入：params: BuildSignParams封装的参数
    返回：-1:失败，0：成功
    """
    item_size_set = params.item_size_set
    sign_file_dir = params.sign_file_dir
    sign_tool_path = params.sign_tool_path
    sign_tmp_path = params.sign_tmp_path
    root_dir = params.root_dir
    product_delivery_path = params.product_delivery_path

    sign_dict, ret = _collect_sign_files(item_size_set, sign_file_dir)
    if ret != 0:
        return -1

    ini_files = []
    for file in sign_dict["cms"]:
        ini_file, ret = _prepare_sign_file(file, sign_file_dir, sign_tmp_path, product_delivery_path)
        if ret != 0:
            return -1
        logging.info("ini file prepared for signing: %s", ini_file)
        ini_files.append(ini_file)

    cmd = _build_sign_command(sign_tool_path, root_dir, ini_files)
    COMM_LOG.cilog_info(THIS_FILE_NAME, "------------------------------------")
    COMM_LOG.cilog_info(THIS_FILE_NAME, "execute:%s", cmd)
    ret = _run_cmd(cmd)
    if ret[0] != 0:
        COMM_LOG.cilog_error(
            THIS_FILE_NAME, "make cms sign failed!\n\t%s", ret[1]
        )
        return -1
    COMM_LOG.cilog_info(THIS_FILE_NAME, "%s", ret[1])

    return 0


# 添加esbc头，支持在不签名的情况下添加头，包含版本、tag、chip、nvcnt信息
def add_bios_esbc_header(root_dir, item_size_set, sign_file_dir):
    """
    功能：需要做RSA签名绑定的镜像绑定esbc二级头
    输入：para1：davinci工程路径、 para2: 待签名的镜像清单、 para3: 镜像根路径
    返回：-1:失败，0：成功
    """
    bios_esbc_header_tool_path = os.path.join(
        root_dir, "scripts", "signtool", "esbc_header"
    )
    # 检查加头工具目录是否存在
    if not os.path.exists(bios_esbc_header_tool_path):
        COMM_LOG.cilog_error(THIS_FILE_NAME, "bios esbc tool dir not exits")
        return -1

    for input_filename, conf_item in list(item_size_set.items()):
        input_file = os.path.join(sign_file_dir, input_filename)

        if conf_item.nvcnt:
            cmd = f'{os.environ["HI_PYTHON"]} {os.path.join(bios_esbc_header_tool_path, "esbc_header.py")}'
            # 用esbc_header.py工具脚本添加esbc头
            cmd += f" -raw_img {input_file} -out_img {input_file}"
            cmd += f" -version {conf_item.version} -nvcnt {conf_item.nvcnt} -tag {conf_item.tag}"

            COMM_LOG.cilog_info(THIS_FILE_NAME, "------------------------------------")
            COMM_LOG.cilog_info(THIS_FILE_NAME, "execute:%s", cmd)
            ret = _run_cmd(cmd)
            if ret[0] != 0:
                COMM_LOG.cilog_error(
                    THIS_FILE_NAME,
                    "add %s esbc header failed!\n\t%s",
                    input_file,
                    ret[1],
                )
                return -1
        else:
            COMM_LOG.cilog_info(
                THIS_FILE_NAME, "%s don't need add esbc head!\n", input_file
            )
    return 0


def convert_der_file(crl_file: str, der_file: str) -> int:
    """
    将 PEM 格式的 CRL 文件转换为 DER 格式，并保存到临时目录。
    返回值：
        0 - 成功
        1 - 失败（包括文件不存在、OpenSSL 未安装、转换失败等）
    """
    try:
        # 检查输入文件是否存在
        if not os.path.isfile(crl_file):
            logging.error("Input CRL file not found: %s", crl_file)
            return 1
        # 调用 openssl 转换
        cmd = f"openssl crl -in {crl_file} -outform DER -out {der_file}"
        result = _run_cmd(cmd)
        if result[0] != 0:
            logging.error("OpenSSL conversion failed: %s", result[1])
            return 1
        return 0
    except Exception as e:
        logging.error("Unexpected error while converting CRL: %s", e)
        return 1


# 1 生成ini文件、2 添加esbc头、 3 执行签名、4 合入文件头
def _prepare_sign_environment(root_dir):
    """准备签名临时目录"""
    sign_tmp_path = os.path.join(root_dir, "sign_tmp")
    if not os.path.isdir(sign_tmp_path):
        os.makedirs(sign_tmp_path)
    return sign_tmp_path


def _prepare_crl_file(root_dir):
    """准备CRL文件，返回der文件路径"""
    signature_path = os.path.join(root_dir, "scripts", "signtool", "signature")
    crl_file = os.path.join(signature_path, "SWSCRL.crl")
    der_file = os.path.join(signature_path, "SWSCRL.der")
    if not os.path.exists(der_file):
        convert_der_file(crl_file, der_file)
    return der_file


def _build_base_command(bios_tool_path):
    """构建基础命令"""
    return "{} {}".format(
        os.environ["HI_PYTHON"], os.path.join(bios_tool_path, "image_pack.py")
    )


def _build_no_sign_cmd(cmd, input_file, conf_item):
    """构建无签名命令"""
    cmd = cmd + " -raw_img %s -out_img %s -version %s -nvcnt %s -tag %s" % (
        input_file,
        input_file,
        conf_item.version,
        conf_item.nvcnt,
        conf_item.tag,
    )
    if conf_item.position != "":
        cmd = cmd + " -position %s" % (conf_item.position)
    return cmd


def _build_cms_sign_cmd(params: CmsSignCmdParams) -> str:
    """构建CMS签名命令"""
    cmd = params.cmd
    add_cmd = params.conf_item.additional

    for sign in params.conf_item.type.split("/"):
        cmd = (
            cmd
            + " -raw_img %s -out_img %s -version %s -nvcnt %s -tag %s %s"
            % (
                params.input_file,
                params.input_file,
                params.conf_item.version,
                params.conf_item.nvcnt,
                params.conf_item.tag,
                add_cmd,
            )
        )

        if sign == "cms":
            cmd = _add_cms_params(cmd, params)

    return cmd


def _add_cms_params(cmd: str, params: CmsSignCmdParams) -> str:
    """添加CMS签名参数到命令"""
    ini_file = os.path.join(params.sign_path, os.path.basename(params.input_name))
    cmd = (
        cmd
        + " -cms %s.ini.p7s -ini %s.ini -crl %s -certtype 1 --addcms"
        % (ini_file, ini_file, params.der_file)
    )
    if params.conf_item.position != "":
        cmd = cmd + " -position %s" % (params.conf_item.position)
    return cmd


def _get_image_paths(input_name, sign_file_dir, sign_tmp_path, product_delivery_path):
    """获取镜像相关路径"""
    input_file = os.path.join(sign_file_dir, input_name)
    relative_path = input_file.replace(
        ("{}" + PATH_SEPARATOR).format(product_delivery_path), ""
    )
    sign_file = os.path.realpath(os.path.join(sign_tmp_path, relative_path))
    sign_path = os.path.dirname(sign_file)
    return input_file, sign_path


def _build_image_command(params: ImageCommandParams) -> Optional[str]:
    """构建镜像处理命令"""
    cmd = _build_base_command(params.bios_tool_path)

    if params.add_sign != "true" or params.conf_item.type == "":
        return _build_no_sign_cmd(cmd, params.input_file, params.conf_item)
    elif params.add_sign == "true" and params.conf_item.type != "":
        cms_params = CmsSignCmdParams(
            cmd, params.input_file, params.conf_item,
            params.sign_path, params.input_name, params.der_file
        )
        return _build_cms_sign_cmd(cms_params)
    else:
        return None


def _execute_image_command(cmd, input_file):
    """执行镜像处理命令"""
    if cmd is None:
        COMM_LOG.cilog_error(
            THIS_FILE_NAME,
            "bios_check_cfg.xml config format is invalid, %s is not correct!,please check!",
            input_file,
        )
        return -1

    COMM_LOG.cilog_info(THIS_FILE_NAME, "------------------------------------")
    COMM_LOG.cilog_info(THIS_FILE_NAME, "execute:%s", cmd)
    ret = _run_cmd(cmd)
    if ret[0] != 0:
        COMM_LOG.cilog_error(
            THIS_FILE_NAME, "add %s header failed!\n\t%s", input_file, ret[1]
        )
        return -1
    return 0


def _process_image_headers(params, der_file, sign_tmp_path, product_delivery_path):
    """处理所有镜像的头部绑定"""
    item_size_set = params.item_size_set
    sign_file_dir = params.sign_file_dir

    for input_name, conf_item in list(item_size_set.items()):
        input_file, sign_path = _get_image_paths(
            input_name, sign_file_dir, sign_tmp_path, product_delivery_path
        )

        cmd_params = ImageCommandParams(
            params.bios_tool_path,
            params.add_sign,
            input_file,
            sign_path,
            input_name,
            der_file,
            conf_item,
        )
        cmd = _build_image_command(cmd_params)
        ret_code = _execute_image_command(cmd, input_file)
        if ret_code != 0:
            return ret_code

    return 0


def _setup_ini_generation(params, sign_tmp_path, product_delivery_path):
    """设置ini文件生成"""
    return build_inifile(
        BuildIniParams(
            params.item_size_set,
            params.sign_file_dir,
            params.bios_tool_path,
            sign_tmp_path,
            product_delivery_path,
            params.add_sign,
        )
    )


def _setup_sign_generation(params, sign_tmp_path, product_delivery_path):
    """设置签名生成"""
    return build_sign(
        BuildSignParams(
            params.item_size_set,
            params.sign_file_dir,
            params.sign_tool_path,
            sign_tmp_path,
            params.root_dir,
            product_delivery_path,
        )
    )


def add_bios_header(params: AddHeaderParams) -> int:
    """生成每个镜像的签名并绑定"""
    # 准备临时目录
    sign_tmp_path = _prepare_sign_environment(params.root_dir)
    product_delivery_path = os.path.join(params.root_dir)

    # 添加ESBC头
    ret_code = add_bios_esbc_header(params.root_dir, params.item_size_set, params.sign_file_dir)
    if ret_code != 0:
        return ret_code

    # 生成ini文件
    ret_code = _setup_ini_generation(params, sign_tmp_path, product_delivery_path)
    if ret_code != 0:
        return ret_code

    # 执行签名
    if params.add_sign == "true":
        ret_code = _setup_sign_generation(params, sign_tmp_path, product_delivery_path)
        if ret_code != 0:
            return ret_code

    # 准备CRL文件
    der_file = _prepare_crl_file(params.root_dir)

    # 处理镜像头部绑定
    ret_code = _process_image_headers(params, der_file, sign_tmp_path, product_delivery_path)
    if ret_code != 0:
        return ret_code

    # 删除中间残留文件
    if os.path.isdir(sign_tmp_path):
        shutil.rmtree(sign_tmp_path)

    COMM_LOG.cilog_info(THIS_FILE_NAME, "add header to all bios image success!")
    return 0


def check_params(params):
    """检查参数。"""
    # 检查BIOS配置文件是否存在
    if not os.path.exists(params["config_file"]):
        COMM_LOG.cilog_error(
            THIS_FILE_NAME,
            "bios image header config file not exits:%s",
            params["config_file"],
        )
        return -1
    # 检查BIOS工具是否存在
    if not os.path.exists(params["bios_tool_path"]):
        COMM_LOG.cilog_error(THIS_FILE_NAME, "biostool dir not exits")
        return -1
    # 检查签名脚本是否存在
    logging.info("sign tool path: %s", params["sgn_tool_path"])
    if not os.path.exists(params["sgn_tool_path"]):
        COMM_LOG.cilog_error(THIS_FILE_NAME, "sign tools script not exits")
        return -1
    return 0


def _define_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser()
    parser.add_argument("sign_file_dir", help="device release dir")
    parser.add_argument("sign_flag", help="sign flag", default="false")
    parser.add_argument(
        "--bios_check_cfg",
        help="default bios_check_cfg.xml",
        default="bios_check_cfg.xml",
    )
    # 签名版本信息，编译传入
    parser.add_argument("--version", help="version")
    # 签名插件脚本
    parser.add_argument("--sign_script", help="sign、 script", default="")
    return parser


def setenv():
    """设置环境变量。"""
    if "HI_PYTHON" not in os.environ:
        os.environ["HI_PYTHON"] = os.path.basename(sys.executable)


def main(argv=None):
    """
    主函数，检查输入参数及环境检查,并调用功能函数
    """
    parser = _define_parser()
    args = parser.parse_args()
    sign_file_dir = args.sign_file_dir
    add_sign = args.sign_flag
    bios_check_cfg = (
        args.bios_check_cfg if args.bios_check_cfg else "bios_check_cfg.xml"
    )
    version = args.version
    # 如果add_sign是false，则直接返回，加头&签名均不执行
    if add_sign == "false":
        return 0
    # 将MY_PATH的父目录赋值给root_dir
    root_dir = os.path.dirname(os.path.dirname(MY_PATH))

    # 需要签名的文件清单
    config_file = os.path.join(root_dir, bios_check_cfg)
    COMM_LOG.cilog_info(THIS_FILE_NAME, "config_file=" + config_file)

    bios_tool_path = os.path.join(root_dir, "scripts", "signtool", "image_pack")

    sgn_tool_path = os.path.join(root_dir, "scripts", "sign", "community_sign_build.py")
    # 判断args.sign_script是否存在值，签名插件脚本路径
    if hasattr(args, "sign_script") and args.sign_script:
        sgn_tool_path = args.sign_script

    ret_code = check_params(
        {
            "config_file": config_file,
            "bios_tool_path": bios_tool_path,
            "sign_file_dir": sign_file_dir,
            "sgn_tool_path": sgn_tool_path,
            "add_sign": add_sign,
        }
    )

    if ret_code != 0:
        return ret_code

    setenv()

    # 读取并解析BIOS的签名、绑定配置文件，并将解析的时候存储在item_size_set变量中
    ret_code, item_size_set, ini_size_set, nvcnt_configs = get_item_set(
        config_file, sign_file_dir, version
    )
    if ret_code != 0:
        return ret_code

    # 调用签名插件对需要签名的镜像进行签名，并绑定镜像文件
    ret_code = add_bios_header(
        AddHeaderParams(
            item_size_set, sign_file_dir, bios_tool_path, sgn_tool_path, root_dir, add_sign
        )
    )
    return ret_code


if __name__ == "__main__":
    sys.exit(main())
