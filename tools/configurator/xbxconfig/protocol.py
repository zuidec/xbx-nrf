"""Configurator channel messages: docs/configurator.md, "USB channel"."""

import struct
from dataclasses import dataclass

VID = 0x1209
APP_PIDS = {0x0001: "dongle", 0x0002: "dongle", 0x0003: "controller", 0x0004: "controller"}
USAGE_PAGE = 0xFF00
MSG_SIZE = 64

CHANNEL_MAJOR = 1  # the app refuses another major version

# commands
INFO = 0x01
SETTING_GET = 0x02
SETTING_SET = 0x03
STREAM = 0x04
CALIBRATE = 0x05
RUMBLE = 0x06
FACTORY_RESET = 0x07
BOOTLOADER = 0x08
REBOOT = 0x09
STREAM_REPORT = 0x80

STATUS = {0: "ok", 1: "unknown command", 2: "bad argument", 3: "busy", 4: "unsupported here"}

DEVICES = {1: "controller", 2: "dongle"}
USB_MODES = {0: "HID", 1: "XInput"}

# settings
SET_PADDLES = 0x01
SET_BINDINGS = 0x02
SET_LED = 0x03  # controller: Guide LED, 1-255
SET_TRG_FULL = 0x05  # LT, RT full pull threshold, %
SET_DONGLE_LEDS = 0x06  # dongle: pair, player, power levels (1-255), on flags
LEDS_PLAYER_ON = 0x01
LEDS_POWER_ON = 0x02
TRG_FULL_MIN = 50

# FACTORY_RESET scope
RESET_SETTINGS = 0x01
RESET_CALIBRATION = 0x02
RESET_PAIRING = 0x04

# CALIBRATE
CAL_STATUS, CAL_START, CAL_SAVE, CAL_CANCEL, CAL_ERASE = range(5)
CAL_STICKS = 0x01
CAL_TRIGGERS = 0x02
CAL_STATES = {0: "idle", 1: "rest", 2: "sweep", 3: "saved"}
AXES = ("lx", "ly", "rx", "ry", "lt", "rt")

# STREAM flags
FLAG_WIRED = 0x01
FLAG_LINKED = 0x02
FLAG_CAL_REFUSED = 0x04

# buttons (XBX_BTN_*, firmware/common/include/protocol.h)
BUTTONS = {
    "D-pad up": 1 << 0,
    "D-pad down": 1 << 1,
    "D-pad left": 1 << 2,
    "D-pad right": 1 << 3,
    "Menu": 1 << 4,
    "View": 1 << 5,
    "LS": 1 << 6,
    "RS": 1 << 7,
    "LB": 1 << 8,
    "RB": 1 << 9,
    "Guide": 1 << 10,
    "A": 1 << 12,
    "B": 1 << 13,
    "X": 1 << 14,
    "Y": 1 << 15,
}
BUTTONS_EXT = {"Share": 1 << 0, "Pair": 1 << 1, "P1": 1 << 2, "P2": 1 << 3, "P3": 1 << 4,
               "P4": 1 << 5}
PADDLES = ("P1", "P2", "P3", "P4")  # left upper, left lower, right upper, right lower

TRIGGER_MAX = 1023
STICK_MAX = 32767


class ProtocolError(Exception):
    pass


class StatusError(ProtocolError):
    def __init__(self, cmd, status):
        self.cmd = cmd
        self.status = status
        super().__init__(f"command 0x{cmd:02x}: {STATUS.get(status, status)}")


def request(cmd, seq, args=b""):
    """A request message (64 bytes)."""
    if len(args) > MSG_SIZE - 2:
        raise ValueError("arguments too long")
    return bytes([cmd, seq]) + bytes(args).ljust(MSG_SIZE - 2, b"\0")


@dataclass
class Info:
    channel: tuple
    device: str
    firmware: tuple
    usb_mode: str
    serial: str
    board: str

    @property
    def firmware_str(self):
        return ".".join(map(str, self.firmware))

    @classmethod
    def parse(cls, r):
        return cls(
            channel=(r[0], r[1]),
            device=DEVICES.get(r[2], f"unknown ({r[2]})"),
            firmware=(r[3], r[4], r[5]),
            usb_mode=USB_MODES.get(r[6], str(r[6])),
            serial=r[7:15].hex().upper(),
            board=r[15:].split(b"\0")[0].decode(errors="replace"),
        )


@dataclass
class Stream:
    seq: int
    buttons: int
    buttons_ext: int
    lx: int
    ly: int
    rx: int
    ry: int
    lt: int
    rt: int
    adc: tuple  # lx, ly, rx, ry, lt, rt raw counts
    cal_state: int
    flags: int
    battery_mv: int
    battery_pct: int
    motors: tuple  # heavy, weak, lt, rt

    @classmethod
    def parse(cls, msg):
        d = msg[3:]
        btn, ext, lx, ly, rx, ry, lt, rt = struct.unpack_from("<HBhhhhHH", d, 0)
        adc = struct.unpack_from("<6h", d, 15)
        cal, flags, mv, pct = struct.unpack_from("<BBHB", d, 27)
        return cls(msg[1], btn, ext, lx, ly, rx, ry, lt, rt, adc, cal, flags, mv, pct,
                   tuple(d[32:36]))


@dataclass
class AxisRange:
    min: int
    center: int
    max: int


@dataclass
class CalStatus:
    state: int
    targets: int
    refused: int  # bit per AXES
    stored: int
    full_pct: tuple  # LT, RT
    ranges: list  # AxisRange per AXES
    inverted: int  # stick axes reported against their counts (bit per AXES)

    @classmethod
    def parse(cls, r):
        v = struct.unpack_from("<18h", r, 5)
        ranges = [AxisRange(*v[3 * i:3 * i + 3]) for i in range(len(AXES))]
        return cls(r[0], r[1], r[2], r[3], (r[4], r[41]), ranges, r[42] if len(r) > 42 else 0)


def pack_bindings(values):
    return struct.pack("<4H", *values)


def unpack_bindings(value):
    return list(struct.unpack_from("<4H", value, 0))


def pack_rumble(levels, ms):
    return bytes(levels) + struct.pack("<H", ms)
