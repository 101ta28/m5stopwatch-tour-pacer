#!/usr/bin/env python3
"""Capture a StopWatch USB serial port for 30 seconds, including USB reconnections."""
import argparse
import sys
import time
from pathlib import Path

from tour_usb import RECONNECT, open_port


def capture(port, log, seconds):
    def emit(data):
        log.write(data)
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()

    deadline = time.monotonic() + seconds
    emit(f"{port} を待っています。記録時間は {seconds} 秒です。\n".encode())
    while time.monotonic() < deadline:
        connected = False
        try:
            with open_port(port, monitor=True) as device:
                connected = True
                emit(f"\n{port} に接続しました。\n".encode())
                while time.monotonic() < deadline:
                    chunk = device.read(max(0, deadline - time.monotonic()))
                    if chunk:
                        emit(chunk)
        except OSError as error:
            if error.errno not in RECONNECT:
                raise
            if connected:
                emit(b"\n[disconnected]\n")
        time.sleep(0.1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    args = parser.parse_args()
    path = Path(__file__).resolve().parent.parent / "build" / "board-monitor.log"
    path.parent.mkdir(exist_ok=True)
    print(f"起動ログを {path} に保存します。", flush=True)
    try:
        with path.open("wb", buffering=0) as log:
            capture(args.port, log, 30)
    except KeyboardInterrupt:
        pass
    except (OSError, ValueError) as error:
        sys.exit(str(error))
