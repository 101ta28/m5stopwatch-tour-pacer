#!/usr/bin/env python3
"""Capture a Linux serial port for 30 seconds, including USB reconnections."""
import argparse
import errno
import os
import select
import sys
import termios
import time
from pathlib import Path


def capture(port, log, seconds):
    def emit(data):
        log.write(data)
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()

    deadline = time.monotonic() + seconds
    emit(f"{port} を待っています。記録時間は {seconds} 秒です。\n".encode())
    while time.monotonic() < deadline:
        try:
            fd = os.open(port, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
            with os.fdopen(fd, "rb", buffering=0) as serial:
                attrs = termios.tcgetattr(fd)
                attrs[0] = attrs[1] = attrs[3] = 0
                attrs[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
                attrs[4] = attrs[5] = termios.B115200
                attrs[6][termios.VMIN] = 1
                attrs[6][termios.VTIME] = 0
                termios.tcsetattr(fd, termios.TCSANOW, attrs)
                emit(f"\n{port} に接続しました。\n".encode())
                while time.monotonic() < deadline:
                    timeout = max(0, deadline - time.monotonic())
                    if not select.select([fd], [], [], timeout)[0]:
                        return
                    chunk = serial.read(4096)
                    if chunk is None:
                        continue
                    if not chunk:
                        emit(b"\n[disconnected]\n")
                        break
                    emit(chunk)
        except (OSError, termios.error) as error:
            if error.args[0] not in (errno.ENOENT, errno.ENODEV, errno.ENXIO, errno.EIO):
                raise
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
    except (OSError, termios.error) as error:
        sys.exit(str(error))
