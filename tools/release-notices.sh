#!/usr/bin/env bash
# Third-party notices for the release binaries, into <out dir>:
#   THIRD-PARTY-NOTICES-firmware.txt              both .uf2 files (same SDK parts)
#   THIRD-PARTY-NOTICES-configurator.txt          the Linux configurator
#   THIRD-PARTY-NOTICES-configurator-windows.txt  the Windows configurator
# Attach each with its binaries.
#
# Usage: tools/release-notices.sh <out dir> [firmware] [configurator]
#   No part named: both. configurator: for this OS's build, run after it.
# Needs:
#   firmware      the pinned SDK and toolchain (firmware/ncs-version) under
#                 $NCS_ROOT (default ~/ncs)
#   configurator  Linux: a --release build (its Debian 12 image and
#                 PyInstaller work folder). Windows (Git Bash): a build.sh
#                 build (its venv, and the Python that made it).

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
usage() { echo "usage: $0 <out dir> [firmware] [configurator]" >&2; exit 2; }
[ $# -ge 1 ] || usage
out="$1"
shift
parts=("$@")
[ ${#parts[@]} -gt 0 ] || parts=(firmware configurator)
for p in "${parts[@]}"; do
	case "$p" in
	firmware | configurator) ;;
	*) usage ;;
	esac
done
mkdir -p "$out"
out="$(cd "$out" && pwd)"
cfg="$repo/tools/configurator"

os="$(uname -s | tr '[:upper:]' '[:lower:]')"
case "$os" in
mingw* | msys* | cygwin*) os=windows ;;
darwin) os=macos ;;
esac

section() {
	printf '\n\n%s\n%s\n\n' "$1" "$(printf '=%.0s' $(seq ${#1}))"
}
need() {
	for f; do
		[ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
	done
}

# --- firmware ---------------------------------------------------------------
firmware_notices() {
	# shellcheck disable=SC1091
	source "$repo/firmware/ncs-version"
	local ncs="${NCS_ROOT:-$HOME/ncs}/$NCS_VERSION"
	local sdk_licenses="${NCS_ROOT:-$HOME/ncs}/toolchains/$NCS_TOOLCHAIN_BUNDLE/opt/zephyr-sdk/gnu/arm-zephyr-eabi/share/licenses"
	need "$ncs/zephyr/LICENSE" "$ncs/nrf/LICENSE" "$ncs/modules/hal/nordic/nrfx/nrfx.h" \
		"$sdk_licenses/picolibc/COPYING.picolibc"
	{
		cat <<EOF
Third-party software in the xbx-nrf firmware (controller and dongle .uf2)
Built with nRF Connect SDK $NCS_VERSION and the Zephyr SDK toolchain.

Component                          License
Zephyr RTOS, CMSIS                 Apache-2.0
nrfx (Nordic drivers)              BSD-3-Clause
nRF Connect SDK (ESB radio),       LicenseRef-Nordic-5-Clause
  nrfxlib (MPSL, CC310 platform)
picolibc (C library)               BSD-style, see below

Sources: https://github.com/nrfconnect/sdk-nrf (tag $NCS_VERSION) and the
repositories in its west.yml.
EOF
		section "Apache-2.0 (Zephyr, CMSIS)"
		cat "$ncs/zephyr/LICENSE"
		section "BSD-3-Clause (nrfx)"
		sed -n '2,31{s/^ \* \{0,1\}//;p}' "$ncs/modules/hal/nordic/nrfx/nrfx.h"
		section "LicenseRef-Nordic-5-Clause (nRF Connect SDK, nrfxlib)"
		sed -n '/<text>/,/<\/text>/{/<\/\{0,1\}text>/d;p}' "$ncs/nrf/LICENSE"
		section "picolibc"
		cat "$sdk_licenses/picolibc/COPYING.picolibc"
	} >"$out/THIRD-PARTY-NOTICES-firmware.txt"
}

# --- configurator, Linux ----------------------------------------------------
# Every shared library PyInstaller bundled, mapped to its Debian package; the
# package's copyright file is its notice.
configurator_linux() {
	local work="$cfg/build/release/pyinstaller/xbx-config"
	local image="xbx-config-build:debian12"
	need "$work/PKG-00.toc"
	local libs engine pyinst
	libs="$(grep -oE "\('[^'/]+\.so[^']*'" "$work/PKG-00.toc" | tr -d "('" | sort -u | tr '\n' ' ')"
	engine="$(command -v podman || command -v docker)"
	pyinst="$(echo "$cfg"/build/release/venv/lib/python3*/site-packages/pyinstaller-*.dist-info/licenses/COPYING.txt)"
	need "$pyinst"
	{
		cat <<EOF
Third-party software in the xbx-nrf configurator (Linux binary)
Built with PyInstaller on Debian 12; the bundled parts are Debian 12's.

Bundled Debian packages (each one's license: its copyright file below):
EOF
		"$engine" run --rm "$image" sh -c '
			for l in '"$libs"'; do
				dpkg -S "*/$l" 2>/dev/null | head -n1 | cut -d: -f1
			done | { cat; echo libpython3.11-stdlib; } | sort -u | while read -r p; do
				printf "%s %s\n" "$p" "$(dpkg-query -W -f="\${Version}" "$p")" >&2
				printf "\n\n%s\n" "$p"; printf "%s" "$p" | sed "s/./=/g"; printf "\n\n"
				cat "/usr/share/doc/$p/copyright"
			done' 2>"$out/.packages" >"$out/.copyrights"
		sed 's/^/  /' "$out/.packages"
		echo "  PyInstaller bootloader            GPL-2.0-or-later WITH Bootloader-exception"
		cat "$out/.copyrights"
		section "PyInstaller"
		cat "$pyinst"
	} >"$out/THIRD-PARTY-NOTICES-configurator.txt"
	rm -f "$out/.packages" "$out/.copyrights"
}

# --- configurator, Windows --------------------------------------------------
# python.org's Python: its LICENSE.txt covers the libraries its Windows build
# ships (OpenSSL, libffi, zlib, ...), Tcl/Tk (no separate license.terms) and
# usually the MSVC runtime. Plus hidapi and the PyInstaller bootloader.
configurator_windows() {
	local venv="$cfg/build/release/venv"
	local site="$venv/Lib/site-packages"
	local vpy="$venv/Scripts/python.exe"
	need "$vpy"
	vpy_print() { "$vpy" -c "$1" | tr -d '\r'; }
	local base pyver hidver
	# the Python the venv, and so the .exe, was made from
	base="$(cygpath -u "$(vpy_print 'import sys; print(sys.base_prefix)')")"
	pyver="$(vpy_print 'import platform; print(platform.python_version())')"
	hidver="$(vpy_print 'from importlib.metadata import version; print(version("hidapi"))')"
	shopt -s nullglob
	local tcl=("$base"/tcl/tcl8*/license.terms)
	local tk=("$base"/tcl/tk8*/license.terms)
	local pyinst=("$site"/pyinstaller-*.dist-info/licenses/COPYING.txt)
	local hidlic=("$site"/hidapi-*.dist-info/licenses/* "$site"/hidapi-*.dist-info/LICENSE*)
	shopt -u nullglob
	need "$base/LICENSE.txt" \
		"${pyinst[0]:-$site/pyinstaller-*.dist-info/licenses/COPYING.txt}" \
		"${hidlic[0]:-$site/hidapi-*.dist-info/LICENSE*}"
	grep -qi openssl "$base/LICENSE.txt" ||
		{ echo "error: no OpenSSL in $base/LICENSE.txt" >&2; exit 1; }
	# Tcl/Tk: their own license.terms if this Python has them, else in LICENSE.txt
	local tcltk="its own license.terms, below"
	if [ ${#tcl[@]} -eq 0 ] || [ ${#tk[@]} -eq 0 ]; then
		grep -q "Sun Microsystems, Inc., Scriptics Corporation" "$base/LICENSE.txt" ||
			{ echo "error: no Tcl/Tk license in $base/LICENSE.txt or tcl/*/license.terms" >&2; exit 1; }
		tcltk="in Python's section"
		tcl=() tk=()
	fi
	local crt="redistributed under the Microsoft Visual C++ Redistributable license"
	grep -q "Microsoft Distributable Code" "$base/LICENSE.txt" &&
		crt="Python's section includes its terms"
	{
		cat <<EOF
Third-party software in the xbx-nrf configurator (Windows binary)
Built with PyInstaller and python.org's Python $pyver for Windows.

- Python $pyver: PSF-2.0. Its Windows build bundles libraries (OpenSSL,
  libffi, zlib, bzip2, xz, expat, ...) whose licenses follow Python's in
  the same section below.
- Tcl/Tk 8.6: Tcl/Tk license, $tcltk.
- hidapi $hidver (Python module and HIDAPI library): its license files,
  below.
- PyInstaller bootloader: GPL-2.0-or-later WITH Bootloader-exception, below.
- Microsoft Visual C++ runtime (VCRUNTIME140.dll): $crt.
EOF
		section "Python $pyver and its bundled libraries"
		cat "$base/LICENSE.txt"
		if [ ${#tcl[@]} -gt 0 ]; then
			section "Tcl"
			cat "${tcl[0]}"
			section "Tk"
			cat "${tk[0]}"
		fi
		section "hidapi $hidver"
		local f
		for f in "${hidlic[@]}"; do
			printf -- '--- %s\n\n' "$(basename "$f")"
			cat "$f"
			echo
		done
		section "PyInstaller"
		cat "${pyinst[0]}"
	} >"$out/THIRD-PARTY-NOTICES-configurator-windows.txt"
}

for p in "${parts[@]}"; do
	case "$p" in
	firmware) firmware_notices ;;
	configurator)
		case "$os" in
		linux) configurator_linux ;;
		windows) configurator_windows ;;
		*) echo "error: no configurator notices for $os yet" >&2; exit 1 ;;
		esac
		;;
	esac
done

ls -l "$out"/THIRD-PARTY-NOTICES-*.txt
