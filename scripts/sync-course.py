#!/usr/bin/env python3
"""待機中のStopWatchに、JSONのコースと終了日時または持ち時間をUSBで設定します。"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re
import sys
import time

from tour_usb import exchange, with_device


ERRORS = {
    b"TOUR COURSE BUSY": "本体を待機画面に戻してください。",
    b"TOUR COURSE INVALID": "本体がコース設定を受け付けませんでした。",
    b"TOUR COURSE FAILED": "本体にコースを保存できませんでした。",
    b"TOUR COURSE DEADLINE": "終了日時を本体の現在日時より後に設定してください。",
    b"TOUR COURSE DURATION": "持ち時間は1〜2147483647秒で指定してください。",
    b"TOUR COURSE CLOCK": "先に本体の現在日時を設定してください。",
}


def fields(value, path, required, optional=()):
    if not isinstance(value, dict):
        raise ValueError(f"{path} にはオブジェクトを指定してください。")
    for key in required:
        if key not in value:
            raise ValueError(f"{path} に {key} がありません。")
    for key in value:
        if key not in required and key not in optional:
            raise ValueError(f"{path} に未対応の項目 {key} があります。")


def name(value, path):
    if (not isinstance(value, str) or not value.strip(" ") or len(value.encode()) > 96
            or any(ord(ch) < 32 or ord(ch) == 127 for ch in value)):
        raise ValueError(f"{path} には制御文字を含まないUTF-8で96バイト以内の名前を指定してください。")
    return value


def seconds(value, path):
    if type(value) is not int or not 1 <= value <= 2147483647:
        raise ValueError(f"{path} は1〜2147483647の整数（秒）で指定してください。")
    return value


def deadline_seconds(value):
    if not isinstance(value, str) or not re.fullmatch(r"20[0-9]{2}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}", value):
        raise ValueError("deadline は2000〜2099年の現地日時を YYYY-MM-DDTHH:MM で指定してください。")
    try:
        local = datetime.fromisoformat(value)
    except ValueError as error:
        raise ValueError(f"deadline の日付または時刻が正しくありません。{value}") from error
    delta = local - datetime(2000, 1, 1)
    return delta.days * 86400 + delta.seconds


def encode_course(course):
    if isinstance(course, dict) and "deadline" in course and "durationSeconds" in course:
        raise ValueError("deadline と durationSeconds はどちらか一方を指定してください。")
    if isinstance(course, dict) and course.keys() == {"durationSeconds"}:
        return b"", "+" + str(seconds(course["durationSeconds"], "durationSeconds"))
    fields(course, "JSON", ("name", "warnings", "segments"), ("deadline", "durationSeconds"))
    timing = "-"
    if "durationSeconds" in course:
        timing = "+" + str(seconds(course["durationSeconds"], "durationSeconds"))
    elif "deadline" in course:
        timing = str(deadline_seconds(course["deadline"]))
    warnings = course["warnings"]
    fields(warnings, "warnings", ("overall", "segment"))
    overall = seconds(warnings["overall"], "warnings.overall")
    segment = seconds(warnings["segment"], "warnings.segment")
    lines = [name(course["name"], "name"), f"{overall}\t{segment}"]
    segments = course["segments"]
    if not isinstance(segments, list) or not 1 <= len(segments) <= 32:
        raise ValueError("segments には区間を1〜32個指定してください。")
    for index, item in enumerate(segments):
        path = f"segments[{index}]"
        fields(item, path, ("seconds", "name"))
        duration = seconds(item["seconds"], f"{path}.seconds")
        label = name(item["name"], f"{path}.name")
        lines.append(f"{duration}\t{label}")
    return "\n".join(lines).encode(), timing


def synchronize(port, body, timing, timeout=40):
    def upload(device, until):
        def request(command, reply, errors=ERRORS):
            return exchange(device, b"TOUR COURSE " + command, reply,
                            min(until, time.monotonic() + 2), errors) == reply

        # A disconnect can leave half a command before BEGIN. Ignore that
        # command's error and retry BEGIN until the receiver is ready.
        busy = b"TOUR COURSE BUSY"
        if not request(b"BEGIN", b"TOUR COURSE READY", {busy: ERRORS[busy]}):
            return None
        # Wait for each line so the device's small USB receive queue cannot overflow.
        for line in body.splitlines():
            if not request(b"LINE " + line, b"TOUR COURSE NEXT"):
                return None
        if request(f"SAVE {timing} {len(body)}".encode(), b"TOUR COURSE OK"):
            return True
        return None

    with_device(port, upload, timeout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="USBシリアルポート（例 COM3、/dev/cu.usbmodem101、/dev/ttyACM0）")
    parser.add_argument("file", type=Path, help="UTF-8のコース設定JSON")
    args = parser.parse_args()
    try:
        course = json.loads(args.file.read_text(encoding="utf-8-sig"))
        body, timing = encode_course(course)
        print(f"{args.port} で待機中の本体に設定を送ります。", flush=True)
        synchronize(args.port, body, timing)
    except (OSError, ValueError) as error:
        sys.exit(f"設定を完了できませんでした。{error}")
    if "name" in course:
        print(f"コース「{course['name']}」を本体に保存しました。")
    if "deadline" in course:
        print(f"今回の終了日時を {course['deadline'].replace('T', ' ')} に設定しました。")
    elif "durationSeconds" in course:
        print(f"持ち時間を {course['durationSeconds']} 秒に設定しました。")
