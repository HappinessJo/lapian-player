"""Python 3.14 在本机的兼容补丁（仅在构建期使用，不进入产品）。

本机 Python 3.14 用 ``os.mkdir(path, 0o700)`` 创建的目录，连当前用户自己都
无法进入（临时目录的 ACL 没有给任何人写权限），于是 ``tempfile.mkdtemp()``
建出来的目录不可写，pip 解包、aqt 下载全部报 PermissionError。

这里把 ``tempfile.mkdtemp`` 换成语义等价、以 0o777 建目录的实现；只影响
PYTHONPATH 里带上本目录的进程，不影响系统 Python 的其他使用。
"""

import os
import tempfile
from tempfile import _get_candidate_names

_ORIGINAL_MKDTEMP = tempfile.mkdtemp


def _mkdtemp_with_writable_mode(suffix=None, prefix=None, dir=None):
    prefix = prefix or tempfile.gettempprefix()
    suffix = suffix if suffix is not None else ''
    target_dir = dir or tempfile.gettempdir()
    names = _get_candidate_names()
    for _ in range(tempfile.TMP_MAX):
        try:
            name = next(names)
        except StopIteration:
            break
        path = os.path.join(target_dir, prefix + name + suffix)
        try:
            os.mkdir(path, 0o777)
        except FileExistsError:
            continue
        return os.path.abspath(path)
    raise FileExistsError("没有可用的临时目录名")


tempfile.mkdtemp = _mkdtemp_with_writable_mode
