#!/usr/bin/env bash
# Unsigned development build (no MCUboot) into <app>/build/, optionally
# flashed right after.
#
# Usage (from anywhere; starts the pinned SDK toolchain itself if needed):
#   ./build-unsigned.sh <xbx-nrf|dongle> [-f <device>] [extra west build args]
#   e.g. ./build-unsigned.sh xbx-nrf -p                       # pristine rebuild
#        ./build-unsigned.sh dongle -f /dev/ttyACM0           # build + flash
#
# -f / --flash <device>, by board:
#   nrf52840dongle/nrf52840  serial port of the DFU bootloader (press RESET)
#   */uf2 (Pro Micro)        UF2 drive: mount point or block device (/dev/sdX)
#   anything else            "swd": west flash with the board's runner
#
# Each app has a default board below. Pass -b <board> to override it.
# BUILD_DIR=<dir> builds into <app>/<dir> instead (a second configuration,
# e.g. a fake-input controller, without overwriting the main build).
# TODO: controller → custom board once it exists (see todo.md, Firmware).

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=check-sdk.sh
source "$script_dir/check-sdk.sh"
xbx_relaunch_in_toolchain "$script_dir" "$script_dir/$(basename "${BASH_SOURCE[0]}")" "$@"

usage() {
	echo "usage: $0 <xbx-nrf|dongle> [-f <device>] [extra west build args]" >&2
	exit 2
}

[ $# -ge 1 ] || usage
app="$1"
shift

case "$app" in
xbx-nrf) board="promicro_nrf52840/nrf52840/uf2" ;; # prototype; later: custom bottom board
dongle)  board="nrf52840dongle/nrf52840" ;; # PCA10059; Pro Micro: -b promicro_nrf52840/nrf52840/uf2
*) usage ;;
esac

# Take -f / --flash out of the arguments (west gets the rest). An explicit
# -b / --board wins over the default; remember it for flashing.
flash_dev=""
explicit_board=false
west_args=()
while [ $# -gt 0 ]; do
	case "$1" in
	--) west_args+=("$@"); break ;; # the rest is for CMake
	-f | --flash) [ $# -ge 2 ] || usage; flash_dev="$2"; shift 2; continue ;;
	--flash=*) flash_dev="${1#--flash=}" ;;
	-b | --board)
		[ $# -ge 2 ] || usage
		board="$2"; explicit_board=true; west_args+=("$1" "$2"); shift 2; continue ;;
	--board=*) board="${1#--board=}"; explicit_board=true; west_args+=("$1") ;;
	*) west_args+=("$1") ;;
	esac
	shift
done
app_dir="$script_dir/$app"
build_dir="$app_dir/${BUILD_DIR:-build}"

xbx_check_sdk "$script_dir" || exit 1

echo "app:   $app"
echo "board: $board"
echo "out:   $build_dir"

# BOARD_ROOT: our boards in firmware/boards/ (sysbuild ignores the app folder)
if $explicit_board; then
	BOARD_ROOT="$script_dir" west build -d "$build_dir" "$app_dir" "${west_args[@]}"
else
	BOARD_ROOT="$script_dir" west build -b "$board" -d "$build_dir" "$app_dir" "${west_args[@]}"
fi

echo
echo "images:"
find "$build_dir" -maxdepth 3 -path "*/zephyr/zephyr.*" \( -name '*.uf2' -o -name '*.hex' \) \
	-printf '  %p\n' | sort

[ -n "$flash_dev" ] || exit 0

image_dir="$build_dir/$app/zephyr"
echo
case "$board" in
nrf52840dongle/nrf52840)
	if ! nrfutil nrf5sdk-tools --help >/dev/null 2>&1; then
		echo "error: nrfutil nrf5sdk-tools missing; install it with:" >&2
		echo "  nrfutil install nrf5sdk-tools" >&2
		exit 1
	fi
	echo "flash: DFU over $flash_dev"
	nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req=0x00 \
		--application "$image_dir/zephyr.hex" --application-version 1 "$build_dir/dfu.zip"
	nrfutil nrf5sdk-tools dfu usb-serial -pkg "$build_dir/dfu.zip" -p "$flash_dev"
	;;
*/uf2)
	drive="$flash_dev"
	if [ -b "$flash_dev" ]; then
		drive=$(findmnt -n -o TARGET --source "$flash_dev" || true)
		if [ -z "$drive" ]; then
			udisksctl mount -b "$flash_dev" >/dev/null
			drive=$(findmnt -n -o TARGET --source "$flash_dev")
		fi
	fi
	if [ ! -f "$drive/INFO_UF2.TXT" ]; then
		echo "error: $drive is not a UF2 bootloader drive (double-tap RST first)" >&2
		exit 1
	fi
	echo "flash: UF2 to $drive"
	cp "$image_dir/zephyr.uf2" "$drive/"
	sync
	;;
*)
	if [ "$flash_dev" != "swd" ]; then
		echo "error: $board flashes over SWD; use -f swd" >&2
		exit 1
	fi
	echo "flash: west flash (SWD)"
	west flash -d "$build_dir"
	;;
esac
echo "flash: done"
