#!/usr/bin/env python3
"""Build and run the host tests from any working directory."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    os.chdir(ROOT)
    build = ROOT / "build"
    build.mkdir(exist_ok=True)
    compiler = os.environ.get("CXX", "g++" if os.name == "nt" else "c++")
    binary = build / ("tour-tests.exe" if os.name == "nt" else "tour-tests")
    flags = ["-std=c++17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-g", "-O1"]
    if os.name != "nt":
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    src = Path("StopWatchTourGuide/src")
    sources = [src / (unit + ".cpp") for unit in ("Tour", "RecordStore", "App", "CourseEditor", "UsbCommands")]
    subprocess.run([compiler, *flags, "-I", src, "tests/test.cpp", *sources, "-o", binary], check=True)
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    subprocess.run([binary], check=True)
    subprocess.run([sys.executable, "tests/usb-cli.py"], check=True)


if __name__ == "__main__":
    try:
        main()
    except OSError as error:
        sys.exit(str(error))
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
