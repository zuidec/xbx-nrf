"""Files the app installs on the user's system: kept as the real files in
tools/configurator/files/, bundled by build.sh."""

import os
import shutil
import subprocess
import sys
import tempfile

UDEV_RULE = "70-xbx-nrf.rules"
UDEV_DIR = "/etc/udev/rules.d"


def path(name):
    """A bundled file: next to the package, or in PyInstaller's unpack dir."""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return os.path.join(base, "files", name)


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


def installed():
    """What the app put on this system (paths); only the Linux udev rule.
    Windows and macOS: nothing (no driver, no settings files)."""
    target = os.path.join(UDEV_DIR, UDEV_RULE)
    return [target] if sys.platform.startswith("linux") and os.path.exists(target) else []


def app_file():
    """The one-file build itself (None when run from source)."""
    return sys.executable if getattr(sys, "frozen", False) else None


def uninstall():
    """Remove what installed() lists, as root; returns the paths removed."""
    paths = installed()
    if paths:
        quoted = " ".join(f"'{p}'" for p in paths)
        _as_root(f"rm -f {quoted} && {RELOAD}", f"removing {', '.join(paths)}")
    return paths
