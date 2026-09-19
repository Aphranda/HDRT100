"""Build the portable GUI using the active Windows Python environment."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]

README = """DHRT100 序列触发调试 GUI

启动：双击 DHRT100_Sequence_Debug.exe。
该目录必须整体分发，不能只复制 exe；_internal 目录包含运行时依赖。
程序不要求目标机安装 Python。USBTMC 通讯仍需要目标机已安装对应的 Windows
USB 设备驱动；Serial 模式需要设备对应的串口驱动。

本地配置：%LOCALAPPDATA%\\DHRT100\\sequence_trigger_debug_ui.json。
启动恢复参数草稿，关闭前自动保存；恢复不会下发设备配置或自动启动。
转台角度扫描填写开始、终止、步长、运行速度，输入标定设置每度脉冲数。

DHRT100_Tool.exe 是 GUI 使用的内部命令行助手，包含 USBTMC/Serial OTA 工具，
不要单独移动它。当前包只完成了无硬件的软件启动、自检和命令行帮助验证，
未在本机执行 OTA 或单板验收。

图标文件：5711_-_Sync_Event.png
"""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.out_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    command = [sys.executable, "-m", "PyInstaller", "--noconfirm",
               "--distpath", str(output / "dist"), "--workpath", str(output / "build"),
               str(Path(__file__).with_name("sequence_debug.spec"))]
    with (output / "build.log").open("w", encoding="utf-8") as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    print("build_log=" + str(output / "build.log"))
    if result.returncode:
        return result.returncode
    bundle = output / "dist" / "DHRT100_Sequence_Debug"
    helper = bundle / "DHRT100_Tool.exe"
    (bundle / "README.txt").write_text(README, encoding="utf-8")
    result = subprocess.run([str(helper), "--self-test", str(output / "self-test.json")],
                            cwd=output, timeout=60)
    print("bundle=" + str(bundle))
    if result.returncode:
        return result.returncode
    result = subprocess.run([str(bundle / "DHRT100_Sequence_Debug.exe"),
                             "--self-test", str(output / "gui-self-test.json")],
                            cwd=output, timeout=60)
    if result.returncode or not (output / "gui-self-test.json").is_file():
        return result.returncode or 1
    archive = Path(shutil.make_archive(str(output / "DHRT100_Sequence_Debug"),
                                       "zip", root_dir=bundle.parent,
                                       base_dir=bundle.name))
    print("archive=" + str(archive))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
