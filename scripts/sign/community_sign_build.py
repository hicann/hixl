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
# 签名命令：./client --config ./client.toml add --file-type p7s --key-type x509 --key-name SignCert --detached infile
# 传入签名文件
# 解析参数
# 验证参数
# 执行签名命令
# 判断.p7s文件是否生成，没有生成则签名失败
# 返回执行结果
# Purpose:

import os
import sys
import logging
import subprocess
from subprocess import PIPE, STDOUT
from typing import List, Optional, Tuple

myfile = os.path.realpath(__file__)
mypath = os.path.dirname(myfile)

logging.basicConfig(
    format="[%(asctime)s] [%(levelname)s] [%(pathname)s] [line:%(lineno)d] %(message)s",
    level=logging.INFO,
)


def _get_sign_filename() -> Tuple[Optional[str], Optional[str]]:
    """获取签名文件名。"""
    crlfile = "SWSCRL.crl"
    cmstag = ".p7s"
    return crlfile, cmstag


def _get_sign_crl(signtype, default_crl):
    """获取crl路径"""
    sign_crl = default_crl

    if signtype in ("atlas_cms", "cms_pss"):
        # 计算产品线会往如下路径下归档crl，并用以签名
        sign_crl = os.path.join(mypath, "../cert_path/pss/SWSCRL.crl")
        if not os.path.exists(sign_crl):
            return default_crl

    if signtype == "cms_ch_pss":
        sign_crl = os.path.join(mypath, "../cert_path/whitebox/SWSCRL.crl")

    return sign_crl


def _check_result(inputfile) -> bool:
    """签名后处理"""
    crlfile, cmstag = _get_sign_filename()
    if crlfile is None or cmstag is None:
        logging.error("get cms or crl file name error")
        return False
    for file in inputfile:
        cms = file + cmstag
        if not os.path.isfile(cms):
            logging.error("cms file:%s is not exist", cms)
            return False
    return True


def _help():
    logging.info(
        "==================================== 帮助信息 =================================="
    )
    logging.info("通用命令，命令格式如下:")
    logging.info("python community_sign_build.py [cmd] target ...")
    logging.info(
        "--------------------------------------------------------------------------------"
    )
    logging.info("[cmd] help|cms|")
    logging.info("  %s: 查看帮助", "help".ljust(8))
    logging.info("  %s: 制作cms签名", "cms".ljust(8))
    logging.info(
        "--------------------------------------------------------------------------------"
    )
    logging.info(
        "  %s: 待签名的文件路径,支持多target,各target以空格分开", "target".ljust(8)
    )
    logging.info(
        "====================================== END ====================================="
    )


def get_sign_cmd(file, rootdir) -> list:
    """获取签名命令。"""
    sign_crl = os.path.join(rootdir, "scripts/signtool/signature/SWSCRL.crl")
    cmd = [
        "/home/jenkins/signatrust_client/signatrust_client",
        "--config",
        "/home/jenkins/signatrust_client/client.toml",
        "add",
        "--file-type",
        "p7s",
        "--key-type",
        "x509",
        "--key-name",
        "SignCert",
        "--detached",
        file,
        "--timestamp-key",
        "TimeCert",
        "--crl",
        sign_crl,
    ]
    return cmd


def _run_sign(inputfiles, rootdir):
    """执行签名。"""
    crlfile, cmstag = _get_sign_filename()
    ret = True
    for file in inputfiles:
        if not os.path.isfile(file):
            logging.warning("input file:%s is not exist", file)
            continue
        cmd = get_sign_cmd(file, rootdir)

        logging.info("run sign cmd %s in %s", " ".join(cmd), mypath)
        result = subprocess.run(
            cmd, cwd=mypath, check=False, stdout=PIPE, stderr=STDOUT
        )
        if 0 != result.returncode:
            logging.error(result.stdout.decode())
            logging.error("file %s signed error", file)
            ret = False
            break
    return ret


# 多个文件签名场景需要拆分分别签
def main(argv):
    """主流程。"""
    if (len(argv)) < 3:
        logging.error("argv number is error, it must >= 2, now (%s)", str(argv))
        sys.exit(1)

    rootdir = argv[1]
    inputfiles = argv[2:]
    # 初始化签名环境
    ret = _run_sign(inputfiles, rootdir)

    if ret is not False:
        if not _check_result(inputfiles):
            logging.error("check signature result fail")
            sys.exit(1)
    else:
        logging.error("signature build fail")
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main(sys.argv)
