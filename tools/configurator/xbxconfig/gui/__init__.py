"""The window (Tkinter / ttk)."""


def main():
    from .app import App  # noqa: PLC0415

    App().mainloop()
