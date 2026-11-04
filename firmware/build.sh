#!/usr/bin/env bash
# Firmware build, optionally flashed right after.
#
# Usage (from anywhere; starts the pinned SDK toolchain itself if needed):
#   ./build.sh <target> [-r] [-f <device>] [extra west build args]
#   ./build.sh <xbx-controller|xbx-dongle>-bootloader [-f swd [--hex <file>]]
#
# Targets: board, folder in the app (xbx-nrf/ or dongle/)
#   xbx-controller            xbx_controller/nrf52840/uf2      build/
#   xbx-controller-promicro   promicro_nrf52840/nrf52840/uf2   build-promicro/ (test)
#   xbx-dongle                xbx_dongle/nrf52840/uf2          build/
#   xbx-dongle-pca10059       nrf52840dongle/nrf52840          build-pca10059/
#   *-bootloader              bootloader/build.sh (UF2 bootloader, own options)
#   Old names still work: xbx-nrf, xbx-nrf-promicro, xbx-nrf-bootloader.
#
#   e.g. ./build.sh xbx-controller -p                # pristine rebuild
#        ./build.sh xbx-controller -f swd            # build + flash app over SWD
#        ./build.sh xbx-dongle -f /dev/sdX           # build + flash, UF2 drive
#        ./build.sh xbx-dongle-pca10059 -f /dev/ttyACM0
#        ./build.sh xbx-dongle -r                    # release build
#        ./build.sh xbx-dongle-bootloader -f swd     # its bootloader (erases all)
#        ./build.sh xbx-controller-bootloader -f swd --hex <file>  # downloaded
#
# -r / --release adds the app's release.conf (no USB console, no logs) and
# builds into the target's folder with -release added.
#
# -f / --flash <device>, by board:
#   PCA10059  serial port of its DFU bootloader (press RESET)
#   */uf2     UF2 drive: mount point or block device (/dev/sdX); "swd"
#             writes only the app (bootloader stays)
#
# BUILD_DIR=<dir> builds into <app>/<dir> instead (a second configuration,
# e.g. a fake-input controller, without overwriting the main build).

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# bootloader targets: before the toolchain start, which --hex doesn't need
case "${1:-}" in
xbx-controller-bootloader | xbx-nrf-bootloader)
	shift; exec "$script_dir/bootloader/build.sh" xbx_controller "$@" ;;
xbx-dongle-bootloader) shift; exec "$script_dir/bootloader/build.sh" xbx_dongle "$@" ;;
esac

# shellcheck source=check-sdk.sh
source "$script_dir/check-sdk.sh"
xbx_relaunch_in_toolchain "$script_dir" "$script_dir/$(basename "${BASH_SOURCE[0]}")" "$@"

usage() {
	echo "usage: $0 <xbx-controller|xbx-controller-promicro|xbx-dongle|xbx-dongle-pca10059> [-r] [-f <device>] [extra west build args]" >&2
	echo "       $0 <xbx-controller|xbx-dongle>-bootloader [-f swd [--hex <file>]]" >&2
	exit 2
}

[ $# -ge 1 ] || usage
target="$1"
shift

case "$target" in
xbx-controller | xbx-nrf)
	app=xbx-nrf; board="xbx_controller/nrf52840/uf2"; build_name=build ;;
xbx-controller-promicro | xbx-nrf-promicro)
	app=xbx-nrf; board="promicro_nrf52840/nrf52840/uf2"; build_name=build-promicro ;;
xbx-dongle)
	app=dongle; board="xbx_dongle/nrf52840/uf2"; build_name=build ;;
xbx-dongle-pca10059)
	app=dongle; board="nrf52840dongle/nrf52840"; build_name=build-pca10059 ;;
*) usage ;;
esac

# Take -r and -f / --flash out of the arguments (west gets the rest). The
# board comes with the target: one folder, one board.
flash_dev=""
release=false
cmake_sep=false
west_args=()
while [ $# -gt 0 ]; do
	case "$1" in
	--) west_args+=("$@"); cmake_sep=true; break ;; # the rest is for CMake
	-r | --release) release=true ;;
	-f | --flash) [ $# -ge 2 ] || usage; flash_dev="$2"; shift 2; continue ;;
	--flash=*) flash_dev="${1#--flash=}" ;;
	-b | --board | --board=*)
		echo "error: the target sets the board; see the targets in $0" >&2
		exit 2 ;;
	*) west_args+=("$1") ;;
	esac
	shift
done
app_dir="$script_dir/$app"
if $release; then
	build_dir="$app_dir/${BUILD_DIR:-$build_name-release}"
	$cmake_sep || west_args+=(--)
	west_args+=(-DEXTRA_CONF_FILE="$app_dir/release.conf")
else
	build_dir="$app_dir/${BUILD_DIR:-$build_name}"
fi

xbx_check_sdk "$script_dir" || exit 1

echo "app:   $app"
echo "board: $board"
echo "out:   $build_dir"
$release && echo "conf:  $app_dir/release.conf"

# BOARD_ROOT: our boards in firmware/boards/ (sysbuild ignores the app folder)
BOARD_ROOT="$script_dir" west build -b "$board" -d "$build_dir" "$app_dir" "${west_args[@]}"

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
	if [ "$flash_dev" = swd ]; then
		echo "flash: west flash (SWD, app only)"
		west flash -d "$build_dir" -r openocd
		echo "flash: done"
		exit 0
	fi
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
esac
echo "flash: done"
