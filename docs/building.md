---
title: Building
created: 2026-09-29
updated: 2026-11-04
tags:
  - xbx-nrf
  - firmware
  - howto
aliases:
  - building
  - build
  - versions
---

# Building

The firmware, bootloader and configurator build separately; each has its own
section with commands and output files. Pre-built files available here:
[Getting started](getting_started.md).

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

The SDK and its toolchain come through `nrfutil`; the apps need no other
compiler. The bootloader also needs the Arm GNU Toolchain and `make`.

### All platforms

| Needs | For | Where |
|---|---|---|
| nrfutil | SDK and toolchain | [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util): download, put on the `PATH` (Linux: `chmod +x`) |
| Arm GNU Toolchain | bootloader | [Arm](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads): `arm-none-eabi` ([Bootloader](#bootloader)) |
| OpenOCD | SWD programming | [Getting started](getting_started.md#preparing-to-flash-the-bootloader) |

### Linux

| Needs | For | Package (names vary by distro) |
|---|---|---|
| Git | the code | `git` |
| `make` | bootloader | `make` |
| Python 3 with tkinter and venv | configurator | `python3`, `python3-tk`, `python3-venv` |
| `objdump` | configurator build | `binutils` |
| podman or docker | configurator `--release` build | `podman` |

1. Code, SDK and toolchain (a few GB, into `~/ncs/`):
   ```sh
   git clone https://github.com/zuidec/xbx-nrf.git
   nrfutil install sdk-manager
   nrfutil sdk-manager install --ncs-version v3.4.1
   ```
2. USB permissions: the udev rule ([Linux](getting_started.md#linux), step 2,
   or `xbx-config.py install-udev`); SWD probes: [flashing](flashing.md).

### Windows

Not tested yet. Every command runs in **Git Bash**. Git Bash has no `make`,
so flash a released bootloader `.hex` instead of building it
([Bootloader](#bootloader)).

| Needs | For | Where |
|---|---|---|
| Git for Windows | the code, Git Bash | <https://git-scm.com/downloads/win> |
| Python 3 | configurator | <https://www.python.org/downloads/>: tick "Add python.exe to PATH" in the installer (tkinter included) |

1. Code, SDK and toolchain (into `C:\ncs\`):
   ```sh
   git clone https://github.com/zuidec/xbx-nrf.git
   nrfutil install sdk-manager
   nrfutil sdk-manager install --ncs-version v3.4.1
   ```
2. The build script looks in `~/ncs/`: point it at `C:\ncs\` (add to
   `~/.bashrc` to keep it):
   ```sh
   export ZEPHYR_BASE=/c/ncs/v3.4.1/zephyr
   ```

### macOS

Not tested yet. If you'd like to try it, open an issue on
[GitHub](https://github.com/zuidec/xbx-nrf/issues).

## Firmware

- Two standalone Zephyr apps:
    1. Controller (`firmware/xbx-nrf/`)
    2. Dongle (`firmware/dongle/`).
- Shared code: `firmware/common/include/` (e.g. `protocol.h`).
- Our board definitions: `firmware/boards/`; `build.sh` passes it as 
  `BOARD_ROOT` (sysbuild doesn't search the app folder).

Build the targets for your hardware: `xbx-controller` and `xbx-dongle` for
the custom boards. Each target has its own build folder (paths from the repo
root).

| Target | Board | Build folder |
|---|---|---|
| `xbx-controller` | `xbx_controller/nrf52840/uf2` | `firmware/xbx-nrf/build/` |
| `xbx-controller-promicro` | `promicro_nrf52840/nrf52840/uf2` (test build) | `firmware/xbx-nrf/build-promicro/` |
| `xbx-dongle` | `xbx_dongle/nrf52840/uf2` | `firmware/dongle/build/` |
| `xbx-dongle-pca10059` | `nrf52840dongle/nrf52840` (PCA10059, off the shelf) | `firmware/dongle/build-pca10059/` |

### Commands

From the repo root, in a Bash shell (Linux, macOS, or Git Bash on Windows;
not PowerShell or cmd):

```sh
firmware/build.sh xbx-controller                      # incremental
firmware/build.sh xbx-controller -r                   # release build
firmware/build.sh xbx-dongle -p                       # pristine
firmware/build.sh xbx-dongle -f /dev/<sdX|ttyACMX>    # build, then flash
```

- Runs from any folder and shell: if needed it restarts itself in the
  pinned toolchain (`nrfutil sdk-manager toolchain launch`).
- Extra arguments go to `west build`; the target sets the board (`-b` is
  refused, so a folder never changes board).
- `-r` / `--release` adds the app's `release.conf` (no USB console, no logs)
  and builds into the target's folder with `-release` added.
- `-f` / `--flash <device>` flashes after a successful build; the device
  for each board is in [flashing](flashing.md).
- `-- -DCONFIG_…` options stay in that build folder's cache until a pristine
  build (`-p`).
- `BUILD_DIR=<dir>` builds into `<app>/<dir>/` instead, for a second
  configuration next to the main one. E.g. the fake-input controller:
  ```sh
  BUILD_DIR=build-p2 firmware/build.sh xbx-controller -p -- \
      -DCONFIG_XBX_FAKE_INPUT=y
  ```
- The custom boards' plain form (`xbx_controller/nrf52840`, no bootloader)
  exists only because Zephyr needs it; the apps have no overlay for it.

> [!TIP]
> **SDK not found, or the wrong version**
>
> `build.sh` keeps `ZEPHYR_BASE` if it's already set; otherwise it uses
> `~/ncs/<version>/zephyr` for the version in `firmware/ncs-version`. If it
> reports no SDK or the wrong one, check `echo $ZEPHYR_BASE`: a value left
> over from another SDK or project wins. Unset it, or point it at the pinned
> SDK's `zephyr` folder.

### Output files

Images are in `<build folder>/<app>/zephyr/`:

- Controller: `firmware/xbx-nrf/build/xbx-nrf/zephyr/`
- Dongle: `firmware/dongle/build/dongle/zephyr/`

Other targets and release builds swap `build` for their folder
(`build-promicro`, `build-release`, …).

| File | Use |
|---|---|
| `zephyr.uf2` | UF2 flashing (custom boards, Pro Micro) |
| `zephyr.hex` | SWD flashing; PCA10059 DFU (`dfu.zip`) |
| `zephyr.elf` | Debugging |

The build folder's own `zephyr/` (e.g. `firmware/xbx-nrf/build/zephyr/`) is
sysbuild's and has no firmware.

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

## Bootloader

Custom versions of the Adafruit nRF52 Bootloader. The repo holds only
the custom board files(`firmware/bootloader/boards/`) and `VERSION`; the first 
build clones Adafruit's repo at the pinned tag (`UPSTREAM` in `VERSION`) into
`firmware/bootloader/src/`, copies in the custom files, and runs its
Makefile.

```sh
firmware/build.sh xbx-controller-bootloader     # build
firmware/build.sh xbx-dongle-bootloader -f swd  # build, erase all, flash
firmware/build.sh xbx-controller-bootloader -f swd --hex <file>
```

The `--hex` option flashes a downloaded `.hex` without building (needs only
OpenOCD). `-f swd` erases the app and its settings too
([flashing](flashing.md)).

Output, in `firmware/bootloader/build/<board>/` (`xbx_controller`,
`xbx_dongle`):

| File | Use |
|---|---|
| `<board>_bootloader-<version>_s140_6.1.1.hex` | Full install over SWD |
| `update-<board>_bootloader-<version>_nosd.uf2` | Bootloader update, copied onto its own drive |

- Needs the **Arm GNU Toolchain** (`ARM_GCC_DIR`, default
  `~/.local/opt/arm-gnu-toolchain-14.2.rel1-x86_64-arm-none-eabi`). openSUSE's
  newlib links a semihosting `crt0` that faults without a debugger.
- After a `VERSION` bump to a new upstream tag, delete `src/` to fetch it
  again.
- USB IDs: pid.codes test PIDs `1209:0005` (dongle), `1209:0006`
  (controller).

## Configurator

Run from source: `python3 tools/configurator/xbx-config.py` (Windows:
`python`). One-file build:

```sh
tools/configurator/build.sh             # Current OS
tools/configurator/build.sh --release   # Linux release (in a container)
```

Output, in `tools/configurator/output/`: `xbx-config-<version>-<os>-<cpu>`
(`.exe` on Windows; `-local` added on Linux without `--release`). Details:
[Building](configurator.md#building).

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
| **Configurator channel** | `CFG_USB_VERSION_*` in `config_usb.h` | Major: incompatible; minor: additions ([Versioning](configurator.md#versioning)) |


- **Release:** bump the version in its own commit (e.g.
  `Controller: version 0.6.0`), then an annotated tag on that commit:
  `git tag -a controller-v0.6.0 -m "<what's new>"`.
- **Release binaries:** attach the `-r` firmware build or the configurator
  (Linux `--release`, Windows `.exe`), and the notices for the third-party
  code inside them. After the builds,
  `tools/release-notices.sh <out dir> [firmware] [configurator]` writes
  `THIRD-PARTY-NOTICES-firmware.txt` and, for this OS's configurator build,
  `THIRD-PARTY-NOTICES-configurator.txt` (Linux) or
  `THIRD-PARTY-NOTICES-configurator-windows.txt` (Windows). The
  Configurator (Windows) workflow builds the `.exe` and runs it (Actions →
  Run workflow). The notes link the tag as the source (MPL-2.0).
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
3. Update `firmware/ncs-version` and [Pinned versions](#pinned-versions).
4. Pristine-build both apps; fix warnings.
5. Re-run the link test on hardware.
6. Commit it all together ("Upgrade to NCS vX.Y.Z").

Older SDKs can stay installed side by side for building old commits.
