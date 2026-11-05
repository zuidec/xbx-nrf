"""Third-party notices for a configurator build, from what PyInstaller bundles.
Called by xbx-config.spec after Analysis; the text goes into the build
(files/THIRD-PARTY-NOTICES.txt, shown in Help → About) and build.sh copies it
next to the binary.

  Linux --release  Debian 12 packages of the bundled libraries: their
                   copyright files (run inside the build container)
  Windows          python.org's Python: its LICENSE.txt plus the texts in
                   licenses/windows/ (the same set its own installer ships)
  other builds     a short note: only release builds carry notices
"""

import os
import re
import subprocess
import sys
from importlib import metadata

HERE = os.path.dirname(os.path.abspath(__file__))
WINDOWS_TEXTS = os.path.join(HERE, "licenses", "windows")

# Windows DLLs PyInstaller may bundle from python.org's Python, all covered by
# the texts below; any other DLL stops the build until it has a notice
WINDOWS_DLLS = re.compile(r"(python3\d*|vcruntime140(_1)?|libcrypto-3(-x64)?|libssl-3(-x64)?"
                          r"|libffi-\d+|tcl86t|tk86t)\.dll", re.IGNORECASE)
WINDOWS_TEXT_ORDER = [  # (section title, file in licenses/windows/)
    ("Python: incorporated software", "python-incorporated.txt"),
    ("Microsoft C runtime (VCRUNTIME140.dll)", "msvc-runtime.txt"),
    ("OpenSSL", "openssl.txt"),
    ("libffi", "libffi.txt"),
    ("Tcl", "tcl.txt"),
    ("Tk", "tk.txt"),
    ("bzip2", "bzip2.txt"),
    ("xz", "xz.txt"),
    ("zlib", "zlib.txt"),
]


def section(title):
    t = title.encode()
    return b"\n\n" + t + b"\n" + b"=" * len(title) + b"\n\n"


def read(path):
    with open(path, "rb") as f:
        return f.read()


def dist_file(name, pred):
    """Files of an installed package (PyInstaller, hidapi) matching pred."""
    d = metadata.distribution(name)
    found = [str(d.locate_file(f)) for f in d.files or [] if pred(f)]
    if not found:
        sys.exit(f"notices: no license file in the {name} package")
    return sorted(found)


def pyinstaller_copying():
    return dist_file("pyinstaller", lambda f: f.name == "COPYING.txt")[0]


def text(binaries):
    """The notices for this build: binaries is the spec's a.binaries."""
    if sys.platform.startswith("linux") and os.environ.get("XBX_RELEASE_BUILD"):
        return linux(binaries)
    if sys.platform == "win32":
        return windows(binaries)
    return (b"This is a local build of the xbx-nrf configurator. Third-party notices\n"
            b"come with the release builds: https://github.com/zuidec/xbx-nrf/releases\n")


def linux(binaries):
    """Debian packages of the bundled shared libraries; each package's
    copyright file is its notice."""
    libs = sorted({dest for dest, _src, _kind in binaries if "/" not in dest and ".so" in dest})
    pkgs = set()
    for lib in libs:
        r = subprocess.run(["dpkg", "-S", f"*/{lib}"], capture_output=True, text=True)
        if r.returncode == 0 and r.stdout:
            pkgs.add(r.stdout.splitlines()[0].split(":")[0])
    pkgs.add(f"libpython{sys.version_info.major}.{sys.version_info.minor}-stdlib")
    out = [b"Third-party software in the xbx-nrf configurator (Linux binary)\n"
           b"Built with PyInstaller on Debian 12; the bundled parts are Debian 12's.\n\n"
           b"Bundled Debian packages (each one's license: its copyright file below):\n"]
    copyrights = []
    for p in sorted(pkgs):
        version = subprocess.run(["dpkg-query", "-W", "-f=${Version}", p], check=True,
                                 capture_output=True).stdout
        out.append(b"  " + p.encode() + b" " + version + b"\n")
        copyrights.append(section(p) + read(f"/usr/share/doc/{p}/copyright"))
    out.append(b"  PyInstaller bootloader            GPL-2.0-or-later WITH Bootloader-exception\n")
    out += copyrights
    out += [section("PyInstaller"), read(pyinstaller_copying())]
    return b"".join(out)


def windows(binaries):
    """python.org's Python: what its Windows installer's LICENSE.txt holds,
    plus hidapi and the PyInstaller bootloader."""
    base = sys.base_prefix
    unknown = []
    for dest, src, _kind in binaries:
        name = os.path.basename(dest)
        if name.lower().endswith(".dll") and not WINDOWS_DLLS.fullmatch(name):
            unknown.append(name)
        elif name.lower().endswith(".pyd") and not name.lower().startswith("hid."):
            if not os.path.abspath(src).lower().startswith(base.lower()):
                unknown.append(name)  # an extension from some other package
    if unknown:
        sys.exit("notices: no license notice for bundled " + ", ".join(sorted(unknown)) +
                 ": add its text to tools/configurator/notices.py and licenses/windows/")
    ver = sys.version.split()[0]
    hidapi = metadata.version("hidapi")
    rows = [
        (f"Python {ver}, incorporated code", "PSF-2.0 and others, below"),
        ("Microsoft C runtime", "Microsoft Distributable Code terms"),
        ("OpenSSL 3", "Apache-2.0"),
        ("libffi", "MIT"),
        ("Tcl/Tk 8.6", "TCL"),
        ("bzip2, xz, zlib", "bzip2-1.0.6, public domain, Zlib"),
        (f"hidapi {hidapi}", "its license files, below"),
        ("PyInstaller bootloader", "GPL-2.0-or-later WITH Bootloader-exception"),
    ]
    out = [(f"Third-party software in the xbx-nrf configurator (Windows binary)\n"
            f"Built with PyInstaller and python.org's Python {ver} for Windows.\n\n" +
            "".join(f"  {c:<37}{lic}\n" for c, lic in rows)).encode()]
    out += [section(f"Python {ver}"), read(os.path.join(base, "LICENSE.txt"))]
    for title, name in WINDOWS_TEXT_ORDER:
        out += [section(title), read(os.path.join(WINDOWS_TEXTS, name))]
    out.append(section(f"hidapi {hidapi}"))
    for f in dist_file("hidapi", lambda f: "LICENSE" in f.name.upper()):
        out += [b"--- " + os.path.basename(f).encode() + b"\n\n", read(f), b"\n"]
    out += [section("PyInstaller"), read(pyinstaller_copying())]
    return b"".join(out).replace(b"\r\n", b"\n")  # Windows checkouts: CRLF
