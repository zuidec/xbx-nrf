---
title: Flashing & debugging the nRF52840 boards
created: 2026-09-28
updated: 2026-10-31
tags:
  - xbx-nrf
  - firmware
  - howto
aliases:
  - flashing
  - programming
  - SWD
---

# Equipment and configuration

## Hardware

| Board | Role | Built-in programming | Board target |
|---|---|---|---|
| Pro Micro nRF52840 (nice!nano clone) | Controller test build | nice!nano UF2 bootloader  | `promicro_nrf52840/nrf52840/uf2` |
| Nordic nRF52840 Dongle (PCA10059) | Dongle (off-the-shelf option) | Nordic USB DFU bootloader  | `nrf52840dongle/nrf52840` |
| Custom controller board | Controller | None; needs flashed with Semi-custom UF2 bootloader | `xbx_controller/nrf52840/uf2` |
| Custom dongle board | Dongle | None; needs flashed with Semi-custom UF2 bootloader | `xbx_dongle/nrf52840/uf2` |

| Debug probe | Notes |
|---|---|
| Generic CMSIS-DAP Probe | Should work fine, not tested but worth trying if you have one |
| Raspberry Pi Debug Probe | ~$12 CMSIS-DAP and connects directly to the custom boards  |

> [!NOTE]
> The Raspberry Pi Debug Probe doesn't work with Nordic's J-Link-only tools 
> (`nrfutil device`, nRF Connect Programmer over SWD, the nRF Connect VS Code 
> debugger).

Board overlay naming: see [Board overlays](building.md#board-overlays).

## Generic SWD wiring

| Probe | Target |
|---|---|
| SWDIO | SWDIO |
| SWCLK | SWCLK |
| GND | GND |
| 3.3 V / VAPP / TVCC | VTREF (**sense only**; power the target from USB) |
| NRST (optional) | NRST|


- **Pro Micro:** SWDIO/SWCLK pads on the underside (check the clone's
  silkscreen).
- **PCA10059:** pads on the underside (*nRF52840 Dongle User Guide*).
- **Custom dongle:** J1 (JST-SH, Raspberry Pi Debug Probe cable).
- **Custom controller board:** J1 (JST-SH, Raspberry Pi Debug Probe cable)

## Raspberry Pi Debug Probe (CMSIS-DAP)

### Wiring

| Probe port | Pins | Connect to |
|---|---|---|
| **D** (debug) | SC (SWCLK), GND, SD (SWDIO) | Target SWD pads |
| **U** (UART) | TX, GND, RX | Target UART (TX ↔ RX crossed); custom dongle: the probe's cable plugs straight into J3 |



### Custom boards (Preferred UF2 bootloader)

Custom boards use a modified Adafruit UF2 bootloader. After initial bootloader 
flash, a drive should automatically appear.

If the bootloader has been previously programmed, enter it as follows:

- **Controller:** Hold **LS+RS** at power on
- **Dongle:** Hold **PAIR** at power on
- **Both:** double-tap the reset button (only on USB for the controller)

> [!WARNING]
> **Config changes**
>
> Settings can be lost if the stored format has changed.
> Commonly affected settings are the stick calibration and pairing

1. Download the latest release:
    - [Controller 
    Releases](https://github.com/zuidec/xbx-nrf/releases?q=controller&expanded=true)
    - [Dongle 
    Releases](https://github.com/zuidec/xbx-nrf/releases?q=dongle&expanded=true)
2. Enter the bootloader as described above. Make sure the device is powered over 
   USB.
3. Open the USB drive that appears, either `XBXDONGLE` or `XBXCTRL`
4. Simply copy the corresponding UF2 file to the USB drive. It should disconnect 
   automatically when it completes

Alternatively, use the `Firmware update` section in the dongle or controller 
tab of the Configurator if the device is already running an older version of the 
firmware.

### Custom boards (SWD)

Both custom boards (`xbx_controller`, `xbx_dongle`) flash over SWD with a 
CMSIS-DAP Probe; `west flash` runs OpenOCD with CMSIS-DAP.

The script builds first, so the nRF Connect SDK is needed:
[building](building.md)

From `firmware/`:
```sh
./build.sh xbx-controller -f swd      # controller
./build.sh xbx-dongle -f swd          # custom dongle
```

- **App only:** the targets are the `uf2` variants, so this writes the app
  at `0x26000` and keeps the bootloader. Without a bootloader it won't boot
- **Power:** needs to be flashed on USB power
- **Logs:** the apps log over their USB serial port; J3 (UART) is present but 
not implemented for logging


### nRF52840 Dongle (PCA10059) — USB DFU (no probe)

1. Press the sideways **RESET** button (red LED pulses).
2. Find the bootloader's port (`ls -l /dev/serial/by-id/`, "Open DFU
   Bootloader"), then build and flash from `firmware/`:
   ```sh
   ./build.sh xbx-dongle-pca10059 -f /dev/ttyACMx
   ```
   The script packages `dongle/build-pca10059/dfu.zip` and runs
   `nrfutil nrf5sdk-tools dfu usb-serial`. GUI alternative: nRF Connect for
   Desktop → Programmer.

### Pro Micro nRF52840 — UF2 bootloader (no probe)

nice!nano UF2 bootloader (Adafruit nRF52). The `uf2` board variant links the
app after the SoftDevice area it expects.

1. Short **RST to GND twice** quickly: the LED fades and a `NICENANO` drive
   appears.
2. Build and flash, from `firmware/`, giving the drive's block device (mounted
   for you) or its mount point:
   ```sh
   ./build.sh xbx-nrf-promicro -f /dev/sdX
   ```
   Or copy `xbx-nrf/build-promicro/xbx-nrf/zephyr/zephyr.uf2` onto the
   drive by hand.
3. The app reboots; logs appear on `/dev/ttyACM*` (USB CDC console).

> [!NOTE]
> **P0.13 = external VCC switch**
>
> On nice!nano-style boards P0.13 switches the `VCC` pin (low = on). Drive it
> low when powering sticks or sensors from `VCC`, or use `3.3V` instead.

## Locked chip (APPROTECT)

Newer nRF52840s might lock debug access unless the firmware unlocks it: the 
probe connects but can't halt or read memory.

1. OpenOCD 0.12+ with the **DAP-mode** ST-Link interface:
   ```sh
   openocd -f interface/stlink-dap.cfg -f target/nrf52.cfg \
           -c "init; nrf52_recover; exit"
   ```
2. Without `nrf52_recover`, write CTRL-AP (AP 1) directly:
   ```
   nrf52.dap apreg 1 0x04 0x01   ; ERASEALL
   sleep 500                      ; (poll 0x08 until 0)
   nrf52.dap apreg 1 0x04 0x00
   nrf52.dap apreg 1 0x00 0x01   ; reset
   nrf52.dap apreg 1 0x00 0x00
   ```
3. If the ST-Link can't (old/clone firmware without DAP mode), recover with a
   CMSIS-DAP probe.

Keep debug unlocked during development: Zephyr's APPROTECT Kconfig choice
(`CONFIG_NRF_APPROTECT_USE_UICR` default, `_LOCK`, `_USER_HANDLING`); check
the options in your SDK.



The target leaves room for the Nordic bootloader. If SWD erased it, restore it
first ([Restoring bootloaders](#restoring-bootloaders)).

## Restoring bootloaders

- **Pro Micro:** nice!nano bootloader hex (nice!nano / Adafruit nRF52
  bootloader releases), over SWD:
  ```sh
  BL=nice_nano_bootloader-0.9.2_s140_6.1.1.hex
  openocd -f interface/stlink.cfg -f target/nrf52.cfg \
          -c "init; reset halt; nrf5 mass_erase" \
          -c "program $BL verify reset exit"
  ```
- **Custom boards:** `./build.sh xbx-nrf-bootloader -f swd` /
  `./build.sh xbx-dongle-bootloader -f swd`.
- **PCA10059:** nRF5 SDK `examples/dfu/open_bootloader/pca10059_usb_debug`,
  over SWD.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| ST-Link won't connect ("target voltage 0 V") | Target 3.3 V not on the sense pin |
| Connects, can't halt or read memory | Chip locked → [Locked chip (APPROTECT)](#locked-chip-approtect) |
| Dongle app doesn't run after SWD flashing | Bootloader gone → build for the `bare` variant |
| Custom board app doesn't run after `-f swd` | No UF2 bootloader (the app starts at `0x26000`) → install it ([Custom boards — UF2 bootloader (Preferred)](#custom-boards--uf2-bootloader-preferred)) |
| No UF2 drive | Double tap too slow; charge-only USB cable |
| `west flash`: no matching UF2 partition | Drive not mounted |
| No `/dev/ttyACM*` after flashing | App crashed early; user not in `dialout` |
| Board overlay has no effect | Wrong filename ([Board overlays](building.md#board-overlays)) |
| Controller stops after an OpenOCD command | CPU left halted: add `resume`, or power-cycle |
