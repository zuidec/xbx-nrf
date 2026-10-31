"""Finding and opening the configurator interface.

Linux: hidraw through sysfs, standard library only. Elsewhere: hidapi (the
`hidapi` package, bundled by build.sh).
"""

import glob
import os
import select
import sys
from dataclasses import dataclass

from . import protocol


@dataclass
class Found:
    path: str  # opens it
    pid: int
    kind: str  # "controller" / "dongle", from the PID
    serial: str


class AccessError(OSError):
    """The device is there but may not be opened (Linux: no udev rule)."""


def _is_config_desc(desc):
    # usage page 0xFF00 (2-byte item) first, as config_usb.c writes it
    return desc[:3] == b"\x06\x00\xff"


def _find_hidraw():
    found = []
    for node in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        try:
            with open(f"{node}/device/uevent") as f:
                uevent = dict(l.split("=", 1) for l in f.read().splitlines() if "=" in l)
            with open(f"{node}/device/report_descriptor", "rb") as f:
                desc = f.read()
        except OSError:
            continue
        _, vid, pid = (int(x, 16) for x in uevent.get("HID_ID", "0:0:0").split(":"))
        if vid != protocol.VID or pid not in protocol.APP_PIDS or not _is_config_desc(desc):
            continue
        found.append(Found("/dev/" + os.path.basename(node), pid, protocol.APP_PIDS[pid],
                           uevent.get("HID_UNIQ", "")))
    return found


class Hidraw:
    def __init__(self, path):
        try:
            self.fd = os.open(path, os.O_RDWR)
        except PermissionError as e:
            raise AccessError(e.errno, f"no access to {path}", path) from e

    def write(self, msg):
        os.write(self.fd, b"\0" + msg)  # report number 0: the interface has none

    def read(self, timeout):
        """One 64-byte report, or None after timeout seconds."""
        r, _, _ = select.select([self.fd], [], [], timeout)
        return os.read(self.fd, protocol.MSG_SIZE) if r else None

    def close(self):
        os.close(self.fd)


def _find_hidapi():
    import hid  # noqa: PLC0415 (only off Linux)

    found = []
    for d in hid.enumerate(protocol.VID):
        if d["product_id"] in protocol.APP_PIDS and d.get("usage_page") == protocol.USAGE_PAGE:
            found.append(Found(d["path"], d["product_id"], protocol.APP_PIDS[d["product_id"]],
                               d.get("serial_number") or ""))
    return found


class Hidapi:
    def __init__(self, path):
        import hid  # noqa: PLC0415

        self.dev = hid.device()
        try:
            self.dev.open_path(path)
        except OSError as e:
            raise AccessError(str(e)) from e

    def write(self, msg):
        self.dev.write(b"\0" + msg)

    def read(self, timeout):
        data = self.dev.read(protocol.MSG_SIZE, int(timeout * 1000))
        return bytes(data) if data else None

    def close(self):
        self.dev.close()


LINUX = sys.platform.startswith("linux")


def find():
    """Configurator interfaces of every connected controller and dongle."""
    return _find_hidraw() if LINUX else _find_hidapi()


def open_path(path):
    return Hidraw(path) if LINUX else Hidapi(path)
