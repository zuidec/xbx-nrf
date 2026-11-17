#!/usr/bin/env bash
# Install (or with -u remove) the dongle console logger as a system service.
# Needs root: sudo ./install.sh
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
bin=/usr/local/bin/xbx-dongle-log
unit=/etc/systemd/system/xbx-dongle-log.service

[ "$(id -u)" -eq 0 ] || { echo "run as root: sudo $0 $*" >&2; exit 1; }

if [ "${1:-}" = "-u" ]; then
	systemctl disable --now xbx-dongle-log.service 2>/dev/null || true
	rm -f "$bin" "$unit"
	systemctl daemon-reload
	echo "removed; logs kept in /var/log/xbx-dongle"
	exit 0
fi

install -m 755 "$here/xbx-dongle-log.py" "$bin"
install -m 644 "$here/xbx-dongle-log.service" "$unit"
systemctl daemon-reload
systemctl enable --now xbx-dongle-log.service
systemctl --no-pager status xbx-dongle-log.service | head -5
echo "logging to /var/log/xbx-dongle/"
