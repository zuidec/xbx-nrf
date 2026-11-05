#!/usr/bin/env bash
# Third-party notices for the firmware release binaries, into <out dir>:
#   THIRD-PARTY-NOTICES-firmware.txt  both .uf2 files (same SDK parts)
# Attach it to each firmware release. The configurator's notices come from
# its own build (tools/configurator/build.sh, notices.py).
#
# Usage: tools/release-notices.sh <out dir>
# Needs: the pinned SDK and toolchain (firmware/ncs-version) under
# $NCS_ROOT (default ~/ncs).

set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="${1:?usage: $0 <out dir>}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"

section() {
	printf '\n\n%s\n%s\n\n' "$1" "$(printf '=%.0s' $(seq ${#1}))"
}
need() {
	for f; do
		[ -e "$f" ] || { echo "missing: $f" >&2; exit 1; }
	done
}

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

firmware_notices
ls -l "$out/THIRD-PARTY-NOTICES-firmware.txt"
