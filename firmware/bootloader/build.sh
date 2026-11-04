#!/usr/bin/env bash
# UF2 bootloader build: the Adafruit nRF52 Bootloader at a pinned tag (fetched
# into src/) plus our board folders (boards/<board>/), output in
# build/<board>/. Version from VERSION (<upstream>-xbx.<revision>): in the
# file names and the drive's INFO_UF2.TXT.
#
# Usage: ../build.sh <xbx-controller|xbx-dongle>-bootloader [-f swd [--hex <file>]],
# or from anywhere: ./build.sh <board> [-f swd [--hex <file>]]
#   board: a folder in boards/ (xbx_controller, xbx_dongle)
#   -f swd: full erase, then flash the bootloader over SWD (Pi Debug Probe).
#           Wipes the app and its settings.
#   --hex <file>: flash this .hex (e.g. from a release) instead of building;
#           needs only OpenOCD. The name must be that board's
#           (<board>_bootloader-*.hex).
#
# Needs the Arm GNU Toolchain (distro newlib builds may carry a semihosting
# crt0 that faults without a debugger): ARM_GCC_DIR, default below. Runs in
# the pinned nRF Connect toolchain for its Python (intelhex).

set -euo pipefail

REPO=https://github.com/adafruit/Adafruit_nRF52_Bootloader.git
ARM_GCC_URL=https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
ARM_GCC_DIR="${ARM_GCC_DIR:-$HOME/.local/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi}"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fw_dir="$(dirname "$script_dir")"

version_field() { sed -n "s/^$1 *= *//p" "$script_dir/VERSION"; }
TAG="$(version_field UPSTREAM)"
VERSION="$TAG-xbx.$(version_field REVISION)"

usage() {
	echo "usage: $0 <$(ls "$script_dir/boards" | paste -sd '|')> [-f swd [--hex <file>]]" >&2
	exit 2
}

[ $# -ge 1 ] || usage
args=("$@") # for the toolchain restart
board="$1"
shift
[ -d "$script_dir/boards/$board" ] || usage
flash=false
hex_file=""
while [ $# -gt 0 ]; do
	case "$1" in
	-f) [ "${2:-}" = swd ] || usage; flash=true; shift 2 ;;
	--hex) [ -n "${2:-}" ] || usage; hex_file="$2"; shift 2 ;;
	*) usage ;;
	esac
done
[ -z "$hex_file" ] || $flash || { echo "error: --hex needs -f swd" >&2; exit 2; }

flash_swd() {
	command -v openocd >/dev/null || { echo "error: openocd not found" >&2; exit 1; }
	echo
	echo "flash: full erase + bootloader over SWD"
	openocd -f interface/cmsis-dap.cfg -c 'transport select swd' -f target/nrf52.cfg \
		-c init -c 'reset halt' -c 'nrf5 mass_erase' \
		-c "program $1 verify reset exit"
}

# a downloaded image: no toolchain, no build
if [ -n "$hex_file" ]; then
	[ -f "$hex_file" ] || { echo "error: no such file: $hex_file" >&2; exit 1; }
	case "$(basename "$hex_file")" in
	"${board}"_bootloader-*.hex) ;;
	*)
		echo "error: $hex_file isn't a $board bootloader (${board}_bootloader-*.hex)" >&2
		exit 1
		;;
	esac
	flash_swd "$(cd "$(dirname "$hex_file")" && pwd)/$(basename "$hex_file")"
	exit 0
fi

# shellcheck source=../check-sdk.sh
source "$fw_dir/check-sdk.sh"
xbx_relaunch_in_toolchain "$fw_dir" "$script_dir/$(basename "${BASH_SOURCE[0]}")" "${args[@]}"

if [ ! -x "$ARM_GCC_DIR/bin/arm-none-eabi-gcc" ]; then
	echo "error: no Arm GNU Toolchain in $ARM_GCC_DIR (set ARM_GCC_DIR);" >&2
	echo "  download arm-none-eabi for x86_64 Linux: $ARM_GCC_URL" >&2
	exit 1
fi

src="$script_dir/src"
if [ ! -d "$src/.git" ]; then
	git clone --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules \
		"$REPO" "$src"
fi
have="$(git -C "$src" describe --tags --always)"
if [ "$have" != "$TAG" ]; then
	echo "error: $src is at $have, not $TAG; delete it to fetch again" >&2
	exit 1
fi

rm -rf "$src/src/boards/$board"
cp -r "$script_dir/boards/$board" "$src/src/boards/"
rm -rf "$src/_build/build-$board" # the Makefile doesn't track a changed version

name="${board}_bootloader-$VERSION"
hex="${name}_s140_6.1.1.hex" # bootloader + SoftDevice area (MBR) + UICR
uf2="update-${name}_nosd.uf2" # bootloader update from a running bootloader
# GIT_VERSION: upstream takes it from git describe (its own tag, "-dirty"
# with our board files copied in); ours names the revision
make -C "$src" BOARD="$board" PYTHON=python3 CROSS_COMPILE="$ARM_GCC_DIR/bin/arm-none-eabi-" \
	GIT_VERSION="$VERSION" \
	-j"$(nproc)" \
	"_build/build-$board/$hex" "_build/build-$board/$uf2"

out="$script_dir/build/$board"
mkdir -p "$out"
cp "$src/_build/build-$board/$hex" "$src/_build/build-$board/$uf2" "$out/"
echo
echo "images:"
echo "  $out/$hex"
echo "  $out/$uf2"

$flash || exit 0
flash_swd "$out/$hex"
