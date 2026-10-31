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


def install_udev_rule():
    """Copy the rule into /etc/udev/rules.d and reload udev, as root via
    pkexec (graphical prompt) or sudo (terminal). Replug the device after."""
    tool = shutil.which("pkexec") or shutil.which("sudo")
    if not tool:
        raise OSError("neither pkexec nor sudo found: install the rule by hand: "
                      f"{path(UDEV_RULE)} -> {UDEV_DIR}/")
    # PyInstaller's unpack dir may not be readable by root's helper: copy out first
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, UDEV_RULE)
        shutil.copyfile(path(UDEV_RULE), src)
        os.chmod(tmp, 0o755)
        os.chmod(src, 0o644)
        script = (f"install -m 644 '{src}' '{UDEV_DIR}/{UDEV_RULE}' && "
                  "udevadm control --reload-rules && udevadm trigger --subsystem-match=hidraw")
        r = subprocess.run([tool, "sh", "-c", script], capture_output=True, text=True)
    if r.returncode:
        raise OSError(f"installing the udev rule failed: {r.stderr.strip() or r.returncode}")
