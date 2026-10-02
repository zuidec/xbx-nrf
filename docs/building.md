---
title: Building the firmware
created: 2026-09-29
updated: 2026-09-29
tags:
  - xbx-nrf
  - firmware
  - howto
aliases:
  - building
  - build
  - versions
---

# Building the firmware

Related: [[docs/flashing|Flashing & debugging]],
[[docs/signing|Firmware signing]], [[todo#5. Firmware]].

## Apps

| App | Folder | Default board | Also supported |
|---|---|---|---|
| Controller | `firmware/xbx-nrf` | `promicro_nrf52840/nrf52840/uf2` | Later: custom board definition |
| Dongle | `firmware/dongle` | `nrf52840dongle/nrf52840` (PCA10059, off the shelf) | `xbx_dongle/nrf52840` (custom board), `promicro_nrf52840/nrf52840/uf2` |

Standalone Zephyr apps, outside the SDK folder. Shared code:
`firmware/common/include/` (e.g. `protocol.h`). Our own board definitions:
`firmware/boards/`; both build scripts pass it as `BOARD_ROOT` (sysbuild
doesn't search the app folder), so plain `west build` needs
`BOARD_ROOT=firmware/boards` for them.

## Pinned versions

| Component | Version | Pinned where |
|---|---|---|
| nRF Connect SDK | **v3.4.1** (LTS) | `firmware/ncs-version` |
| Toolchain bundle | **8285d8ad56** | `firmware/ncs-version` |
| Zephyr | 4.4.2 | part of the SDK |
| nrfutil / sdk-manager | 8.2.1 / 1.16.1 | not pinned |
| KiCad | 10.0.3 | not pinned |

`firmware/check-sdk.sh` (run by both build scripts) stops the build if
`$ZEPHYR_BASE/../nrf/VERSION` or the toolchain behind `west` doesn't match
`firmware/ncs-version`, and prints the fix.

## Setup

1. Install `nrfutil`, then the SDK and toolchain (into `~/ncs/`):
   ```sh
   nrfutil install sdk-manager
   nrfutil sdk-manager install --ncs-version v3.4.1
   ```
2. Create `~/bin/ncs-shell` (not in the repo):
   ```bash
   #!/bin/bash
   export XBX_KEY_DIR="$HOME/.config/xbx-nrf/keys"   # docs/signing.md
   export ZEPHYR_BASE="$HOME/ncs/v3.4.1/zephyr"
   nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 --shell
   ```
   `ZEPHYR_BASE` lets `west build` work from our app folders; without it west
   reports "unknown command build".
3. USB permissions: see [[docs/flashing]].

## Building

Development builds: unsigned, no bootloader, into `<app>/build/`.

```sh
ncs-shell
cd ~/Projects/xbx-nrf
firmware/build-unsigned.sh xbx-nrf       # incremental
firmware/build-unsigned.sh dongle -p     # pristine
firmware/build-unsigned.sh dongle -p -b nrf52840dongle/nrf52840
```

- Extra arguments go to `west build`; `-b` overrides the default board.
- Default boards live in `build-unsigned.sh`; update them when the real hardware
  exists ([[todo#Link]]).
- Plain `west build` also works but skips the version check, and needs `-b`
  again after `-p`.

Signed builds: [[docs/signing]].

### Output files

Sysbuild puts each image in a subfolder named after the app:

| File | Use |
|---|---|
| `<app>/build/<app>/zephyr/zephyr.uf2` | UF2 bootloader (Pro Micro) |
| `<app>/build/<app>/zephyr/zephyr.hex` | SWD flashing |
| `<app>/build/<app>/zephyr/zephyr.elf` | Debugging |

`<app>/build/zephyr/` is sysbuild's own and has no firmware.

### Board overlays

Name them after the **full** board target, `/` → `_`:

| Board target | Overlay file |
|---|---|
| `promicro_nrf52840/nrf52840/uf2` | `promicro_nrf52840_nrf52840_uf2.overlay` |
| `nrf52840dongle/nrf52840` | `nrf52840dongle_nrf52840.overlay` |
| `xbx_dongle/nrf52840` | `xbx_dongle_nrf52840.overlay` |

Shorter names are **silently ignored**; look for `-- Found devicetree overlay:`
in the build log.

### Default board: why it's in the script

Sysbuild resolves the board before reading the app, so `set(BOARD …)` in an
app's `CMakeLists.txt` has no effect.

## Versions

| What | Where | Bump when |
|---|---|---|
| **App version** | `<app>/VERSION` | Releasing that app |
| **Protocol version** | `XBX_PROTOCOL_VERSION` in `protocol.h` | Packet format changes; reflash **both** boards |
| **Build ID** | `git describe` (`APP_BUILD_VERSION`) | Automatic |

- Printed at boot, e.g.
  `xbx-nrf controller v0.1.0 (47bdc2425afb), protocol v1, channel 76, tx 8 dBm`.
- After `git tag v0.1.0` the build ID reads `v0.1.0` (`v0.1.0-3-g<hash>` later).
- Uncommitted changes don't show in the build ID: commit before flashing
  anything you keep.
- Signed images carry the app version (`0.1.0+0`) for MCUboot.

## Upgrading the SDK

1. Read Nordic's release notes and migration guide.
2. `nrfutil sdk-manager install --ncs-version <new>`; note the toolchain bundle
   ID (`~/ncs/toolchains/toolchains.json`).
3. Update `firmware/ncs-version`, `~/bin/ncs-shell` and the table above.
4. Pristine-build both apps, unsigned and signed; fix warnings.
5. Re-run the link test on hardware.
6. Commit it all together ("Upgrade to NCS vX.Y.Z").

Older SDKs can stay installed side by side for building old commits.
