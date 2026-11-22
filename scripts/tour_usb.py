"""Linux USB serial transport for StopWatch commands."""
import errno
import fcntl
import os
import select
import termios
import time
import tty


RECONNECT = {errno.ENOENT, errno.ENODEV, errno.ENXIO, errno.EIO, errno.EPIPE, errno.EAGAIN}


def exchange(fd, command, prefix, deadline, errors):
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
            if line in errors:
                raise ValueError(errors[line])
            if line.startswith(prefix):
                return line
    return None


def with_device(port, operation, timeout=40):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            try:
                try:
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError as error:
                    raise ValueError("別の同期処理がUSBポートを使用しています。") from error
                tty.setraw(fd, termios.TCSANOW)
                attrs = termios.tcgetattr(fd)
                attrs[2] |= termios.CLOCAL | termios.CREAD
                attrs[4] = attrs[5] = termios.B115200
                termios.tcsetattr(fd, termios.TCSANOW, attrs)
                termios.tcflush(fd, termios.TCIFLUSH)
                while time.monotonic() < deadline:
                    result = operation(fd, deadline)
                    if result is not None:
                        return result
            finally:
                os.close(fd)
        except (OSError, termios.error) as error:
            if error.args[0] not in RECONNECT:
                raise
            time.sleep(0.15)
    raise TimeoutError("端末の応答を確認できません。USB接続とファームウェアを確認し、通常モードで再実行してください。")
