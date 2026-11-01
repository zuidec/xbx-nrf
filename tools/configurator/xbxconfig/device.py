"""A connected controller or dongle: requests, responses and the stream.

A reader thread takes every input report: responses go to the waiting
request, STREAM reports to the latest-stream slot and the stream callback.
One request at a time (the firmware drops a second one).
"""

import queue
import threading

from . import protocol as p
from . import transport

TIMEOUT = 1.0  # s per request


class Device:
    def __init__(self, found):
        self.found = found
        self.io = transport.open_path(found.path)
        self._seq = 0
        self._lock = threading.Lock()  # one request at a time
        self._responses = queue.Queue()
        self._stop = threading.Event()
        self.latest_stream = None
        self.on_stream = None  # called from the reader thread
        self.error = None  # set when the reader stopped (unplugged)
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        try:
            self.info = self.get_info()
        except Exception:
            self.close()
            raise
        if self.info.channel[0] != p.CHANNEL_MAJOR:
            self.close()
            raise p.ProtocolError(
                f"channel version {self.info.channel[0]}.{self.info.channel[1]}: "
                f"this app speaks {p.CHANNEL_MAJOR}.x")

    @property
    def alive(self):
        return self.error is None and not self._stop.is_set()

    def close(self):
        self._stop.set()
        self._reader.join(timeout=1)
        try:
            self.io.close()
        except OSError:
            pass

    def _read_loop(self):
        while not self._stop.is_set():
            try:
                msg = self.io.read(0.1)
            except OSError as e:
                self.error = e
                self._responses.put(None)
                return
            if not msg:
                continue
            if msg[0] == p.STREAM_REPORT:
                s = p.Stream.parse(msg)
                self.latest_stream = s
                if self.on_stream:
                    self.on_stream(s)
            else:
                self._responses.put(msg)

    def request(self, cmd, args=b""):
        """Send cmd, wait for its response; the result bytes (61)."""
        with self._lock:
            if not self.alive:
                raise p.ProtocolError(f"device gone: {self.error}")
            while not self._responses.empty():
                self._responses.get_nowait()  # late responses of a timed-out request
            self._seq = (self._seq + 1) & 0xFF
            self.io.write(p.request(cmd, self._seq, args))
            while True:
                try:
                    msg = self._responses.get(timeout=TIMEOUT)
                except queue.Empty:
                    raise p.ProtocolError(f"no response to command 0x{cmd:02x}") from None
                if msg is None:
                    raise p.ProtocolError(f"device gone: {self.error}")
                if msg[0] == cmd and msg[1] == self._seq:
                    break
            if msg[2]:
                raise p.StatusError(cmd, msg[2])
            return msg[3:]

    # commands

    def get_info(self):
        return p.Info.parse(self.request(p.INFO))

    def setting_ids(self):
        r = self.request(p.SETTING_GET, b"\0")
        return list(r[1:1 + r[0]])

    def setting_get(self, sid):
        return self.request(p.SETTING_GET, bytes([sid]))[1:]

    def setting_set(self, sid, value):
        self.request(p.SETTING_SET, bytes([sid]) + bytes(value))

    def paddles(self):
        """(enabled, [XBX_BTN_* per paddle])"""
        return bool(self.setting_get(p.SET_PADDLES)[0]), p.unpack_bindings(
            self.setting_get(p.SET_BINDINGS))

    def set_paddles_enabled(self, on):
        self.setting_set(p.SET_PADDLES, bytes([1 if on else 0]))

    def set_bindings(self, values):
        self.setting_set(p.SET_BINDINGS, p.pack_bindings(values))

    def led(self):
        """Controller: Guide LED level 1-255."""
        return self.setting_get(p.SET_LED)[0]

    def set_led(self, level):
        self.setting_set(p.SET_LED, bytes([level]))

    def dongle_leds(self):
        """Dongle: {"pair", "player", "power": level 1-255, "player_on", "power_on"}."""
        v = self.setting_get(p.SET_DONGLE_LEDS)
        return {"pair": v[0], "player": v[1], "power": v[2],
                "player_on": bool(v[3] & p.LEDS_PLAYER_ON),
                "power_on": bool(v[3] & p.LEDS_POWER_ON)}

    def set_dongle_leds(self, leds):
        on = ((p.LEDS_PLAYER_ON if leds["player_on"] else 0)
              | (p.LEDS_POWER_ON if leds["power_on"] else 0))
        self.setting_set(p.SET_DONGLE_LEDS,
                         bytes([leds["pair"], leds["player"], leds["power"], on]))

    def trigger_full(self):
        """(LT, RT) full pull thresholds, %."""
        v = self.setting_get(p.SET_TRG_FULL)
        return v[0], v[1]

    def set_trigger_full(self, lt, rt):
        self.setting_set(p.SET_TRG_FULL, bytes([lt, rt]))

    def trigger_fx(self):
        """{"click_on", "click" strength 1-255, "click_ms", "rumble_on",
        "from_heavy", "from_weak" %, "threshold" % of the trigger output}."""
        v = self.setting_get(p.SET_TRG_FX)
        return {"click_on": bool(v[0] & p.TRG_FX_CLICK), "click": v[1],
                "rumble_on": bool(v[0] & p.TRG_FX_RUMBLE), "from_heavy": v[2],
                "from_weak": v[3], "threshold": v[4], "click_ms": v[5]}

    def set_trigger_fx(self, fx):
        flags = ((p.TRG_FX_CLICK if fx["click_on"] else 0)
                 | (p.TRG_FX_RUMBLE if fx["rumble_on"] else 0))
        self.setting_set(p.SET_TRG_FX, bytes([flags, fx["click"], fx["from_heavy"],
                                              fx["from_weak"], fx["threshold"],
                                              fx["click_ms"]]))

    def stream(self, hz):
        self.request(p.STREAM, bytes([hz]))

    def calibrate(self, step=p.CAL_STATUS, targets=0):
        return p.CalStatus.parse(self.request(p.CALIBRATE, bytes([step, targets])))

    def rumble(self, levels, ms):
        self.request(p.RUMBLE, p.pack_rumble(levels, ms))

    def factory_reset(self, scope):
        self.request(p.FACTORY_RESET, bytes([scope]))

    def bootloader(self):
        self.request(p.BOOTLOADER)

    def reboot(self):
        self.request(p.REBOOT)


def find():
    return transport.find()


def open_first(kind=None):
    """The first device found (of kind "controller" / "dongle", if given)."""
    for f in find():
        if kind is None or f.kind == kind:
            return Device(f)
    raise p.ProtocolError(f"no {kind or 'device'} found on USB")
