#!/usr/bin/env bash
# Unsigned development build (no MCUboot) into <app>/build/.
#
# Usage (inside ncs-shell):
#   firmware/build-unsigned.sh <xbx-nrf|dongle> [extra west build args]
#   e.g. firmware/build-unsigned.sh xbx-nrf -p      # pristine rebuild
#
# Each app has a default board below. Pass -b <board> to override it.
# TODO: controller → custom board once it exists (see todo.md, Firmware).

set -euo pipefail

usage() {
	echo "usage: $0 <xbx-nrf|dongle> [extra west build args]" >&2
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

# an explicit -b / --board in the extra args wins over the default
for arg in "$@"; do
	case "$arg" in
	-b | --board | --board=*) board="" ;;
	esac
done

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app_dir="$script_dir/$app"

# shellcheck source=check-sdk.sh
source "$script_dir/check-sdk.sh"
xbx_check_sdk "$script_dir" || exit 1

echo "app:   $app"
echo "board: ${board:-(from arguments)}"
echo "out:   $app_dir/build"

west build ${board:+-b "$board"} -d "$app_dir/build" "$app_dir" "$@"

echo
echo "images:"
find "$app_dir/build" -maxdepth 3 -path "*/zephyr/zephyr.*" \( -name '*.uf2' -o -name '*.hex' \) \
	-printf '  %p\n' | sort
