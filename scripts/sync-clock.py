#!/usr/bin/env python3
"""Set the StopWatch RTC to this computer's local wall time over USB."""
import argparse
from datetime import datetime, timedelta
import sys
import termios
import time

from tour_usb import exchange, with_device


EPOCH = datetime(2000, 1, 1)
ERRORS = {
    b"TOUR TIME BUSY": "端末を待機画面に戻してから同期してください。",
    b"TOUR TIME FAILED": "端末で現在日時の設定または読み戻しに失敗しました。",
    b"TOUR TIME INVALID": "端末で現在日時の設定または読み戻しに失敗しました。",
}


def set_clock(fd, deadline):
    # Only sample the PC clock after the application answers.
    # Time spent in the bootloader or waiting for USB is excluded.
    ready = exchange(fd, b"TOUR TIME?", b"TOUR TIME READY",
                     min(deadline, time.monotonic() + 1), ERRORS)
    if ready != b"TOUR TIME READY":
        return None
    local = datetime.now().replace(microsecond=0)
    if not 2000 <= local.year <= 2099:
        raise ValueError("PCの現地日時を2000〜2099年に設定してください。")
    seconds = (local - EPOCH) // timedelta(seconds=1)
    prefix = b"TOUR TIME OK "
    reply = exchange(fd, f"TOUR TIME {seconds}".encode(), prefix,
                     min(deadline, time.monotonic() + 1.5), ERRORS)
    if reply is None:
        return None
    value = reply[len(prefix):]
    if value.isdigit() and int(value) in (seconds, seconds + 1):
        verified = EPOCH + timedelta(seconds=int(value))
        if abs((datetime.now() - verified).total_seconds()) <= 2:
            return verified


def synchronize(port, timeout=40):
    return with_device(port, set_clock, timeout)


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
