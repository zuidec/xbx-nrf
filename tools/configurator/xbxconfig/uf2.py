"""UF2 files and the UF2 bootloader's drive (docs/configurator.md,
"Firmware update", "Image marker")."""

import os
import shutil
import struct
import subprocess
import sys
import threading
import time
from dataclasses import dataclass

from . import device as dev
from . import protocol as p

MAGIC0, MAGIC1, MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FLAG_FAMILY = 0x2000
FAMILY_NRF52840 = 0xADA52840
MARKER_MAGIC = b"XBXIMG\x01\x00"

DRIVES = {"controller": "XBXCTRL", "dongle": "XBXDONGLE"}
BOARD_IDS = {"controller": "nRF52840-xbx-controller", "dongle": "nRF52840-xbx-dongle"}

DRIVE_TIMEOUT = 20  # s: bootloader start + the desktop mounting it
MOUNT_TIMEOUT = 120  # s: the user mounting it by hand
COPY_TIMEOUT = 30  # s: a drive that stops responding holds the write in the kernel
RESTART_TIMEOUT = 20


class Uf2Error(Exception):
    pass


@dataclass
class Image:
    path: str
    data: bytes  # the raw file
    device: str  # from the marker
    version: tuple

    @property
    def version_str(self):
        return ".".join(map(str, self.version))


def load(path):
    """Read and check a .uf2: nRF52840 family, our image marker."""
    with open(path, "rb") as f:
        data = f.read()
    if not data or len(data) % 512:
        raise Uf2Error("not a UF2 file (size isn't a multiple of 512)")
    chunks = []
    for off in range(0, len(data), 512):
        b = data[off:off + 512]
        m0, m1, flags, addr, size, _, _, family = struct.unpack_from("<8I", b)
        if m0 != MAGIC0 or m1 != MAGIC1 or struct.unpack_from("<I", b, 508)[0] != MAGIC_END:
            raise Uf2Error(f"not a UF2 file (bad block at offset {off})")
        if not flags & FLAG_FAMILY or family != FAMILY_NRF52840:
            raise Uf2Error("not an nRF52840 (UF2 bootloader) image")
        chunks.append((addr, b[32:32 + size]))
    chunks.sort()
    payload = bytearray()
    base = chunks[0][0]
    for addr, chunk in chunks:
        payload[addr - base:addr - base + len(chunk)] = chunk
    i = payload.find(MARKER_MAGIC)
    if i < 0:
        raise Uf2Error("no xbx-nrf image marker: not our firmware, or built before the marker")
    m = payload[i + 8:i + 12]
    return Image(path, data, p.DEVICES.get(m[0], f"unknown ({m[0]})"), tuple(m[1:4]))


def _mounts_linux():
    mounts = {}
    with open("/proc/mounts") as f:
        for line in f:
            src, target = line.split()[:2]
            mounts[os.path.realpath(src)] = target.replace("\\040", " ")
    return mounts


def find_drive(kind):
    """Mount point of the bootloader drive; None if not there (yet).
    Raises Uf2Error if it's there but not mounted (Linux)."""
    label = DRIVES[kind]
    if sys.platform.startswith("linux"):
        node = f"/dev/disk/by-label/{label}"
        if not os.path.exists(node):
            return None
        return _mounts_linux().get(os.path.realpath(node), False) or None
    if sys.platform == "darwin":
        path = f"/Volumes/{label}"
        return path if os.path.isdir(path) else None
    if sys.platform == "win32":
        import ctypes  # noqa: PLC0415

        buf = ctypes.create_unicode_buffer(64)
        for letter in "DEFGHIJKLMNOPQRSTUVWXYZ":
            root = f"{letter}:\\"
            if ctypes.windll.kernel32.GetVolumeInformationW(root, buf, 64, None, None, None,
                                                            None, 0) and buf.value == label:
                return root
        return None
    raise Uf2Error(f"unsupported platform {sys.platform}")


def _udisks(*args, interactive=False):
    """Linux: udisksctl as the user; True if it worked. Without a desktop
    session polkit wants an admin password: interactive lets udisksctl
    ask for it in the terminal, otherwise it fails at once."""
    tool = shutil.which("udisksctl")
    if not tool:
        return False
    try:
        if interactive:
            r = subprocess.run([tool, *args], stdout=subprocess.DEVNULL, timeout=120)
        else:
            r = subprocess.run([tool, *args, "--no-user-interaction"], capture_output=True,
                               timeout=10)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return r.returncode == 0


def drive_present_unmounted(kind):
    return (sys.platform.startswith("linux")
            and os.path.exists(f"/dev/disk/by-label/{DRIVES[kind]}")
            and find_drive(kind) is None)


def check_drive(mount, kind):
    """The drive's INFO_UF2.TXT names our board."""
    try:
        with open(os.path.join(mount, "INFO_UF2.TXT")) as f:
            info = f.read()
    except OSError as e:
        raise Uf2Error(f"no INFO_UF2.TXT on {mount}: {e}") from e
    board = next((l.split(":", 1)[1].strip() for l in info.splitlines()
                  if l.startswith("Board-ID:")), "")
    if not board.startswith(BOARD_IDS[kind]):
        raise Uf2Error(f"the drive is a different board ({board or 'unknown'})")


def _write(target, data):
    with open(target, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())


def _copy(target, data, timeout):
    """Write and sync target; True when done, False after timeout. A drive
    that stops responding blocks write/fsync in the kernel (no signal gets
    through) until its error handling gives up, minutes later: POSIX copies
    in a child process, which is left to finish on its own, so this process
    can go on and exit. Raises OSError for a failed write."""
    if not hasattr(os, "fork"):  # Windows: a thread (its process can't exit early)
        result = []

        def run():
            try:
                _write(target, data)
                result.append(None)
            except OSError as e:
                result.append(e)

        t = threading.Thread(target=run, daemon=True)
        t.start()
        t.join(timeout)
        if not result:
            return False
        if result[0]:
            raise result[0]
        return True

    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:  # child: only the copy, then out without any cleanup
        code = 1
        try:
            os.close(r)
            _write(target, data)
            code = 0
        except BaseException as e:  # never back into the parent's code
            try:
                os.write(w, f"{getattr(e, 'errno', 0) or 0}\0{e}".encode())
            except BaseException:
                pass  # the parent gave up waiting: nobody to tell
        finally:
            os._exit(code)
    os.close(w)
    try:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            done, status = os.waitpid(pid, os.WNOHANG)
            if done:
                if os.waitstatus_to_exitcode(status) == 0:
                    return True
                errno_, _, msg = os.read(r, 4096).decode(errors="replace").partition("\0")
                raise OSError(int(errno_ or 0) or None, msg or "copy failed")
            time.sleep(0.1)
        return False
    finally:
        os.close(r)


def update(device, image, progress=print, interactive=False):
    """BOOTLOADER, copy the image, wait for the app again. device is closed
    on return; returns the new Info. interactive (a terminal): an admin
    password prompt is allowed for mounting the drive."""
    kind = device.info.device
    serial = device.info.serial
    if image.device != kind:
        raise Uf2Error(f"this file is {image.device} firmware; the device is a {kind}")

    progress("Restarting into the bootloader...")
    device.bootloader()
    device.close()

    progress(f"Waiting for the {DRIVES[kind]} drive...")

    label = DRIVES[kind]
    node = f"/dev/disk/by-label/{label}"
    tried = False
    mounted_here = False
    end = time.monotonic() + DRIVE_TIMEOUT
    while True:
        mount = find_drive(kind)
        if mount:
            break
        if not tried and drive_present_unmounted(kind):
            tried = True
            if interactive:
                progress(f"Mounting {label} (may ask for an admin password)...")
            if _udisks("mount", "-b", node, interactive=interactive):
                mounted_here = True
                continue
            progress(f"{label} is there but couldn't be mounted (no access). Mount it, "
                     f"e.g. in another terminal: sudo mount {node} /mnt")
            end = time.monotonic() + MOUNT_TIMEOUT
        if time.monotonic() >= end:
            raise Uf2Error(f"timed out waiting for the {label} drive; the {kind} stays in "
                           f"its bootloader: copy the file onto {label} to finish")
        time.sleep(0.25)
    check_drive(mount, kind)
    if not os.access(mount, os.W_OK):
        raise Uf2Error(f"no write access to {mount} (mounted by another user?); the {kind} "
                       f"stays in its bootloader: finish with "
                       f"sudo cp '{image.path}' '{mount}/' && sync")

    progress(f"Copying {os.path.basename(image.path)} ({len(image.data) // 1024} KiB)...")
    target = os.path.join(mount, "firmware.uf2")
    try:
        done = _copy(target, image.data, COPY_TIMEOUT)
    except OSError:
        # the bootloader restarts after the last block: the drive may vanish first
        if find_drive(kind):
            raise
    else:
        if not done:
            progress(f"{label} stopped responding during the copy; checking whether the "
                     f"{kind} restarted anyway...")
        elif mounted_here:
            # every block is on the device. Unmount (never power off) what we
            # mounted, so the desktop doesn't warn when it goes; it may be
            # gone already.
            _udisks("unmount", "-b", node)

    progress("Waiting for the restart...")

    info = dev.wait_back(kind, serial, RESTART_TIMEOUT)
    if info is None:
        raise Uf2Error(f"timed out waiting for the {kind} to come back")
    if info.firmware != image.version:
        raise Uf2Error(f"the {kind} came back with firmware {info.firmware_str}, not "
                       f"{image.version_str}: the copy didn't take")
    progress(f"Done: firmware {info.firmware_str}")
    return info
