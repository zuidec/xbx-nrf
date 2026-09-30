#!/bin/bash
# Rumble the controller for 1 s through the dongle's HID output report.
# Usage: test-rumble.sh <hr|lr|all> <0-255>
#   hr = heavy motor, lr = light motor, all = both
set -e

usage() {
	echo "usage: $0 <hr|lr|all> <0-255>" >&2
	exit 1
}

[[ $# -eq 2 && $2 =~ ^[0-9]+$ && $2 -le 255 ]] || usage
level=$2
case $1 in
hr) heavy=$level light=0 ;;
lr) heavy=0 light=$level ;;
all) heavy=$level light=$level ;;
*) usage ;;
esac

uevent=$(grep -l "XBX-NRF" /sys/class/hidraw/*/device/uevent 2>/dev/null | head -1)
[[ -n $uevent ]] || { echo "XBX-NRF dongle not found" >&2; exit 1; }
dev=/dev/$(basename "$(dirname "$(dirname "$uevent")")")

# report 2: heavy, light, LT, RT, Guide LED
printf "\\x02\\x$(printf %02x "$heavy")\\x$(printf %02x "$light")\\x00\\x00\\x00" >"$dev"
sleep 1
printf '\x02\x00\x00\x00\x00\x00' >"$dev"
