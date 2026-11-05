"""Drawing widgets: sticks, triggers, buttons."""

import tkinter as tk
from tkinter import ttk

from .. import protocol as p

BG = "#f4f4f4"
FG = "#202020"
DIM = "#b0b0b0"
ACCENT = "#1f6fd1"
OK = "#2e9d4b"
BAD = "#d13b2e"
RAW = "#888888"
ON_OK = "#ffffff"
RANGE = "#cfe0f6"
RANGE_BAD = "#f6d2cf"
# the defaults; theme.py sets them from the chosen theme before the window

ADC_MAX = 4095


class StickView(tk.Canvas):
    """A stick: its reported position (dot) inside the unit circle."""

    def __init__(self, master, title, size=150):
        super().__init__(master, width=size, height=size + 18, bg=BG, highlightthickness=0)
        self.size = size
        m = 6
        self.create_text(size / 2, size + 9, text=title, fill=FG)
        self.create_oval(m, m, size - m, size - m, outline=DIM, width=2)
        self.create_line(size / 2, m, size / 2, size - m, fill=DIM, dash=(2, 3))
        self.create_line(m, size / 2, size - m, size / 2, fill=DIM, dash=(2, 3))
        self.dot = self.create_oval(0, 0, 0, 0, fill=ACCENT, outline="")
        self.r = size / 2 - m

    def show(self, x, y, pressed=False):
        """x, y: -32767..32767, up = positive."""
        c = self.size / 2
        px = c + x / p.STICK_MAX * self.r
        py = c - y / p.STICK_MAX * self.r
        d = 9 if pressed else 7
        self.coords(self.dot, px - d, py - d, px + d, py + d)
        self.itemconfigure(self.dot, fill=OK if pressed else ACCENT)


class TriggerBar(tk.Canvas):
    """A trigger: 0..1023 as a filled bar."""

    def __init__(self, master, title, width=150):
        super().__init__(master, width=width, height=40, bg=BG, highlightthickness=0)
        self.w = width
        self.create_text(width / 2, 32, text=title, fill=FG)
        self.create_rectangle(2, 2, width - 2, 20, outline=DIM, width=2)
        self.bar = self.create_rectangle(3, 3, 3, 19, fill=ACCENT, outline="")
        self.label = self.create_text(width / 2, 11, text="0", fill=FG)

    def show(self, v):
        self.coords(self.bar, 3, 3, 3 + (self.w - 6) * v / p.TRIGGER_MAX, 19)
        self.itemconfigure(self.label, text=str(v))


class ButtonGrid(ttk.Frame):
    """Every button as a label that lights while pressed."""

    LAYOUT = [
        ["LB", "View", "Guide", "Menu", "RB"],
        ["D-pad up", "D-pad down", "D-pad left", "D-pad right", "Share"],
        ["A", "B", "X", "Y", "Pair"],
        ["LS", "RS", "", "", ""],
        ["P1", "P2", "P3", "P4", ""],
    ]

    def __init__(self, master):
        super().__init__(master)
        self.labels = {}
        for r, row in enumerate(self.LAYOUT):
            for c, name in enumerate(row):
                if not name:
                    continue
                lbl = tk.Label(self, text=name, width=10, relief="groove", bg=BG, fg=DIM)
                lbl.grid(row=r, column=c, padx=2, pady=2)
                self.labels[name] = lbl

    def show(self, buttons, ext):
        for name, lbl in self.labels.items():
            bit = p.BUTTONS.get(name)
            on = (buttons & bit) if bit else (ext & p.BUTTONS_EXT[name])
            lbl.configure(bg=OK if on else BG, fg=ON_OK if on else DIM)


class CalStickView(tk.Canvas):
    """Calibration view of one stick: raw ADC position and the range seen,
    oriented as reported (an inverted axis is drawn mirrored)."""

    def __init__(self, master, title, size=170):
        super().__init__(master, width=size, height=size + 18, bg=BG, highlightthickness=0)
        self.size = size
        self.m = 6
        self.create_text(size / 2, size + 9, text=title, fill=FG)
        self.frame = self.create_rectangle(self.m, self.m, size - self.m, size - self.m,
                                           outline=DIM, width=1)
        self.range = self.create_rectangle(0, 0, 0, 0, outline=ACCENT, width=2)
        self.hc = self.create_line(0, 0, 0, 0, fill=ACCENT)
        self.vc = self.create_line(0, 0, 0, 0, fill=ACCENT)
        self.dot = self.create_oval(0, 0, 0, 0, fill=FG, outline="")
        self.inv_x = self.inv_y = False

    def _frac(self, v, inverted):
        f = v / ADC_MAX
        return 1 - f if inverted else f

    def _x(self, v):
        return self.m + self._frac(v, self.inv_x) * (self.size - 2 * self.m)

    def _y(self, v):  # reported up = positive, screen up = smaller
        return self.size - self.m - self._frac(v, self.inv_y) * (self.size - 2 * self.m)

    def show(self, raw_x, raw_y, rx, ry, refused, inv_x=False, inv_y=False):
        """rx, ry: AxisRange of each axis; inv_x, inv_y: reported against
        the counts."""
        self.inv_x, self.inv_y = inv_x, inv_y
        self.coords(self.range, self._x(rx.min), self._y(ry.max), self._x(rx.max),
                    self._y(ry.min))
        self.coords(self.hc, self._x(rx.min), self._y(ry.center), self._x(rx.max),
                    self._y(ry.center))
        self.coords(self.vc, self._x(rx.center), self._y(ry.min), self._x(rx.center),
                    self._y(ry.max))
        color = BAD if refused else ACCENT
        self.itemconfigure(self.range, outline=color)
        self.itemconfigure(self.hc, fill=color)
        self.itemconfigure(self.vc, fill=color)
        x, y = self._x(raw_x), self._y(raw_y)
        self.coords(self.dot, x - 4, y - 4, x + 4, y + 4)


def trigger_pull(rng):
    """Pull direction (+1 / -1) and travel of a trigger's AxisRange (centre =
    rest); the longer side wins when it reaches both ways (auto-ranging)."""
    up, down = rng.max - rng.center, rng.center - rng.min
    return (1, up) if up >= down else (-1, down)


class CalTriggerView(tk.Canvas):
    """Calibration view of one trigger, released at the left and pulled to the
    right whichever way its ADC value moves: the range seen, the current
    position and the full pull threshold. Spans rest to the ADC end."""

    def __init__(self, master, title, width=360):
        super().__init__(master, width=width, height=56, bg=BG, highlightthickness=0)
        self.w = width
        self.m = 6
        self.create_text(width / 2, 48, text=title, fill=FG)
        self.create_rectangle(self.m, 10, width - self.m, 30, outline=DIM)
        self.range = self.create_rectangle(0, 11, 0, 29, fill=RANGE, outline="")
        self.full = self.create_line(0, 6, 0, 34, fill=OK, width=2, dash=(3, 2))
        self.pos = self.create_line(0, 8, 0, 32, fill=ACCENT, width=3)

    def show(self, raw, rng, full_pct, refused):
        """rng: AxisRange (centre = rest)."""
        rest = rng.center
        sign, travel = trigger_pull(rng)
        span = max(1, ADC_MAX - rest if sign > 0 else rest)

        def x(offset):  # counts pulled from rest
            return self.m + max(0, min(1, offset / span)) * (self.w - 2 * self.m)

        self.coords(self.range, x(0), 11, x(travel), 29)
        self.itemconfigure(self.range, fill=RANGE_BAD if refused else RANGE)
        full = x(travel * full_pct / 100)
        self.coords(self.full, full, 6, full, 34)
        self.itemconfigure(self.full, state="normal" if travel > 0 else "hidden")
        pos = x((raw - rest) * sign)
        self.coords(self.pos, pos, 8, pos, 32)
