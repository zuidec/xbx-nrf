#!/usr/bin/env bash
# Third-party notices for the release binaries, into <out dir>:
#   THIRD-PARTY-NOTICES-firmware.txt      both .uf2 files (same SDK parts)
#   THIRD-PARTY-NOTICES-configurator.txt  the Linux configurator
# Attach both to each release.
#
# Usage: tools/release-notices.sh <out dir>
# Needs: the pinned SDK and toolchain (firmware/ncs-version) under
# $NCS_ROOT (default ~/ncs), and a configurator --release build (its
# Debian 12 image and PyInstaller work folder).

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:?usage: $0 <out dir>}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

# shellcheck disable=SC1091
source "$repo/firmware/ncs-version"
ncs="${NCS_ROOT:-$HOME/ncs}/$NCS_VERSION"
sdk_licenses="${NCS_ROOT:-$HOME/ncs}/toolchains/$NCS_TOOLCHAIN_BUNDLE/opt/zephyr-sdk/gnu/arm-zephyr-eabi/share/licenses"
cfg="$repo/tools/configurator"
work="$cfg/build/release/pyinstaller/xbx-config"
image="xbx-config-build:debian12"

for f in "$ncs/zephyr/LICENSE" "$ncs/nrf/LICENSE" "$ncs/modules/hal/nordic/nrfx/nrfx.h" \
	"$sdk_licenses/picolibc/COPYING.picolibc" "$work/PKG-00.toc"; do
	[ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
done

section() {
	printf '\n\n%s\n%s\n\n' "$1" "$(printf '=%.0s' $(seq ${#1}))"
}

# --- firmware ---------------------------------------------------------------
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

# --- configurator -----------------------------------------------------------
# Every shared library PyInstaller bundled, mapped to its Debian package; the
# package's copyright file is its notice.
libs="$(grep -oE "\('[^'/]+\.so[^']*'" "$work/PKG-00.toc" | tr -d "('" | sort -u | tr '\n' ' ')"
engine="$(command -v podman || command -v docker)"
pyinst="$(echo "$cfg"/build/release/venv/lib/python3*/site-packages/pyinstaller-*.dist-info/licenses/COPYING.txt)"
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

ls -l "$out"/THIRD-PARTY-NOTICES-*.txt
