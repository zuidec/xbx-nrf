---
title: Building the firmware
created: 2026-09-29
updated: 2026-10-30
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

Related: [[docs/flashing|Flashing & debugging]], [[todo#5. Firmware]].

## Apps

| App | Folder | Default board | Also supported |
|---|---|---|---|
| Controller | `firmware/xbx-nrf` | `xbx_controller/nrf52840/uf2` (custom board; `xbx_controller/nrf52840` without bootloader) | `promicro_nrf52840/nrf52840/uf2` |
| Dongle | `firmware/dongle` | `nrf52840dongle/nrf52840` (PCA10059, off the shelf) | `xbx_dongle/nrf52840/uf2` (custom board, `xbx-dongle` target; `xbx_dongle/nrf52840` without bootloader), `promicro_nrf52840/nrf52840/uf2` |

Standalone Zephyr apps, outside the SDK folder. Shared code:
`firmware/common/include/` (e.g. `protocol.h`). Our own board definitions:
`firmware/boards/`; the build script passes it as `BOARD_ROOT` (sysbuild
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

`firmware/check-sdk.sh` (run by the build script) stops the build if
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
   export ZEPHYR_BASE="$HOME/ncs/v3.4.1/zephyr"
   nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 --shell
   ```
   `ZEPHYR_BASE` lets `west build` work from our app folders; without it west
   reports "unknown command build".
3. USB permissions: see [[docs/flashing]].

## Building

Builds go into `<app>/build/`; board bootloaders stay.

```sh
cd ~/Projects/xbx-nrf/firmware
./build.sh xbx-nrf                  # incremental
./build.sh dongle -p                # pristine
./build.sh dongle -f /dev/ttyACM0   # build, then flash
./build.sh dongle -r                # release build
./build.sh xbx-dongle -f /dev/sdX   # custom dongle, UF2 drive
./build.sh xbx-nrf-bootloader       # controller bootloader ([[#Bootloader]])
./build.sh xbx-dongle-bootloader    # dongle bootloader
```

- Runs from any folder and shell: outside `ncs-shell` it restarts itself in
  the pinned toolchain (`nrfutil sdk-manager toolchain launch`).
- Extra arguments go to `west build`; `-b` overrides the default board.
- `-r` / `--release` adds the app's `release.conf` (no USB console, no logs)
  and builds into `<app>/build-release/`.
- `-f` / `--flash <device>` flashes after a successful build
  ([[docs/flashing]]):

  | Board | Device |
  |---|---|
  | `nrf52840dongle/nrf52840` | DFU bootloader's serial port (press RESET first) |
  | `*/uf2` (Pro Micro) | UF2 drive: mount point, or block device (`/dev/sdX`, mounted for you) |
  | Others (custom boards) | `swd`: `west flash` with the board's runner |
- `-- -DCONFIG_…` options stay in that build folder's cache until a pristine
  build (`-p`).
- `BUILD_DIR=<dir>` builds into `<app>/<dir>/` instead, for a second
  configuration next to the main one. E.g. the fake-input controller:
  ```sh
  BUILD_DIR=build-p2 ./build.sh xbx-nrf -p -- -DCONFIG_XBX_FAKE_INPUT=y
  ```
- Default boards live in `build.sh`: `xbx_controller/nrf52840/uf2` for the
  controller, the PCA10059 for `dongle`. `xbx-dongle` is the dongle app on
  `xbx_dongle/nrf52840/uf2`, built into `dongle/build-xbx/` (`-r`:
  `build-xbx-release/`) so both dongle builds coexist.
- Plain `west build` also works but skips the version check, and needs `-b`
  again after `-p`.

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
| `xbx_dongle/nrf52840/uf2` | `xbx_dongle_nrf52840_uf2.overlay` (includes the one above) |
| `xbx_controller/nrf52840` | `xbx_controller_nrf52840.overlay` |
| `xbx_controller/nrf52840/uf2` | `xbx_controller_nrf52840_uf2.overlay` (includes the one above) |

Shorter names are **silently ignored**; look for `-- Found devicetree overlay:`
in the build log. Board `.conf` files follow the same rule but can't
include each other: `xbx_controller_nrf52840_uf2.conf` is a symlink to the
plain board's.

### Default board: why it's in the script

Sysbuild resolves the board before reading the app, so `set(BOARD …)` in an
app's `CMakeLists.txt` has no effect.

## Bootloader

`firmware/bootloader/`: the Adafruit nRF52 Bootloader, fetched at a pinned
tag (`TAG` in `bootloader/build.sh`) into `src/` (gitignored), plus our
board folders in `boards/`. `./build.sh xbx-nrf-bootloader` /
`xbx-dongle-bootloader` build into `bootloader/build/<board>/`; `-f swd`
installs it ([[docs/flashing]]).

| File | Use |
|---|---|
| `<board>_bootloader-<tag>_s140_6.1.1.hex` | Full install over SWD |
| `update-<board>_bootloader-<tag>_nosd.uf2` | Bootloader update, copied onto its own drive |

- Needs the **Arm GNU Toolchain** (`ARM_GCC_DIR`, default
  `~/.local/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi`). openSUSE's
  newlib links a semihosting `crt0` that faults without a debugger.
- USB IDs: pid.codes test PIDs `1209:0005` (dongle), `1209:0006`
  (controller).

## Versions

| What | Where | Bump when |
|---|---|---|
| **App version** | `<app>/VERSION` | Releasing that app |
| **Protocol version** | `XBX_PROTOCOL_VERSION` in `protocol.h` | Packet format changes; reflash **both** boards |
| **Build ID** | `git describe` (`APP_BUILD_VERSION`) | Automatic |

- Printed at boot, e.g.
  `xbx-nrf controller v0.5.0 (v0.5.0), protocol v3, tx 8 dBm`.
- After `git tag v0.1.0` the build ID reads `v0.1.0` (`v0.1.0-3-g<hash>` later).
- Uncommitted changes don't show in the build ID: commit before flashing
  anything you keep.

## Upgrading the SDK

1. Read Nordic's release notes and migration guide.
2. `nrfutil sdk-manager install --ncs-version <new>`; note the toolchain bundle
   ID (`~/ncs/toolchains/toolchains.json`).
3. Update `firmware/ncs-version`, `~/bin/ncs-shell` and the table above.
4. Pristine-build both apps; fix warnings.
5. Re-run the link test on hardware.
6. Commit it all together ("Upgrade to NCS vX.Y.Z").

Older SDKs can stay installed side by side for building old commits.
