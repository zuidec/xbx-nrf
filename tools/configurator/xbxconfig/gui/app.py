"""Main window: a tab per device kind; each connects to the first such device
on USB and follows plugging and unplugging."""

import threading
import tkinter as tk
from tkinter import messagebox, ttk

from .. import __version__, files, transport
from .. import device as dev
from .. import protocol as p
from . import panels

WATCH_MS = 1000
TICK_MS = 33


class DeviceTab(ttk.Frame):
    def __init__(self, master, app, kind, panel_classes):
        super().__init__(master)
        self.app = app
        self.kind = kind
        self.device = None
        self.header = ttk.Label(self, text="", padding=(12, 8))
        self.header.pack(anchor="w")
        self.book = ttk.Notebook(self)
        self.book.pack(fill="both", expand=True)
        self.panels = []
        for cls in panel_classes:
            panel = cls(self.book, app, kind) if cls is panels.FirmwarePanel else cls(self.book)
            self.book.add(panel, text=cls.title)
            self.panels.append(panel)
        self.book.bind("<<NotebookTabChanged>>", lambda _e: self._shown_changed())
        self.shown = None
        self.attach(None)

    @property
    def visible(self):
        return self.app.book.select() == str(self)

    def current(self):
        return self.panels[self.book.index(self.book.select())]

    def attach(self, device):
        self.device = device
        if device:
            i = device.info
            self.header.configure(text=f"{self.kind.capitalize()} connected: firmware "
                                       f"{i.firmware_str}, {i.usb_mode} mode, serial {i.serial}")
        else:
            self.header.configure(text=f"No {self.kind} found: connect it over USB.")
        for panel in self.panels:
            panel.attach(device)
        self.shown = None
        self._shown_changed()

    def _shown_changed(self):
        panel = self.current() if self.visible else None
        if panel is self.shown:
            return
        if self.shown:
            self.shown.on_hide()
        self.shown = panel
        if panel and self.device:
            panel.on_show()
        self._stream(bool(panel and panel.streams))

    def _stream(self, on):
        if not self.device:
            return
        try:
            self.device.stream(panels.STREAM_HZ if on else 0)
        except p.StatusError:
            pass  # no stream on this device
        except (p.ProtocolError, OSError):
            pass  # unplugged: the watcher notices

    def tick(self):
        if self.shown and self.device:
            self.shown.tick()


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"xbx-nrf configurator {__version__}")
        self.minsize(720, 560)
        style = ttk.Style(self)
        if "clam" in style.theme_names():
            style.theme_use("clam")
        self.updating = set()  # kinds in a firmware update: not reconnected meanwhile
        self.banner = ttk.Frame(self, padding=8)
        self.banner_text = ttk.Label(self.banner, text="", wraplength=520)
        self.banner_text.pack(side="left")
        self.banner_btn = ttk.Button(self.banner, text="Give access...", command=self.install)
        self.book = ttk.Notebook(self)
        self.book.pack(fill="both", expand=True, padx=8, pady=8)
        self.tabs = {
            "controller": DeviceTab(self.book, self, "controller", [
                panels.InfoPanel, panels.InputPanel, panels.CalibrationPanel,
                panels.RumblePanel, panels.PaddlesPanel, panels.ResetPanel,
                panels.FirmwarePanel]),
            "dongle": DeviceTab(self.book, self, "dongle", [
                panels.InfoPanel, panels.FirmwarePanel]),
        }
        for kind, tab in self.tabs.items():
            self.book.add(tab, text=kind.capitalize())
        self.book.bind("<<NotebookTabChanged>>",
                       lambda _e: [t._shown_changed() for t in self.tabs.values()])
        self.protocol("WM_DELETE_WINDOW", self.quit_app)
        self.after(0, self.watch)
        self.after(TICK_MS, self.tick)

    def release(self, kind):
        """Detach kind's device without closing it (a firmware update owns it)."""
        self.tabs[kind].attach(None)

    def watch(self):
        no_access = []
        for kind, tab in self.tabs.items():
            if tab.device and not tab.device.alive:
                tab.device.close()
                tab.attach(None)
        if any(not t.device for k, t in self.tabs.items() if k not in self.updating):
            for f in dev.find():
                tab = self.tabs[f.kind]
                if tab.device or f.kind in self.updating:
                    continue
                try:
                    tab.attach(dev.Device(f))
                except transport.AccessError:
                    no_access.append(f.kind)
                except (p.ProtocolError, OSError) as e:
                    tab.header.configure(text=f"{f.kind.capitalize()} found, but: {e}")
        self.show_banner(no_access)
        self.after(WATCH_MS, self.watch)

    def show_banner(self, kinds):
        if not kinds:
            self.banner.pack_forget()
            return
        text = f"Found a {' and a '.join(kinds)}, but this user may not open it."
        if transport.LINUX:
            text += " Install the udev rule (asks for the admin password), then replug."
            self.banner_btn.pack(side="right")
        self.banner_text.configure(text=text)
        self.banner.pack(fill="x", before=self.book)

    def install(self):
        def run():
            try:
                files.install_udev_rule()
            except OSError as e:
                self.after(0, lambda: messagebox.showerror("xbx-config", str(e), parent=self))
            else:
                self.after(0, lambda: messagebox.showinfo(
                    "xbx-config", "Installed. Unplug the device and plug it in again.",
                    parent=self))

        threading.Thread(target=run, daemon=True).start()

    def tick(self):
        for tab in self.tabs.values():
            tab.tick()
        self.after(TICK_MS, self.tick)

    def quit_app(self):
        for tab in self.tabs.values():
            if tab.device:
                tab._stream(False)
                tab.device.close()
        self.destroy()
