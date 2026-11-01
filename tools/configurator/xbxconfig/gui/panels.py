"""Panels of the device tabs. Each gets the connected device (or None) via
attach(), on_show() / on_hide() when its page is selected, and tick() about
30 times a second while shown."""

import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from .. import protocol as p
from .. import uf2
from . import widgets as w

STREAM_HZ = 100


def _error(parent, e):
    messagebox.showerror("xbx-config", str(e), parent=parent)


class Panel(ttk.Frame):
    title = ""
    streams = False  # wants STREAM reports while shown

    def __init__(self, master):
        super().__init__(master, padding=12)
        self.device = None

    def attach(self, device):
        self.device = device

    def on_show(self):
        pass

    def on_hide(self):
        pass

    def tick(self):
        pass

    def call(self, method, *args):
        """A Device method; errors in a dialog. Returns its result or None."""
        if not self.device:
            return None
        try:
            return getattr(self.device, method)(*args)
        except (p.ProtocolError, OSError) as e:
            _error(self, e)
            return None


class InfoPanel(Panel):
    title = "Info"
    FIELDS = (("Device", "device"), ("Firmware", "firmware_str"), ("USB mode", "usb_mode"),
              ("Board", "board"), ("Serial", "serial"))

    def __init__(self, master):
        super().__init__(master)
        self.values = {}
        for r, (label, attr) in enumerate(self.FIELDS):
            ttk.Label(self, text=label + ":").grid(row=r, column=0, sticky="w", padx=(0, 12))
            v = ttk.Label(self, text="-")
            v.grid(row=r, column=1, sticky="w")
            self.values[attr] = v
        ttk.Button(self, text="Restart device", command=lambda: self.call("reboot")).grid(
            row=len(self.FIELDS), column=0, pady=(16, 0), sticky="w")

    def attach(self, device):
        super().attach(device)
        for attr, v in self.values.items():
            v.configure(text=getattr(device.info, attr) if device else "-")


class InputPanel(Panel):
    title = "Input test"
    streams = True

    def __init__(self, master):
        super().__init__(master)
        top = ttk.Frame(self)
        top.pack(anchor="w")
        self.ls = w.StickView(top, "Left stick")
        self.ls.grid(row=0, column=0, padx=8)
        self.rs = w.StickView(top, "Right stick")
        self.rs.grid(row=0, column=1, padx=8)
        trig = ttk.Frame(top)
        trig.grid(row=0, column=2, padx=8, sticky="n")
        self.lt = w.TriggerBar(trig, "LT")
        self.lt.pack(pady=4)
        self.rt = w.TriggerBar(trig, "RT")
        self.rt.pack(pady=4)
        self.status = ttk.Label(trig, text="")
        self.status.pack(pady=8, anchor="w")
        self.buttons = w.ButtonGrid(self)
        self.buttons.pack(anchor="w", pady=12)
        self.raw = ttk.Label(self, text="", font="TkFixedFont")
        self.raw.pack(anchor="w")

    def tick(self):
        s = self.device and self.device.latest_stream
        if not s:
            return
        self.ls.show(s.lx, s.ly, s.buttons & p.BUTTONS["LS"])
        self.rs.show(s.rx, s.ry, s.buttons & p.BUTTONS["RS"])
        self.lt.show(s.lt)
        self.rt.show(s.rt)
        self.buttons.show(s.buttons, s.buttons_ext)
        battery = f"{s.battery_mv} mV" if s.battery_mv else "battery unknown"
        if s.battery_pct != 0xFF:
            battery += f", {s.battery_pct} %"
        link = "wired" if s.flags & p.FLAG_WIRED else (
            "linked to the dongle" if s.flags & p.FLAG_LINKED else "no link")
        self.status.configure(text=f"{battery}\n{link}")
        self.raw.configure(text="raw ADC  " + "  ".join(
            f"{n} {v:4}" for n, v in zip(p.AXES, s.adc)))


class CalInfo(ttk.Frame):
    """The values beside a calibration view: whether they are stored, then
    rows of numbers (raw ADC counts)."""

    def __init__(self, master, name, header, rows):
        super().__init__(master)
        self.name = name
        self.state = ttk.Label(self, text="")
        self.state.grid(row=0, column=0, columnspan=4, sticky="w", pady=(0, 4))
        for c, h in enumerate(header, 1):
            ttk.Label(self, text=h, foreground=w.RAW).grid(row=1, column=c, sticky="e",
                                                         padx=(8, 0))
        self.cells = []
        align = "e" if len(header) > 1 else "w"  # number columns, or one text value
        for r, label in enumerate(rows, 2):
            ttk.Label(self, text=label, foreground=w.RAW).grid(row=r, column=0, sticky="w")
            cells = [ttk.Label(self, text="", font="TkFixedFont") for _ in header]
            for c, cell in enumerate(cells, 1):
                cell.grid(row=r, column=c, sticky=align, padx=(8, 0))
            self.cells.append(cells)

    def show(self, state, color, rows):
        self.state.configure(text=f"{self.name}: {state}", foreground=color)
        for cells, values in zip(self.cells, rows):
            for cell, v in zip(cells, values):
                cell.configure(text=str(v))


class CalibrationPanel(Panel):
    title = "Calibration"
    streams = True
    STEPS = {
        0: "Choose what to calibrate, then Start.",
        1: "Leave everything alone: measuring the rest positions...",
        2: "Move both sticks around their full range and pull the triggers fully, a few "
           "times. Then Save.",
        3: "Saved.",
    }

    def __init__(self, master):
        super().__init__(master)
        self.status = None
        self.poll = 0
        ctl = ttk.Frame(self)
        ctl.pack(anchor="w", fill="x")
        self.target = tk.IntVar(value=0)
        for text, v in (("Sticks and triggers", 0), ("Sticks", p.CAL_STICKS),
                        ("Triggers", p.CAL_TRIGGERS)):
            ttk.Radiobutton(ctl, text=text, variable=self.target, value=v).pack(side="left")
        self.btns = {}
        for text, step in (("Start", p.CAL_START), ("Save", p.CAL_SAVE),
                           ("Cancel", p.CAL_CANCEL), ("Erase", p.CAL_ERASE)):
            b = ttk.Button(ctl, text=text, command=lambda s=step: self.step(s))
            b.pack(side="left", padx=(12 if text == "Start" else 4, 0))
            self.btns[step] = b
        self.text = ttk.Label(self, text="", wraplength=560, font="TkHeadingFont")
        self.text.pack(anchor="w", pady=12)
        views = ttk.Frame(self)
        views.pack(anchor="w")
        self.ls = w.CalStickView(views, "Left stick (raw)")
        self.ls.grid(row=0, column=0, padx=8)
        self.rs = w.CalStickView(views, "Right stick (raw)")
        self.rs.grid(row=0, column=2, padx=8)
        self.info = []
        for col, name in ((1, "Left stick"), (3, "Right stick")):
            info = CalInfo(views, name, ("min", "centre", "max"), ("X", "Y"))
            info.grid(row=0, column=col, padx=(0, 16), sticky="n", pady=8)
            self.info.append(info)
        self.full = []
        self.trg = []
        for name in ("LT", "RT"):
            row = ttk.Frame(self)
            row.pack(anchor="w", pady=(8, 0))
            view = w.CalTriggerView(row, f"{name}: range (blue), position, full from (green)")
            view.pack(side="left")
            box = ttk.Frame(row)
            box.pack(side="left", padx=12)
            ttk.Label(box, text="Full from %").pack(anchor="w")
            v = tk.IntVar(value=95)
            sb = ttk.Spinbox(box, from_=p.TRG_FULL_MIN, to=100, textvariable=v, width=5,
                             command=self.set_full)
            sb.pack(anchor="w")
            sb.bind("<Return>", lambda _e: self.set_full())
            sb.bind("<FocusOut>", lambda _e: self.set_full())
            info = CalInfo(row, name, ("",), ("rest", "full pull", "full from"))
            info.pack(side="left", padx=(4, 0))
            self.trg.append(view)
            self.full.append(v)
            self.info.append(info)
        self.lt, self.rt = self.trg
        ttk.Label(self, text="A calibrated trigger reports full from its % of the travel; "
                             "check it live in the Input test.", foreground=w.RAW).pack(
            anchor="w", pady=(8, 0))

    def on_show(self):
        self.refresh()

    def refresh(self):
        if self.device:
            self.status = self.call("calibrate")
            if self.status:
                for v, pct in zip(self.full, self.status.full_pct):
                    v.set(pct)

    def set_full(self):
        """Apply the full pull thresholds (the firmware saves 1 s later)."""
        try:
            pct = [max(p.TRG_FULL_MIN, min(100, int(v.get()))) for v in self.full]
        except (tk.TclError, ValueError):
            return
        for v, x in zip(self.full, pct):
            v.set(x)
        if self.status and tuple(pct) == tuple(self.status.full_pct):
            return
        self.call("set_trigger_full", *pct)

    def step(self, step):
        if step == p.CAL_ERASE and not messagebox.askyesno(
                "Erase calibration", "Erase the stored calibration? Sticks and triggers "
                "go back to auto-ranging.", parent=self):
            return
        st = self.call("calibrate", step, self.target.get())
        if st:
            self.status = st

    def tick(self):
        self.poll += 1
        if self.poll % 3 == 0:  # status ~10 Hz
            try:
                self.status = self.device.calibrate() if self.device else None
            except (p.ProtocolError, OSError):
                self.status = None
        st, s = self.status, self.device and self.device.latest_stream
        if not st or not s:
            return
        idle = st.state in (0, 3)
        self.btns[p.CAL_START].state(["!disabled" if idle else "disabled"])
        self.btns[p.CAL_ERASE].state(["!disabled" if idle else "disabled"])
        self.btns[p.CAL_SAVE].state(["!disabled" if st.state == 2 else "disabled"])
        self.btns[p.CAL_CANCEL].state(["disabled" if idle else "!disabled"])
        text = self.STEPS.get(st.state, "")
        if st.state == 2 and st.refused:
            bad = ", ".join(n for i, n in enumerate(p.AXES) if st.refused & (1 << i))
            text = f"Not enough yet ({bad}): keep going, then Save again. A trigger moved " \
                   f"both ways from rest means it was pressed at the start: Cancel, Start."
        self.text.configure(text=text, foreground=w.BAD if st.refused else w.FG)
        r = st.ranges
        bit = lambda i: bool(st.refused & (1 << i))  # noqa: E731
        inv = lambda i: bool(st.inverted & (1 << i))  # noqa: E731
        self.ls.show(s.adc[0], s.adc[1], r[0], r[1], bit(0) or bit(1), inv(0), inv(1))
        self.rs.show(s.adc[2], s.adc[3], r[2], r[3], bit(2) or bit(3), inv(2), inv(3))
        self.lt.show(s.adc[4], r[4], st.full_pct[0], bit(4))
        self.rt.show(s.adc[5], r[5], st.full_pct[1], bit(5))
        self.show_info(st)

    def axis_state(self, st, axes, target):
        """Label and colour for a stick (both axes) or a trigger."""
        bits = sum(1 << i for i in axes)
        if st.state in (1, 2) and st.targets & target:
            return ("not enough yet", w.BAD) if st.refused & bits else ("this run", w.ACCENT)
        if st.stored & bits == bits:
            return "stored", w.OK
        return "not stored (auto-ranging)", w.RAW

    def show_info(self, st):
        r = st.ranges
        for info, (x, y) in zip(self.info, ((0, 1), (2, 3))):
            info.show(*self.axis_state(st, (x, y), p.CAL_STICKS),
                      [(r[a].min, r[a].center, r[a].max) for a in (x, y)])
        for info, a, pct in zip(self.info[2:], (4, 5), st.full_pct):
            sign, travel = w.trigger_pull(r[a])
            rest = r[a].center
            pull = "up" if sign > 0 else "down"
            info.show(*self.axis_state(st, (a,), p.CAL_TRIGGERS),
                      [(rest,), (f"{rest + sign * travel} ({pull})",),
                       (f"{rest + int(sign * travel * pct / 100)} ({pct} %)",)])


class RumblePanel(Panel):
    title = "Rumble"
    MOTORS = ("Heavy (left grip)", "Weak (right grip)", "LT trigger", "RT trigger")

    def __init__(self, master):
        super().__init__(master)
        self.levels = []
        for i, name in enumerate(self.MOTORS):
            ttk.Label(self, text=name).grid(row=i, column=0, sticky="w", padx=(0, 12))
            v = tk.IntVar(value=160)
            ttk.Scale(self, from_=0, to=255, variable=v, length=260).grid(row=i, column=1)
            ttk.Label(self, textvariable=v, width=4).grid(row=i, column=2)
            ttk.Button(self, text="Test", command=lambda m=i: self.run(m)).grid(
                row=i, column=3, padx=8, pady=2)
            self.levels.append(v)
        ttk.Label(self, text="Duration (ms)").grid(row=4, column=0, sticky="w", pady=(12, 0))
        self.ms = tk.IntVar(value=1000)
        ttk.Spinbox(self, from_=100, to=2000, increment=100, textvariable=self.ms,
                    width=6).grid(row=4, column=1, sticky="w", pady=(12, 0))
        row = ttk.Frame(self)
        row.grid(row=5, column=0, columnspan=4, sticky="w", pady=12)
        ttk.Button(row, text="All together", command=lambda: self.run(None)).pack(side="left")
        ttk.Button(row, text="Stop", command=self.stop).pack(side="left", padx=8)

    def run(self, motor):
        levels = [int(v.get()) if motor in (None, i) else 0 for i, v in enumerate(self.levels)]
        self.call("rumble", levels, max(0, min(2000, int(self.ms.get()))))

    def stop(self):
        self.call("rumble", [0, 0, 0, 0], 0)


class PaddlesPanel(Panel):
    title = "Paddles"
    NAMES = ("P1 (left upper)", "P2 (left lower)", "P3 (right upper)", "P4 (right lower)")
    CHOICES = ["None"] + list(p.BUTTONS)

    def __init__(self, master):
        super().__init__(master)
        self.enabled = tk.BooleanVar()
        ttk.Checkbutton(self, text="Paddles fitted (off: ignored)", variable=self.enabled,
                        command=self.set_enabled).grid(row=0, column=0, columnspan=2,
                                                       sticky="w")
        ttk.Label(self, text="In XInput mode each paddle presses a button; in HID mode it "
                  "is a button of its own.", wraplength=460).grid(
            row=1, column=0, columnspan=2, sticky="w", pady=(8, 8))
        self.binds = []
        for i, name in enumerate(self.NAMES):
            ttk.Label(self, text=name).grid(row=2 + i, column=0, sticky="w", padx=(0, 12))
            c = ttk.Combobox(self, values=self.CHOICES, state="readonly", width=14)
            c.grid(row=2 + i, column=1, sticky="w", pady=2)
            c.bind("<<ComboboxSelected>>", lambda _e: self.set_bindings())
            self.binds.append(c)

    def on_show(self):
        r = self.call("paddles") if self.device else None
        if not r:
            return
        on, values = r
        self.enabled.set(on)
        names = {v: k for k, v in p.BUTTONS.items()}
        for c, v in zip(self.binds, values):
            c.set(names.get(v, "None"))

    def set_enabled(self):
        self.call("set_paddles_enabled", self.enabled.get())

    def set_bindings(self):
        values = [p.BUTTONS.get(c.get(), 0) for c in self.binds]
        self.call("set_bindings", values)


class LedPanel(Panel):
    """Brightness sliders (1-255); changes go out at most every SEND_MS while dragging.

    ROWS: (key, label, switchable); a switchable row has an on/off checkbox."""

    title = "LED brightness"
    NOTE = ""
    ROWS = ()
    SETTING = 0
    SEND_MS = 50

    def __init__(self, master):
        super().__init__(master)
        self.values = {}  # what the device has (keys as in ROWS, plus "<key>_on")
        self.pending = None
        ttk.Label(self, text=self.NOTE, wraplength=460).pack(anchor="w")
        grid = ttk.Frame(self)
        grid.pack(anchor="w", pady=12)
        self.scales, self.labels, self.checks = {}, {}, {}
        for r, (key, label, switchable) in enumerate(self.ROWS):
            ttk.Label(grid, text=label).grid(row=r, column=0, sticky="w", padx=(0, 12))
            sc = ttk.Scale(grid, from_=1, to=255, length=280,
                           command=lambda _v, k=key: self.slid(k))
            sc.grid(row=r, column=1, pady=4)
            self.scales[key] = sc
            self.labels[key] = ttk.Label(grid, width=4)
            self.labels[key].grid(row=r, column=2, padx=8)
            if switchable:
                v = tk.BooleanVar()
                ttk.Checkbutton(grid, text="On", variable=v,
                                command=lambda k=key: self.switched(k)).grid(row=r, column=3)
                self.checks[key] = v
        self.missing = ttk.Label(self, text="This firmware has no brightness setting; "
                                 "update it.")

    # device access, per device
    def read(self):
        raise NotImplementedError

    def write(self, values):
        raise NotImplementedError

    def on_show(self):
        if not self.device:
            return
        has = self.SETTING in (self.call("setting_ids") or [])
        for w in self.scales.values():
            w.state(["!disabled"] if has else ["disabled"])
        if not has:
            self.missing.pack(anchor="w")
            return
        self.missing.pack_forget()
        v = self.read()
        if v is None:
            return
        self.values = dict(v)
        for key, sc in self.scales.items():
            sc.set(v[key])
            self.labels[key].configure(text=str(v[key]))
        for key, var in self.checks.items():
            var.set(v[key + "_on"])
            self.scales[key].state(["!disabled"] if v[key + "_on"] else ["disabled"])

    def slid(self, key):
        v = round(self.scales[key].get())
        self.labels[key].configure(text=str(v))
        if not self.values or v == self.values[key]:
            return
        self.values[key] = v
        self.queue()

    def switched(self, key):
        if not self.values:
            return
        on = self.checks[key].get()
        self.values[key + "_on"] = on
        self.scales[key].state(["!disabled"] if on else ["disabled"])
        self.queue()

    def queue(self):
        if self.pending is None:
            self.pending = self.after(self.SEND_MS, self.send)

    def send(self):
        self.pending = None
        self.write(dict(self.values))


class ControllerLedPanel(LedPanel):
    NOTE = "Guide LED: it shows pairing and the link, so it can't be turned off."
    ROWS = (("guide", "Guide LED", False),)
    SETTING = p.SET_LED

    def read(self):
        v = self.call("led")
        return None if v is None else {"guide": v}

    def write(self, values):
        self.call("set_led", values["guide"])


class DongleLedPanel(LedPanel):
    NOTE = ("The pair LED shows pairing, so it can't be turned off; the player "
            "and power LEDs can.")
    ROWS = (("pair", "Pair LED", False), ("player", "Player LEDs", True),
            ("power", "Power LED", True))
    SETTING = p.SET_DONGLE_LEDS

    def read(self):
        return self.call("dongle_leds")

    def write(self, values):
        self.call("set_dongle_leds", values)


class ResetPanel(Panel):
    title = "Factory reset"
    SCOPES = (("Settings (paddles, LED, trigger full pull, stored USB mode)",
               p.RESET_SETTINGS),
              ("Stick and trigger calibration", p.RESET_CALIBRATION),
              ("Pairing (forget the dongle)", p.RESET_PAIRING))

    def __init__(self, master):
        super().__init__(master)
        self.vars = []
        for text, bit in self.SCOPES:
            v = tk.BooleanVar()
            ttk.Checkbutton(self, text=text, variable=v).pack(anchor="w")
            self.vars.append((v, bit, text))
        ttk.Button(self, text="Reset and restart", command=self.reset).pack(anchor="w",
                                                                            pady=12)

    def reset(self):
        scope = sum(bit for v, bit, _ in self.vars if v.get())
        if not scope:
            messagebox.showinfo("Factory reset", "Choose what to reset.", parent=self)
            return
        what = "\n".join(f"• {t}" for v, _, t in self.vars if v.get())
        if messagebox.askyesno("Factory reset", f"Erase:\n{what}\n\nThe device restarts.",
                               parent=self):
            for v, _, _ in self.vars:
                v.set(False)
            self.call("factory_reset", scope)


class DongleResetPanel(ResetPanel):
    SCOPES = (("Settings (LED brightness)", p.RESET_SETTINGS),
              ("Pairing (forget all controllers)", p.RESET_PAIRING))


class FirmwarePanel(Panel):
    """Firmware update from a .uf2 the user picks; the update runs in a thread."""

    title = "Firmware update"

    def __init__(self, master, app, kind):
        super().__init__(master)
        self.app = app
        self.kind = kind
        self.image = None
        self.current = ttk.Label(self, text="")
        self.current.pack(anchor="w")
        row = ttk.Frame(self)
        row.pack(anchor="w", pady=12)
        ttk.Button(row, text="Choose .uf2 file...", command=self.choose).pack(side="left")
        self.update_btn = ttk.Button(row, text="Update", command=self.update,
                                     state="disabled")
        self.update_btn.pack(side="left", padx=8)
        self.file = ttk.Label(self, text="No file chosen.", wraplength=520)
        self.file.pack(anchor="w")
        self.progress = ttk.Label(self, text="", wraplength=520)
        self.progress.pack(anchor="w", pady=12)

    def attach(self, device):
        super().attach(device)
        self.current.configure(text=f"Installed: {device.info.firmware_str}" if device
                               else f"No {self.kind} connected.")
        self._ready()

    def _ready(self):
        ok = self.device and self.image and self.image.device == self.kind
        self.update_btn.state(["!disabled" if ok else "disabled"])

    def choose(self):
        path = filedialog.askopenfilename(parent=self, title="Firmware file",
                                          filetypes=[("UF2 firmware", "*.uf2"),
                                                     ("All files", "*")])
        if not path:
            return
        try:
            self.image = uf2.load(path)
        except (OSError, uf2.Uf2Error) as e:
            self.image = None
            self.file.configure(text=f"{path}: {e}", foreground=w.BAD)
        else:
            match = self.image.device == self.kind
            self.file.configure(
                text=f"{path}\n{self.image.device} firmware {self.image.version_str}"
                     + ("" if match else f": not for the {self.kind}"),
                foreground=w.FG if match else w.BAD)
        self._ready()

    def update(self):
        device, image = self.device, self.image
        self.update_btn.state(["disabled"])
        self.app.updating.add(self.kind)
        self.app.release(self.kind)

        def say(msg):
            self.after(0, lambda: self.progress.configure(text=msg, foreground=w.FG))

        def run():
            try:
                uf2.update(device, image, say)
            except (OSError, p.ProtocolError, uf2.Uf2Error) as e:
                self.after(0, lambda: self.progress.configure(text=f"Failed: {e}",
                                                              foreground=w.BAD))
            finally:
                self.after(0, lambda: self.app.updating.discard(self.kind))

        threading.Thread(target=run, daemon=True).start()
