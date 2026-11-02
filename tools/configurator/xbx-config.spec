# PyInstaller spec for build.sh: one file, named by build.sh (XBX_BUILD_NAME).
# Linux: the font libraries stay out, so the system's fontconfig reads its own
# config (a bundled older one warns about newer config files); AppImages do
# the same. Every desktop has them.

import os
import sys

from PyInstaller.utils.hooks import collect_submodules

here = SPECPATH
name = os.environ["XBX_BUILD_NAME"]

# fontconfig and the libraries only freetype loads; expat and zlib stay in
# (Python needs them, and the system's fontconfig works with them)
SYSTEM_LIBS = ("libfontconfig.so", "libfreetype.so", "libpng16.so", "libbrotlicommon.so",
               "libbrotlidec.so")

a = Analysis(
    [os.path.join(here, "xbx-config.py")],
    pathex=[here],
    datas=[(os.path.join(here, "files"), "files")],  # installed as is (udev rule, SDL mapping)
    hiddenimports=collect_submodules("xbxconfig"),  # imported lazily (cli / gui)
)
if sys.platform == "linux":
    a.binaries = [b for b in a.binaries if not os.path.basename(b[0]).startswith(SYSTEM_LIBS)]

pyz = PYZ(a.pure)
exe = EXE(pyz, a.scripts, a.binaries, a.datas, [], name=name, console=True, upx=False)
