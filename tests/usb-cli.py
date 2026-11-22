"""Exercise the USB commands against the compiled firmware parser without a board."""
from contextlib import contextmanager
from importlib.util import find_spec
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
from tempfile import TemporaryDirectory


@contextmanager
def virtual_device():
    if os.name == "nt":
        # The child CLI gets its COM driver from the pipe-backed fixture below.
        yield "COM12", None
        return

    import pty
    import tty
    master, slave = pty.openpty()
    tty.setraw(slave)
    with subprocess.Popen(["build/tour-tests", "--usb"], stdin=master, stdout=master) as device:
        os.close(master)
        try:
            yield os.ttyname(slave), slave
        finally:
            os.close(slave)
            device.wait(timeout=5)
        assert device.returncode == 0, device.returncode


def run(script, port, *args, expected, success=True, com_fixture=False):
    command = [sys.executable, "-X", "utf8"]
    if os.name == "nt" or com_fixture:
        command += [__file__, "--com-fixture"]
    command += ["scripts/" + script, port, *args]
    result = subprocess.run(command, capture_output=True, encoding="utf-8", timeout=8)
    assert (result.returncode == 0) == success, (result.stdout, result.stderr)
    assert expected in (result.stdout if success else result.stderr), (result.stdout, result.stderr)


def run_with_com_fixture():
    # Exercise Windows COM setup, reads and writes with the C++ firmware behind
    # a stand-in for pySerial's OS driver. The parent bounds a missing response.
    from unittest.mock import patch
    import serial
    sys.path.insert(0, str(Path("scripts").resolve()))
    import tour_usb

    class FirmwarePort:
        def __init__(self, port, baudrate, write_timeout):
            assert port is None and baudrate == 115200 and write_timeout > 0

        def __enter__(self):
            assert self.port == "COM12" and not self.rts and not self.dtr
            self.process = subprocess.Popen(["build/tour-tests" + (".exe" if os.name == "nt" else ""), "--usb"],
                                            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
            self.pending = bytearray()
            return self

        def __exit__(self, *exc):
            self.process.stdin.close()
            self.process.stdout.close()
            assert self.process.wait(timeout=5) == 0

        def write(self, packet):
            self.process.stdin.write(packet)
            self.process.stdin.flush()
            return len(packet)

        @property
        def in_waiting(self):
            return len(self.pending)

        def read(self, size):
            if size and not self.pending:
                self.pending.extend(self.process.stdout.readline())
            result = self.pending[:size]
            del self.pending[:size]
            return bytes(result)

    sys.argv = sys.argv[2:]
    with patch.object(serial, "Serial", FirmwarePort), patch.object(tour_usb, "open_port", tour_usb.open_com_port):
        runpy.run_path(sys.argv[0], run_name="__main__")


def main():
    with virtual_device() as (port, slave), TemporaryDirectory() as tmp:
        path = Path(tmp, "course.json")
        course = json.loads(Path("examples/course.json").read_text(encoding="utf-8"))

        def upload(expected, success=True):
            path.write_text(json.dumps(course, ensure_ascii=False), encoding="utf-8")
            run("sync-course.py", port, path, expected=expected, success=success)

        course["deadline"] = "2028-02-29T00:15"
        if slave is not None:
            import fcntl
            import termios
            os.write(slave, b"TOUR COURSE SA")
        upload("2028-02-29 00:15")
        path.write_bytes(json.dumps(course, ensure_ascii=False, indent=2).replace("\n", "\r\n").encode("utf-8-sig"))
        run("sync-course.py", port, path, expected="2028-02-29 00:15")
        if slave is not None:
            if find_spec("serial"):
                run("sync-course.py", "COM12", path, expected="2028-02-29 00:15", com_fixture=True)
            if sys.platform == "darwin":
                fcntl.ioctl(slave, termios.TIOCEXCL)
            else:
                fcntl.flock(slave, fcntl.LOCK_EX)
            try:
                upload("USBポートを使用しています", False)
            finally:
                if sys.platform == "darwin":
                    fcntl.ioctl(slave, termios.TIOCNXCL)
                else:
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
    print("PASS 仮想USBでJSONの送信・入力エラー・時刻同期")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--com-fixture"]:
        run_with_com_fixture()
    else:
        main()
