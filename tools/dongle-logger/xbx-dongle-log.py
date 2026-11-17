#!/usr/bin/env python3
"""
Log the dongle's USB console to monthly files, unattended.

Finds the dongle's console by its USB product name (sysfs), writes every line with the
wall-clock time to <dir>/xbx-dongle-YYYY-MM.log and keeps the newest KEEP
files. The console goes away with the USB device (unplugged, PC suspended,
dongle restarted): the logger notes it, waits and reopens. A suspend shows as
a note with its length. Standard library only.
"""

import argparse
import datetime
import glob
import os
import re
import select
import sys
import termios
import time

PRODUCT = "XBX-NRF Dongle"
KEEP = 6  # monthly files
ANSI = re.compile(rb"\x1b\[[0-9;]*m")


class Log:
    def __init__(self, directory):
        self.dir = directory
        self.month = None
        self.f = None
        os.makedirs(directory, exist_ok=True)

    def write(self, text):
        now = datetime.datetime.now()
        month = now.strftime("%Y-%m")
        if month != self.month:
            self._open(month)
        self.f.write(f"{now.isoformat(sep=' ', timespec='milliseconds')}  {text}\n")
        self.f.flush()

    def _open(self, month):
        if self.f:
            self.f.close()
        self.month = month
        self.f = open(os.path.join(self.dir, f"xbx-dongle-{month}.log"), "a",
                      encoding="utf-8", errors="replace")
        old = sorted(glob.glob(os.path.join(self.dir, "xbx-dongle-*.log")))[:-KEEP]
        for path in old:
            os.remove(path)


def read(path):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return None


def find(device):
    if device:
        return device if os.path.exists(device) else None
    # /sys/class/tty/ttyACMn/device is the USB interface; its parent the device
    for tty in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        usb = os.path.realpath(os.path.join(tty, "device", ".."))
        if read(os.path.join(usb, "product")) == PRODUCT:
            return "/dev/" + os.path.basename(tty)
    return None


def open_port(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0                                  # iflag: raw
    attrs[1] = 0                                  # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attrs[3] = 0                                  # lflag: no echo, no canon
    attrs[4] = attrs[5] = termios.B115200         # ignored by CDC ACM
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    return fd


class SuspendWatch:
    """CLOCK_MONOTONIC stops during suspend, CLOCK_BOOTTIME doesn't."""

    def __init__(self):
        self.mono = time.clock_gettime(time.CLOCK_MONOTONIC)
        self.boot = time.clock_gettime(time.CLOCK_BOOTTIME)

    def check(self):
        mono = time.clock_gettime(time.CLOCK_MONOTONIC)
        boot = time.clock_gettime(time.CLOCK_BOOTTIME)
        slept = (boot - self.boot) - (mono - self.mono)
        self.mono, self.boot = mono, boot
        return slept if slept > 2 else 0


def run(log, device):
    suspend = SuspendWatch()
    fd = None
    path = None
    waiting_logged = False
    partial = b""

    log.write("--- logger started")
    while True:
        slept = suspend.check()
        if slept:
            log.write(f"--- PC was suspended for {slept:.0f} s")

        if fd is None:
            path = find(device)
            if path is None:
                if not waiting_logged:
                    log.write("--- waiting for the dongle console")
                    waiting_logged = True
                time.sleep(1)
                continue
            try:
                fd = open_port(path)
            except OSError as e:
                log.write(f"--- can't open {path}: {e}")
                time.sleep(5)
                continue
            log.write(f"--- console open: {path}")
            waiting_logged = False
            partial = b""

        try:
            ready, _, _ = select.select([fd], [], [], 1.0)
            data = os.read(fd, 4096) if ready else b""
            if ready and not data:
                raise OSError("end of file")
        except OSError as e:
            if partial:
                log.write(partial.decode("utf-8", "replace"))
            log.write(f"--- console gone ({e})")
            os.close(fd)
            fd = None
            continue

        partial += ANSI.sub(b"", data)
        *lines, partial = partial.split(b"\n")
        for line in lines:
            line = line.rstrip(b"\r")
            if line:
                log.write(line.decode("utf-8", "replace"))


def main():
    ap = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    ap.add_argument("--dir", default="/var/log/xbx-dongle", help="log folder")
    ap.add_argument("--device", help="console device (default: found by name)")
    args = ap.parse_args()
    try:
        run(Log(args.dir), args.device)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
