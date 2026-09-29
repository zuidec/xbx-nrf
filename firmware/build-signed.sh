#!/usr/bin/env bash
# Signed release build: MCUboot + application signed with the device's private key.
#
# Usage (inside ncs-shell):
#   firmware/build-signed.sh <xbx-nrf|dongle> <board> [extra west build args]
#   e.g. firmware/build-signed.sh dongle nrf52840dongle/nrf52840/bare
#
# Keys live outside the repo in $XBX_KEY_DIR (exported by ~/bin/ncs-shell):
#   controller-p256.pem  -> firmware/xbx-nrf
#   dongle-p256.pem      -> firmware/dongle
# See docs/signing.md.

set -euo pipefail

usage() {
	echo "usage: $0 <xbx-nrf|dongle> <board> [extra west build args]" >&2
	exit 2
}

[ $# -ge 2 ] || usage
app="$1"
board="$2"
shift 2

case "$app" in
xbx-nrf) key_name="controller-p256.pem" ;;
dongle)  key_name="dongle-p256.pem" ;;
*) usage ;;
esac

if [ -z "${XBX_KEY_DIR:-}" ]; then
	echo "error: XBX_KEY_DIR is not set; run this from ncs-shell" >&2
	exit 1
fi

key="$XBX_KEY_DIR/$key_name"
if [ ! -f "$key" ]; then
	echo "error: signing key not found: $key" >&2
	exit 1
fi
if ! grep -q "PRIVATE KEY" "$key"; then
	echo "error: $key does not look like a PEM private key" >&2
	exit 1
fi

if ! command -v west >/dev/null; then
	echo "error: west not found; run this from ncs-shell" >&2
	exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
app_dir="$script_dir/$app"
build_dir="$app_dir/build-signed"
export ZEPHYR_BASE="${ZEPHYR_BASE:-$HOME/ncs/v3.4.1/zephyr}"

echo "app:   $app"
echo "board: $board"
echo "key:   $key"
echo "out:   $build_dir"

west build --pristine -b "$board" -d "$build_dir" "$app_dir" "$@" -- \
	-DSB_CONF_FILE="$app_dir/sysbuild-signed.conf" \
	-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE="\"$key\""

echo
echo "signed images:"
find "$build_dir" -maxdepth 3 \( -name 'merged.hex' -o -name 'zephyr.signed.bin' \
	-o -name 'zephyr.signed.hex' -o -name 'dfu_application.zip' \) -printf '  %p\n' | sort
