"""StopWatch USB serial transport for Unix device files and Windows COM ports."""
from contextlib import contextmanager
import errno
import os
import sys
import time
from typing import Callable, NamedTuple


class Connection(NamedTuple):
    read: Callable[[float], bytes]  # Empty on timeout, raises on disconnect.
    write: Callable[[bytes], int]


RECONNECT = {errno.ENOENT, errno.ENODEV, errno.ENXIO, errno.EIO, errno.EPIPE, errno.EAGAIN}


@contextmanager
def open_com_port(port, *, monitor=False):
    try:
        import serial
    except ImportError:
        raise SystemExit("Windows のUSB接続には pySerial が必要です。py -3 -m pip install pyserial を実行してください。")
    try:
        # Set the modem lines before opening so the application stays in normal mode.
        device = serial.Serial(port=None, baudrate=115200, write_timeout=1)
        device.port = port
        device.dtr = False
        device.rts = False
        with device:
            def read(timeout):
                device.timeout = timeout
                first = device.read(1)
                if first:
                    return first + device.read(min(device.in_waiting, 4095))
                return b""

            yield Connection(read, device.write)
    except serial.SerialException as error:
        # Windows can temporarily remove the COM port when the board reboots.
        raise OSError(errno.EIO, str(error)) from error


if os.name == "nt":
    open_port = open_com_port
else:
    import fcntl
    import select
    import termios
    import tty

    @contextmanager
    def open_port(port, *, monitor=False):
        flags = os.O_NOCTTY | os.O_NONBLOCK | (os.O_RDONLY if monitor else os.O_RDWR)
        try:
            fd = os.open(port, flags)
        except OSError as error:
            if error.errno == errno.EBUSY:
                raise ValueError("別の処理がUSBポートを使用しています。") from error
            raise
        exclusive_tty = False
        try:
            if not monitor and sys.platform == "darwin":
                fcntl.ioctl(fd, termios.TIOCEXCL)
                exclusive_tty = True
            elif not monitor:
                try:
                    fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError as error:
                    raise ValueError("別の同期処理がUSBポートを使用しています。") from error
            tty.setraw(fd, termios.TCSANOW)
            attrs = termios.tcgetattr(fd)
            attrs[2] |= termios.CLOCAL | termios.CREAD
            attrs[4] = attrs[5] = termios.B115200
            termios.tcsetattr(fd, termios.TCSANOW, attrs)
            if not monitor:
                termios.tcflush(fd, termios.TCIFLUSH)

            def read(timeout):
                if not select.select([fd], [], [], timeout)[0]:
                    return b""
                data = os.read(fd, 4096)
                if not data:
                    raise OSError(errno.ENODEV, "USBが切断されました")
                return data

            yield Connection(read, lambda packet: os.write(fd, packet))
        except termios.error as error:
            raise OSError(*error.args) from error
        finally:
            try:
                if exclusive_tty:
                    fcntl.ioctl(fd, termios.TIOCNXCL)
            finally:
                os.close(fd)


def exchange(device, command, prefix, deadline, errors):
    packet = command + b"\n"
    if device.write(packet) != len(packet):
        raise OSError(errno.EIO, "USBへの送信が中断されました")
    pending = b""
    while time.monotonic() < deadline:
        chunk = device.read(max(0, deadline - time.monotonic()))
        if not chunk:
            break
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
    last_error = None
    while time.monotonic() < deadline:
        try:
            with open_port(port) as device:
                last_error = None
                while time.monotonic() < deadline:
                    result = operation(device, deadline)
                    if result is not None:
                        return result
        except OSError as error:
            if error.errno not in RECONNECT:
                raise
            last_error = error
            time.sleep(0.15)
    detail = f" ({last_error})" if last_error else ""
    raise TimeoutError("端末の応答を確認できません。USB接続と通常モードのポート名を確認してください。" + detail)
