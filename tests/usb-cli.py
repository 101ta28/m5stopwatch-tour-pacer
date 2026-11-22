"""Exercise both Linux USB commands against the firmware parser on a pseudo-terminal."""
import json
import fcntl
import os
from pathlib import Path
import pty
import subprocess
import sys
from tempfile import TemporaryDirectory
import tty


def run(script, port, *args, expected, success=True):
    result = subprocess.run([sys.executable, "scripts/" + script, port, *args],
                            capture_output=True, text=True, timeout=8)
    assert (result.returncode == 0) == success, (result.stdout, result.stderr)
    assert expected in (result.stdout if success else result.stderr), (result.stdout, result.stderr)


def main():
    master, slave = pty.openpty()
    tty.setraw(slave)
    port = os.ttyname(slave)
    with subprocess.Popen(["build/tour-tests", "--usb"], stdin=master, stdout=master) as device:
        os.close(master)
        try:
            with TemporaryDirectory() as tmp:
                path = Path(tmp, "course.json")
                course = json.loads(Path("examples/course.json").read_text(encoding="utf-8"))

                def upload(expected, success=True):
                    path.write_text(json.dumps(course, ensure_ascii=False), encoding="utf-8")
                    run("sync-course.py", port, path, expected=expected, success=success)

                course["deadline"] = "2028-02-29T00:15"
                # Resume after a host disconnected in the middle of a command.
                os.write(slave, b"TOUR COURSE SA")
                upload("2028-02-29 00:15")
                fcntl.flock(slave, fcntl.LOCK_EX)
                try:
                    upload("USBポートを使用しています", False)
                finally:
                    fcntl.flock(slave, fcntl.LOCK_UN)
                course["deadline"] = "2027-02-29T00:15"
                upload("deadline", False)
                course["deadline"] = "2000-01-01T00:00"
                upload("本体の現在日時より後", False)
                del course["deadline"]
                course["name"] = "あ" * 32
                course["segments"] = [{"name": course["name"], "seconds": 2147483647}] * 32
                upload("本体に保存しました")
                course["segments"].append(course["segments"][0])
                upload("segments", False)
                course["segments"] = [{"name": "注\n意", "seconds": 1}]
                upload("segments[0].name", False)
                course["segments"] = [{"name": "見学", "seconds": True}]
                upload("segments[0].seconds", False)
            run("sync-clock.py", port, expected="時刻同期完了")
        finally:
            os.close(slave)
            device.wait(timeout=5)
        assert device.returncode == 0, device.returncode
    print("PASS 仮想USBでJSONの送信・入力エラー・時刻同期")


if __name__ == "__main__":
    main()
