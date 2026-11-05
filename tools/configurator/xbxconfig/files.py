"""Files the app installs on the user's system: kept as the real files in
tools/configurator/files/, bundled by build.sh."""

import os
import shutil
import subprocess
import sys
import tempfile

UDEV_RULE = "70-xbx-nrf.rules"
NOTICES = "THIRD-PARTY-NOTICES.txt"  # made by the build (notices.py)
UDEV_DIR = "/etc/udev/rules.d"

# SDL mapping with Share and the paddles (HID mode; docs/protocol.md), for the
# user only: SDL (Steam, games) reads the file named by the variable, which
# systemd sets for the session from environment.d at login.
SDL_DB = "gamecontrollerdb.txt"
SDL_VAR = "SDL_GAMECONTROLLERCONFIG_FILE"
_CONFIG = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
SDL_DB_PATH = os.path.join(_CONFIG, "xbx-nrf", SDL_DB)
SDL_ENV_PATH = os.path.join(_CONFIG, "environment.d", "50-xbx-nrf.conf")


def path(name):
    """A bundled file: next to the package, or in PyInstaller's unpack dir."""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return os.path.join(base, "files", name)


def notices():
    """The build's third-party notices; None when run from source."""
    try:
        with open(path(NOTICES), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return None


def udev_rule_installed():
    target = os.path.join(UDEV_DIR, UDEV_RULE)
    try:
        with open(target, "rb") as a, open(path(UDEV_RULE), "rb") as b:
            return a.read() == b.read()
    except OSError:
        return False


def _as_root(script, what):
    """Run a shell script as root via pkexec (graphical prompt) or sudo (terminal)."""
    tool = shutil.which("pkexec") or shutil.which("sudo")
    if not tool:
        raise OSError(f"neither pkexec nor sudo found: {what} by hand")
    r = subprocess.run([tool, "sh", "-c", script], capture_output=True, text=True)
    if r.returncode:
        raise OSError(f"{what} failed: {r.stderr.strip() or r.returncode}")


RELOAD = "udevadm control --reload-rules && udevadm trigger --subsystem-match=hidraw"


def install_udev_rule():
    """Copy the rule into /etc/udev/rules.d and reload udev, as root. Replug
    the device after."""
    # PyInstaller's unpack dir may not be readable by root's helper: copy out first
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, UDEV_RULE)
        shutil.copyfile(path(UDEV_RULE), src)
        os.chmod(tmp, 0o755)
        os.chmod(src, 0o644)
        _as_root(f"install -m 644 '{src}' '{UDEV_DIR}/{UDEV_RULE}' && {RELOAD}",
                 f"installing {UDEV_DIR}/{UDEV_RULE}")


def _same(a, b):
    try:
        with open(a, "rb") as fa, open(b, "rb") as fb:
            return fa.read() == fb.read()
    except OSError:
        return False


def _env_line():
    return f"{SDL_VAR}={SDL_DB_PATH}\n"


def sdl_mapping_installed():
    try:
        with open(SDL_ENV_PATH) as f:
            env_ok = f.read() == _env_line()
    except OSError:
        env_ok = False
    return env_ok and _same(SDL_DB_PATH, path(SDL_DB))


def install_sdl_mapping():
    """Write the mapping and the environment.d entry (no root). Takes effect
    at the next login."""
    other = os.environ.get(SDL_VAR)
    if other and other != SDL_DB_PATH:
        raise OSError(f"{SDL_VAR} is already set to {other}: add the lines of "
                      f"{path(SDL_DB)} to that file instead")
    os.makedirs(os.path.dirname(SDL_DB_PATH), exist_ok=True)
    os.makedirs(os.path.dirname(SDL_ENV_PATH), exist_ok=True)
    shutil.copyfile(path(SDL_DB), SDL_DB_PATH)
    with open(SDL_ENV_PATH, "w") as f:
        f.write(_env_line())


def missing():
    """What isn't installed yet: (label, install function, needs root) per
    item. Linux only; Windows and macOS need nothing."""
    if not sys.platform.startswith("linux"):
        return []
    items = []
    if not sdl_mapping_installed():
        items.append((f"SDL / Steam mapping: Share and the paddles in HID mode, from the "
                      f"next login\n{SDL_DB_PATH}\n{SDL_ENV_PATH}", install_sdl_mapping,
                      False))
    if not udev_rule_installed():
        items.append((f"udev rule: device access (admin password)\n{UDEV_DIR}/{UDEV_RULE}",
                      install_udev_rule, True))
    return items


def installed():
    """What the app put on this system (paths): on Linux the udev rule and the
    SDL mapping files. Windows and macOS: nothing (no driver, no settings
    files)."""
    if not sys.platform.startswith("linux"):
        return []
    paths = [os.path.join(UDEV_DIR, UDEV_RULE), SDL_DB_PATH, SDL_ENV_PATH]
    return [p for p in paths if os.path.exists(p)]


def needs_root(paths):
    return any(p.startswith(UDEV_DIR) for p in paths)


def app_file():
    """The one-file build itself (None when run from source)."""
    return sys.executable if getattr(sys, "frozen", False) else None


def uninstall():
    """Remove what installed() lists (the udev rule as root); returns the
    paths removed."""
    paths = installed()
    for p in paths:
        if not p.startswith(UDEV_DIR):
            os.remove(p)
    root = [p for p in paths if p.startswith(UDEV_DIR)]
    if root:
        quoted = " ".join(f"'{p}'" for p in root)
        _as_root(f"rm -f {quoted} && {RELOAD}", f"removing {', '.join(root)}")
    try:
        os.rmdir(os.path.dirname(SDL_DB_PATH))  # ours, if empty now
    except OSError:
        pass
    return paths
