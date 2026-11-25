#!/usr/bin/env python3
"""Set up, build and upload the StopWatch firmware with Arduino CLI."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
FIRMWARE = BUILD / "firmware"
SKETCH = "StopWatchTourGuide"
INDEX = "https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json"


def logged(command, filename):
    with (BUILD / filename).open("wb") as log, subprocess.Popen(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
    ) as process:
        for line in process.stdout:
            log.write(line)
            sys.stdout.buffer.write(line)
            sys.stdout.buffer.flush()
        if process.wait():
            raise subprocess.CalledProcessError(process.returncode, command)


def setup(cli):
    subprocess.run([cli, "core", "update-index", "--additional-urls", INDEX], check=True)
    subprocess.run([cli, "core", "install", "m5stack:esp32@3.3.7", "--additional-urls", INDEX], check=True)
    subprocess.run([cli, "lib", "install", "--no-deps", "M5GFX@0.3.0", "M5Unified@0.2.28"], check=True)


def build(cli):
    BUILD.mkdir(exist_ok=True)
    inventory = subprocess.check_output([cli, "board", "listall", "--format", "json"])
    (BUILD / "boards.json").write_bytes(inventory)
    boards = json.loads(inventory)
    matches = [b["fqbn"] for b in boards.get("boards", []) if b.get("name") == "M5StopWatch"
               and b["fqbn"].startswith("m5stack:esp32:")]
    if len(matches) != 1:
        for warning in boards.get("warnings", []):
            print(warning, file=sys.stderr)
        sys.exit("M5StopWatch が一意に見つかりません。M5Stack ボードパッケージを確認してください。")
    fqbn = matches[0]
    with (BUILD / "board-versions.txt").open("wb") as versions:
        for args in (["version"], ["core", "list"], ["lib", "list"]):
            subprocess.run([cli, *args], stdout=versions, check=True)
        versions.write(f"FQBN={fqbn}\n".encode())
    logged([cli, "compile", "--fqbn", fqbn, "--warnings", "all", "--build-path",
            BUILD / "arduino", "--output-dir", FIRMWARE, SKETCH], "board-build.log")
    logged([sys.executable, "scripts/verify-firmware.py"], "firmware-check.log")
    print("ビルドが完了しました。")


def upload(cli, port, sync_port):
    options = BUILD / "arduino" / "build.options.json"
    if not (FIRMWARE / f"{SKETCH}.ino.bin").is_file() or not options.is_file():
        sys.exit("先に uv run scripts/board.py build でファームウェアを生成してください。")
    subprocess.run([sys.executable, "scripts/verify-firmware.py"], check=True)
    if not sync_port:
        sync_port = port
        if sys.platform.startswith("linux"):
            for link in Path("/dev/serial/by-path").glob("*"):
                if link.resolve() == Path(port).resolve():
                    sync_port = str(link)
                    break
    logged([cli, "upload", "--fqbn", "m5stack:esp32:m5stack_stopwatch", "--port", port, "--protocol", "serial",
            "--input-dir", FIRMWARE, SKETCH], "board-upload.log")
    print("書き込みが完了しました。PCの現地日時を端末に設定します。", flush=True)
    if subprocess.run([sys.executable, "scripts/sync-clock.py", sync_port]).returncode:
        sys.exit("書き込みは完了しています。通常モードのポートを確認し、uv run scripts/sync-clock.py <PORT> で再同期してください。")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("setup", help="ボードパッケージとライブラリを導入")
    commands.add_parser("build", help="ビルドとFlash画像の検証")
    commands.add_parser("ports", help="Arduino CLIで接続中のポートを表示")
    flash = commands.add_parser("upload", help="書き込みと時刻同期")
    flash.add_argument("port")
    flash.add_argument("--sync-port", help="通常モードのポート名が変わる場合に指定")
    args = parser.parse_args()
    if args.command == "upload" and not args.port.strip():
        parser.error("書き込み先のポート名を指定してください。")
    cli = shutil.which(os.environ.get("ARDUINO_CLI", "arduino-cli"))
    if not cli:
        sys.exit("Arduino CLI が見つかりません。PATH または ARDUINO_CLI を確認してください。")
    cli = str(Path(cli).resolve())
    os.chdir(ROOT)
    if args.command == "setup" or (ROOT / ".arduino/data").is_dir():
        for key in ("data", "downloads", "user"):
            os.environ["ARDUINO_DIRECTORIES_" + key.upper()] = str(ROOT / ".arduino" / key)
    if args.command == "setup":
        setup(cli)
    elif args.command == "build":
        build(cli)
    elif args.command == "ports":
        subprocess.run([cli, "board", "list"], check=True)
    else:
        upload(cli, args.port, args.sync_port)


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode)
    except OSError as error:
        sys.exit(str(error))
