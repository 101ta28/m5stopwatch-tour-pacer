#!/usr/bin/env python3
"""Set the StopWatch RTC to this computer's local wall time over USB."""
import argparse
from datetime import datetime, timedelta
import errno
import os
import select
import sys
import termios
import time
import tty


EPOCH = datetime(2000, 1, 1)
RECONNECT = {errno.ENOENT, errno.ENODEV, errno.ENXIO, errno.EIO, errno.EPIPE, errno.EAGAIN}


def exchange(fd, command, prefix, deadline):
    packet = command + b"\n"
    if os.write(fd, packet) != len(packet):
        raise OSError(errno.EIO, "USBへの送信が中断されました")
    pending = b""
    while time.monotonic() < deadline:
        timeout = max(0, deadline - time.monotonic())
        if not select.select([fd], [], [], timeout)[0]:
            break
        chunk = os.read(fd, 512)
        if not chunk:
            raise OSError(errno.ENODEV, "USBが切断されました")
        lines = (pending + chunk).split(b"\n")
        pending = lines.pop()[-512:]
        for line in lines:
            line = line.rstrip(b"\r")
            if line == b"TOUR TIME BUSY":
                raise ValueError("端末を待機画面に戻してから同期してください。")
            if line in (b"TOUR TIME FAILED", b"TOUR TIME INVALID"):
                raise ValueError("端末で現在日時の設定または読み戻しに失敗しました。")
            if line.startswith(prefix):
                return line
    return None


def synchronize(port, timeout=40):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            try:
                tty.setraw(fd, termios.TCSANOW)
                attrs = termios.tcgetattr(fd)
                attrs[2] |= termios.CLOCAL | termios.CREAD
                attrs[4] = attrs[5] = termios.B115200
                termios.tcsetattr(fd, termios.TCSANOW, attrs)
                termios.tcflush(fd, termios.TCIFLUSH)
                while time.monotonic() < deadline:
                    # Only sample the PC clock after the application answers.
                    # Time spent in the bootloader or waiting for USB is excluded.
                    ready = exchange(fd, b"TOUR TIME?", b"TOUR TIME READY",
                                     min(deadline, time.monotonic() + 1))
                    if ready != b"TOUR TIME READY":
                        continue
                    local = datetime.now().replace(microsecond=0)
                    if not 2000 <= local.year <= 2099:
                        raise ValueError("PCの現地日時を2000〜2099年に設定してください。")
                    seconds = (local - EPOCH) // timedelta(seconds=1)
                    prefix = b"TOUR TIME OK "
                    reply = exchange(fd, f"TOUR TIME {seconds}".encode(), prefix,
                                     min(deadline, time.monotonic() + 1.5))
                    if reply is None:
                        continue
                    value = reply[len(prefix):]
                    if value.isdigit() and int(value) in (seconds, seconds + 1):
                        verified = EPOCH + timedelta(seconds=int(value))
                        if abs((datetime.now() - verified).total_seconds()) <= 2:
                            return verified
            finally:
                os.close(fd)
        except (OSError, termios.error) as error:
            if error.args[0] not in RECONNECT:
                raise
            time.sleep(0.15)
    raise TimeoutError("端末の同期応答を確認できません。通常モードで起動し、同期を再実行してください。")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    port = parser.parse_args().port
    print(f"{port} で時刻同期を待っています。自動で起動しない場合は電源ボタンを短く1回押してください。", flush=True)
    try:
        result = synchronize(port)
    except (OSError, termios.error, ValueError) as error:
        sys.exit(f"時刻同期を完了できませんでした。{error}")
    print(f"時刻同期完了　端末の現在日時 {result:%Y/%m/%d %H:%M:%S}")
