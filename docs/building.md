---
title: Building the firmware
created: 2026-09-29
updated: 2026-10-31
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

| App | Folder | Board | Also |
|---|---|---|---|
| Controller | `firmware/xbx-nrf` | `xbx_controller/nrf52840/uf2` (custom board) | `promicro_nrf52840/nrf52840/uf2` (test build) |
| Dongle | `firmware/dongle` | `xbx_dongle/nrf52840/uf2` (custom board) | `nrf52840dongle/nrf52840` (PCA10059, off the shelf) |

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

Two targets per app, each with its own board and build folder; board
bootloaders stay.

| Target | Board | Folder |
|---|---|---|
| `xbx-nrf` | `xbx_controller/nrf52840/uf2` | `xbx-nrf/build/` |
| `xbx-nrf-promicro` | `promicro_nrf52840/nrf52840/uf2` (test build) | `xbx-nrf/build-promicro/` |
| `xbx-dongle` | `xbx_dongle/nrf52840/uf2` | `dongle/build/` |
| `xbx-dongle-pca10059` | `nrf52840dongle/nrf52840` | `dongle/build-pca10059/` |

```sh
cd ~/Projects/xbx-nrf/firmware
./build.sh xbx-nrf                  # incremental
./build.sh xbx-dongle -p            # pristine
./build.sh xbx-dongle -f /dev/sdX   # build, then flash (UF2 drive)
./build.sh xbx-dongle -r            # release build
./build.sh xbx-dongle-pca10059 -f /dev/ttyACM0
./build.sh xbx-nrf-bootloader       # controller bootloader ([[#Bootloader]])
./build.sh xbx-dongle-bootloader    # dongle bootloader
```

- Runs from any folder and shell: outside `ncs-shell` it restarts itself in
  the pinned toolchain (`nrfutil sdk-manager toolchain launch`).
- Extra arguments go to `west build`; the target sets the board (`-b` is
  refused, so a folder never changes board).
- `-r` / `--release` adds the app's `release.conf` (no USB console, no logs)
  and builds into the target's folder with `-release` added.
- `-f` / `--flash <device>` flashes after a successful build
  ([[docs/flashing]]):

  | Board | Device |
  |---|---|
  | PCA10059 | DFU bootloader's serial port (press RESET first) |
  | `*/uf2` (ours, Pro Micro) | UF2 drive: mount point, or block device (`/dev/sdX`, mounted for you); `swd`: the app over SWD |
- `-- -DCONFIG_…` options stay in that build folder's cache until a pristine
  build (`-p`).
- `BUILD_DIR=<dir>` builds into `<app>/<dir>/` instead, for a second
  configuration next to the main one. E.g. the fake-input controller:
  ```sh
  BUILD_DIR=build-p2 ./build.sh xbx-nrf -p -- -DCONFIG_XBX_FAKE_INPUT=y
  ```
- The custom boards' plain form (`xbx_controller/nrf52840`, no bootloader)
  exists only because Zephyr needs it; the apps have no overlay for it.
- Plain `west build` also works but skips the version check, and needs `-b`
  again after `-p`.

### Output files

Sysbuild puts each image in a subfolder named after the app, inside the
target's folder (`<dir>`, [[#Building]]):

| File | Use |
|---|---|
| `<dir>/<app>/zephyr/zephyr.uf2` | UF2 bootloader (custom boards, Pro Micro) |
| `<dir>/<app>/zephyr/zephyr.hex` | SWD flashing; PCA10059 DFU (`dfu.zip`) |
| `<dir>/<app>/zephyr/zephyr.elf` | Debugging |

`<dir>/zephyr/` is sysbuild's own and has no firmware.

### Board overlays

Name them after the **full** board target, `/` → `_`:

| Board target | Overlay file |
|---|---|
| `promicro_nrf52840/nrf52840/uf2` | `promicro_nrf52840_nrf52840_uf2.overlay` |
| `nrf52840dongle/nrf52840` | `nrf52840dongle_nrf52840.overlay` |
| `xbx_dongle/nrf52840/uf2` | `xbx_dongle_nrf52840_uf2.overlay` |
| `xbx_controller/nrf52840/uf2` | `xbx_controller_nrf52840_uf2.overlay` |

Shorter names are **silently ignored**; look for `-- Found devicetree overlay:`
in the build log. Board `.conf` files follow the same rule
(`xbx_controller_nrf52840_uf2.conf`).

### Default board: why it's in the script

Sysbuild resolves the board before reading the app, so `set(BOARD …)` in an
app's `CMakeLists.txt` has no effect.

## Bootloader

`firmware/bootloader/`: the Adafruit nRF52 Bootloader, fetched at a pinned
tag (`UPSTREAM` in `bootloader/VERSION`) into `src/` (gitignored), plus our
board folders in `boards/`. `./build.sh xbx-nrf-bootloader` /
`xbx-dongle-bootloader` build into `bootloader/build/<board>/`; `-f swd`
installs it ([[docs/flashing]]).

| File | Use |
|---|---|
| `<board>_bootloader-<version>_s140_6.1.1.hex` | Full install over SWD |
| `update-<board>_bootloader-<version>_nosd.uf2` | Bootloader update, copied onto its own drive |

- Needs the **Arm GNU Toolchain** (`ARM_GCC_DIR`, default
  `~/.local/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi`). openSUSE's
  newlib links a semihosting `crt0` that faults without a debugger.
- USB IDs: pid.codes test PIDs `1209:0005` (dongle), `1209:0006`
  (controller).

## Versions

Each component has its own version and tags; they don't move together.
Compatibility between them is the protocol and channel versions, not
matching numbers.

| Component | Version in | Tag |
|---|---|---|
| Controller firmware | `firmware/xbx-nrf/VERSION` | `controller-v<version>` |
| Dongle firmware | `firmware/dongle/VERSION` | `dongle-v<version>` |
| Bootloader | `firmware/bootloader/VERSION`: `<upstream>-xbx.<revision>` | `bootloader-v<version>` |
| Configurator | `__version__` in `tools/configurator/xbxconfig/__init__.py` | `configurator-v<version>` |
| Hardware | the board revision | `hw-rev<rev>` |

| Compatibility | Where | Bump when |
|---|---|---|
| **Radio protocol** | `XBX_PROTOCOL_VERSION` in `protocol.h` | Packet format changes; reflash **both** boards |
| **Configurator channel** | `CFG_USB_VERSION_*` in `config_usb.h` | Major: incompatible; minor: additions ([[docs/configurator#Versioning]]) |

- **Release:** bump the version in its own commit (e.g.
  `Controller: version 0.6.0`), then an annotated tag on that commit:
  `git tag -a controller-v0.6.0 -m "<what's new>"`.
- **Bootloader revision:** bump `REVISION` for any change to our board files
  or a fix; a new upstream tag sets `UPSTREAM` and `REVISION = 1`. It shows
  in the drive's `INFO_UF2.TXT` (`UF2 Bootloader 0.11.0-xbx.1`).
- The `v0.1.0`–`v0.5.0` tags predate this: both firmwares together.
- **Build ID** (firmware): `git describe` over that component's tags only
  (`common/build_id.cmake`; Zephyr's `APP_BUILD_VERSION` takes any tag),
  e.g. `controller-v0.6.0-3-g<hash>`; just the hash before its first tag.
  Printed at boot: `xbx-nrf controller v0.6.0 (controller-v0.6.0), protocol
  v3, tx 8 dBm`.
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
