"""Colors from theme files: themes/*.toml bundled with the app, and the
user's own in <app folder>/themes/ (files.USER_THEMES), which replace a
bundled one of the same name. View → Theme lists them by file name and
writes the choice to settings.toml (files.SETTINGS_PATH); it shows from the
next start.

A theme sets any of the keys of LIGHT (themes/light.toml is the template);
base = "<name>" starts it from another theme, and keys it leaves out come
from light. Unknown keys, bad colors and unreadable files are skipped with a
warning on stderr: the app always starts.
"""

import os
import re
import sys
import tkinter as tk
from tkinter import ttk

from .. import files
from . import widgets as w

try:
    import tomllib
except ImportError:  # Python < 3.11 (source runs only; the builds have 3.11)
    tomllib = None

# the keys a theme can set, with light's colors: the start of every theme,
# and the last resort when theme files can't be read (themes/light.toml has
# the same colors)
LIGHT = {
    "background": "#dcdad5",  # windows, panels
    "foreground": "#202020",  # text
    "secondary": "#888888",  # labels beside values
    "disabled": "#999999",  # disabled text
    "canvas": "#f4f4f4",  # behind sticks, triggers, buttons
    "dim": "#b0b0b0",  # outlines, unlit buttons
    "accent": "#1f6fd1",  # stick dots, bars, calibration ranges
    "ok": "#2e9d4b",  # pressed, stored
    "bad": "#d13b2e",  # errors, refused calibration
    "on_ok": "#ffffff",  # text on a lit button
    "range": "#cfe0f6",  # trigger calibration range
    "range_bad": "#f6d2cf",  # the same, refused
    "field": "#ffffff",  # entry fields, check boxes, text boxes
    "control": "#dcdad5",  # buttons, scrollbars
    "tab": "#bab5ab",  # unselected tabs, pressed buttons
    "active": "#eeebe7",  # under the mouse
    "shade": "#cfcdc8",  # bevel shadow
    "border": "#9e9a91",
    "trough": "#bab5ab",  # slider and scrollbar tracks
    "select": "#4a6984",  # selection, menu highlight
    "select_text": "#ffffff",
}



def _warn(msg):
    print(f"theme: {msg}", file=sys.stderr)


def _read(path):
    try:
        with open(path, "rb") as f:
            return tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as e:
        _warn(f"{path}: {e}")
        return None


def available():
    """{name: file}: the bundled themes, then the user's (same name wins)."""
    if tomllib is None:
        _warn("theme files need Python 3.11 or newer; using light")
        return {"light": None}
    found = {}
    for folder in (files.bundled("themes"), files.USER_THEMES):
        try:
            names = sorted(os.listdir(folder))
        except OSError:
            continue
        for n in names:
            if n.endswith(".toml"):
                found[n[:-len(".toml")]] = os.path.join(folder, n)
    return found or {"light": None}


def colors(name, themes, seen=()):
    """name's colors: its base's (light at the bottom), then its own keys."""
    if name in seen:
        _warn(f"base loop: {' → '.join(seen + (name,))}")
        return dict(LIGHT)
    data = _read(themes[name]) if themes.get(name) else None
    if data is None:
        return dict(LIGHT)
    base = data.pop("base", None if name == "light" else "light")
    if base and base not in themes:
        _warn(f"{themes[name]}: no theme {base!r}; starting from light")
        base = "light" if name != "light" else None
    c = colors(base, themes, seen + (name,)) if base else dict(LIGHT)
    for key, value in data.items():
        if key not in LIGHT:
            _warn(f"{themes[name]}: unknown key {key!r} skipped")
        elif not isinstance(value, str):
            _warn(f"{themes[name]}: {key} = {value!r} is not a color string; skipped")
        else:
            c[key] = value
    return c


def selected():
    """The chosen theme's name (settings.toml), light by default."""
    if tomllib is None or not os.path.exists(files.SETTINGS_PATH):
        return "light"
    data = _read(files.SETTINGS_PATH) or {}
    name = data.get("theme", "light")
    return name if isinstance(name, str) else "light"


def select(name):
    """Write theme = name into settings.toml, keeping the rest of the file."""
    path = files.SETTINGS_PATH
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except FileNotFoundError:
        text = ""
    line = f'theme = "{name}"'
    text, n = re.subn(r"(?m)^[ \t]*theme[ \t]*=.*$", line, text, count=1)
    if not n:
        text = f"{line}\n{text}"
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def apply(root):
    """Style root with the chosen theme (before any other widget exists).
    Returns the theme's name."""
    themes = available()
    name = selected()
    if name not in themes:
        _warn(f"no theme {name!r}; using light")
        name = "light"
    c = colors(name, themes)
    for key, value in c.items():
        try:
            root.winfo_rgb(value)
        except tk.TclError:
            _warn(f"{name}: {key} = {value!r} is not a color; using light's")
            c[key] = LIGHT[key]

    # the drawings (widgets.py) and status colors (panels.py)
    w.BG, w.FG, w.DIM, w.RAW = c["canvas"], c["foreground"], c["dim"], c["secondary"]
    w.ACCENT, w.OK, w.BAD, w.ON_OK = c["accent"], c["ok"], c["bad"], c["on_ok"]
    w.RANGE, w.RANGE_BAD = c["range"], c["range_bad"]

    s = ttk.Style(root)
    if "clam" in s.theme_names():
        s.theme_use("clam")
    s.configure(".", background=c["background"], foreground=c["foreground"],
                bordercolor=c["border"], darkcolor=c["shade"], lightcolor=c["active"],
                troughcolor=c["trough"], fieldbackground=c["field"],
                selectbackground=c["select"], selectforeground=c["select_text"],
                insertcolor=c["foreground"], arrowcolor=c["foreground"])
    s.map(".", background=[("disabled", c["background"]), ("active", c["active"])],
          foreground=[("disabled", c["disabled"])])
    s.configure("TButton", background=c["control"])
    s.map("TButton", background=[("disabled", c["background"]), ("pressed", c["tab"]),
                                 ("active", c["active"])],
          lightcolor=[("pressed", c["tab"])], darkcolor=[("pressed", c["tab"])])
    s.map("TNotebook.Tab", background=[("selected", c["background"]), ("!selected", c["tab"])],
          lightcolor=[("selected", c["active"]), ("!selected", c["shade"])])
    for cls in ("TCheckbutton", "TRadiobutton"):
        s.configure(cls, indicatorbackground=c["field"], indicatorforeground=c["foreground"])
        s.map(cls, indicatorbackground=[("pressed", c["background"]),
                                        ("disabled", c["background"])])
    s.configure("TSpinbox", background=c["control"])
    s.map("TCombobox", fieldbackground=[("readonly focus", c["select"]),
                                        ("readonly", c["background"])],
          foreground=[("readonly focus", c["select_text"])],
          arrowcolor=[("disabled", c["disabled"])])

    # classic Tk parts: the window, menus, dialogs, the About text box and its
    # scrollbar, the combobox list
    root.configure(background=c["background"])
    for pattern, key in [
        ("*Toplevel.background", "background"), ("*Frame.background", "background"),
        ("*Menu.background", "background"), ("*Menu.foreground", "foreground"),
        ("*Menu.activeBackground", "select"), ("*Menu.activeForeground", "select_text"),
        ("*Menu.disabledForeground", "disabled"), ("*Menu.selectColor", "foreground"),
        ("*Text.background", "field"), ("*Text.foreground", "foreground"),
        ("*Text.insertBackground", "foreground"), ("*Text.selectBackground", "select"),
        ("*Text.selectForeground", "select_text"),
        ("*Scrollbar.background", "control"), ("*Scrollbar.troughColor", "trough"),
        ("*Scrollbar.activeBackground", "active"),
        ("*TCombobox*Listbox.background", "field"),
        ("*TCombobox*Listbox.foreground", "foreground"),
        ("*TCombobox*Listbox.selectBackground", "select"),
        ("*TCombobox*Listbox.selectForeground", "select_text"),
    ]:
        root.option_add(pattern, c[key])
    return name
