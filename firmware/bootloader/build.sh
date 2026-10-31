#!/usr/bin/env bash
# UF2 bootloader build: the Adafruit nRF52 Bootloader at a pinned tag (fetched
# into src/) plus our board folders (boards/<board>/), output in
# build/<board>/.
#
# Usage: ../build.sh <xbx-nrf|xbx-dongle>-bootloader [-f swd], or from
# anywhere: ./build.sh <board> [-f swd]
#   board: a folder in boards/ (xbx_controller, xbx_dongle)
#   -f swd: full erase, then flash the bootloader over SWD (Pi Debug Probe).
#           Wipes the app and its settings.
#
# Needs the Arm GNU Toolchain (distro newlib builds may carry a semihosting
# crt0 that faults without a debugger): ARM_GCC_DIR, default below. Runs in
# the pinned nRF Connect toolchain for its Python (intelhex).

set -euo pipefail

TAG=0.11.0
REPO=https://github.com/adafruit/Adafruit_nRF52_Bootloader.git
ARM_GCC_URL=https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
ARM_GCC_DIR="${ARM_GCC_DIR:-$HOME/.local/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi}"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fw_dir="$(dirname "$script_dir")"

# shellcheck source=../check-sdk.sh
source "$fw_dir/check-sdk.sh"
xbx_relaunch_in_toolchain "$fw_dir" "$script_dir/$(basename "${BASH_SOURCE[0]}")" "$@"

usage() {
	echo "usage: $0 <$(ls "$script_dir/boards" | paste -sd '|')> [-f swd]" >&2
	exit 2
}

[ $# -ge 1 ] || usage
board="$1"
shift
[ -d "$script_dir/boards/$board" ] || usage
flash=false
case "${1:-}" in
"") ;;
-f) [ "${2:-}" = swd ] || usage; flash=true ;;
*) usage ;;
esac

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

name="${board}_bootloader-$TAG"
hex="${name}_s140_6.1.1.hex" # bootloader + SoftDevice area (MBR) + UICR
uf2="update-${name}_nosd.uf2" # bootloader update from a running bootloader
make -C "$src" BOARD="$board" PYTHON=python3 CROSS_COMPILE="$ARM_GCC_DIR/bin/arm-none-eabi-" \
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
echo
echo "flash: full erase + bootloader over SWD"
openocd -f interface/cmsis-dap.cfg -c 'transport select swd' -f target/nrf52.cfg \
	-c init -c 'reset halt' -c 'nrf5 mass_erase' \
	-c "program $out/$hex verify reset exit"
