---
title: Flashing & debugging the nRF52840 boards
created: 2026-09-28
updated: 2026-10-02
tags:
  - xbx-nrf
  - firmware
  - howto
aliases:
  - flashing
  - programming
  - SWD
---

# Flashing & debugging the nRF52840 boards

Related: [[docs/building]], [[docs/signing]], [[todo#5. Firmware]].

> [!warning] Mostly untested
> ✅ = verified on hardware. Everything else is from docs and may depend on tool
> versions; mark it once it works.

## Hardware

| Board | Role | Built-in programming | Board target |
|---|---|---|---|
| Pro Micro nRF52840 (nice!nano clone) | Controller prototype, dongle stand-in | nice!nano UF2 bootloader ✅ | `promicro_nrf52840/nrf52840/uf2` |
| Nordic nRF52840 Dongle (PCA10059) | Dongle (off-the-shelf option) | Nordic USB DFU bootloader ✅ | `nrf52840dongle/nrf52840` |
| Custom bottom board | Controller | None; SWD only | TBD |
| Custom dongle board | Dongle | None; SWD only (J1, JST-SH) | `xbx_dongle/nrf52840` |

| Debug probe | Notes |
|---|---|
| **ST-Link** (on hand) | OpenOCD / pyOCD / probe-rs. May not unlock a locked chip ([[#Locked chip (APPROTECT)]]) |
| Raspberry Pi Debug Probe | ~$12 CMSIS-DAP fallback that can ([[#Raspberry Pi Debug Probe (CMSIS-DAP)]]) |

Neither works with Nordic's J-Link-only tools (`nrfutil device`, nRF Connect
Programmer over SWD, the nRF Connect VS Code debugger).

Board overlay naming: see [[docs/building#Board overlays]].

## SWD wiring

| Probe | Target |
|---|---|
| SWDIO | SWDIO |
| SWCLK | SWCLK |
| GND | GND |
| 3.3 V / VAPP / TVCC | Target 3.3 V (**sense only**; power the target from USB) |
| NRST (optional) | RESET |

An official ST-Link won't connect without the target 3.3 V on its sense pin.

- **Pro Micro:** SWDIO/SWCLK pads on the underside (check the clone's
  silkscreen).
- **PCA10059:** pads on the underside (*nRF52840 Dongle User Guide*).
- **Custom dongle:** J1 (JST-SH, Raspberry Pi Debug Probe cable). `west flash`
  defaults to OpenOCD with CMSIS-DAP (the Pi probe).
- **Custom controller board:** SWD header or pads, TBD ([[todo#MCU / radio]]).

> [!warning] SWD flashing erases the bootloader
> A full erase or recover wipes the UF2 / Nordic USB bootloader; restore it with
> [[#Restoring bootloaders]].

Paths below use `$HEX` / `$ELF` for `<app>/build/<app>/zephyr/zephyr.hex` /
`.elf`.

## ST-Link + OpenOCD

### Flash
```sh
openocd -f interface/stlink.cfg -f target/nrf52.cfg \
        -c "program $HEX verify reset exit"
```

### Debug
```sh
openocd -f interface/stlink.cfg -f target/nrf52.cfg   # GDB server on :3333
arm-none-eabi-gdb $ELF -ex "target extended-remote :3333"
```
VS Code: **Cortex-Debug** (`"servertype": "openocd"`, configFiles
`interface/stlink.cfg`, `target/nrf52.cfg`).

### RTT logging (optional)
With OpenOCD running, in `telnet localhost 4444`:
```
rtt setup 0x20000000 0x40000 "SEGGER RTT"
rtt start
rtt server start 9090 0
```
Then `nc localhost 9090`. Zephyr: `CONFIG_USE_SEGGER_RTT=y`,
`CONFIG_LOG_BACKEND_RTT=y`.

### pyOCD alternative
```sh
pyocd pack install nrf52840      # once
pyocd flash -t nrf52840 $HEX
pyocd gdbserver -t nrf52840
```

## Raspberry Pi Debug Probe (CMSIS-DAP)

Same as the ST-Link with `interface/cmsis-dap.cfg`, plus reliable chip unlock
and a built-in USB-UART for logs.

### Wiring

| Probe port | Pins | Connect to |
|---|---|---|
| **D** (debug) | SC (SWCLK), GND, SD (SWDIO) | Target SWD pads |
| **U** (UART) | TX, GND, RX | Target UART (TX ↔ RX crossed); custom dongle: the probe's cable plugs straight into J3 |

3.3 V I/O, no sense pin, no reset wire (reset over SWD works).

### OpenOCD
```sh
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg \
        -c "program $HEX verify reset exit"                  # flash
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg       # GDB server
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg \
        -c "init; nrf52_recover; exit"                       # unlock
```

### probe-rs (alternative)
Single binary with flashing, RTT and unlock; also supports the ST-Link.
```sh
probe-rs download --chip nRF52840_xxAA $HEX
probe-rs erase --chip nRF52840_xxAA --allow-erase-all   # unlock
probe-rs attach --chip nRF52840_xxAA $ELF               # RTT viewer
probe-rs gdb --chip nRF52840_xxAA                       # GDB server
```
Install: installer from probe-rs.rs (not packaged for Tumbleweed), plus its
udev rules.

### UART logs
```sh
picocom -b 115200 /dev/ttyACM1   # the probe's UART; see `ls /dev/ttyACM*`
```
Needs the Zephyr console on the UART pins wired to the probe.

## Locked chip (APPROTECT)

Newer nRF52840s lock debug access unless the firmware unlocks it: the probe
connects but can't halt or read memory. **Recovering** = full erase via
Nordic's CTRL-AP (bootloader and UICR included).

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

## Pro Micro nRF52840 — UF2 bootloader (no probe) ✅

nice!nano UF2 bootloader (Adafruit nRF52). The `uf2` board variant links the
app after the SoftDevice area it expects.

1. Short **RST to GND twice** quickly: the LED fades and a `NICENANO` drive
   appears.
2. Build and flash, from `firmware/`, giving the drive's block device (mounted
   for you) or its mount point:
   ```sh
   ./build-unsigned.sh xbx-nrf -f /dev/sdX
   ```
   Or copy `<app>/build/<app>/zephyr/zephyr.uf2` onto the drive by hand.
3. The app reboots; logs appear on `/dev/ttyACM*` (USB CDC console).

> [!note] P0.13 = external VCC switch
> On nice!nano-style boards P0.13 switches the `VCC` pin (low = on). Drive it
> low when powering sticks or sensors from `VCC`, or use `3.3V` instead.

## nRF52840 Dongle (PCA10059) — USB DFU (no probe) ✅

1. Press the sideways **RESET** button (red LED pulses).
2. Find the bootloader's port (`ls -l /dev/serial/by-id/`, "Open DFU
   Bootloader"), then build and flash from `firmware/`:
   ```sh
   ./build-unsigned.sh dongle -f /dev/ttyACMx
   ```
   The script packages `<app>/build/dfu.zip` and runs `nrfutil nrf5sdk-tools
   dfu usb-serial`. GUI alternative: nRF Connect for Desktop → Programmer.

The default board target leaves room for the Nordic bootloader. If SWD erased
it, build for `nrf52840dongle/nrf52840/bare` instead.

## Restoring bootloaders

- **Pro Micro:** nice!nano bootloader hex (nice!nano / Adafruit nRF52
  bootloader releases), over SWD:
  ```sh
  BL=nice_nano_bootloader-0.9.2_s140_6.1.1.hex
  openocd -f interface/stlink.cfg -f target/nrf52.cfg \
          -c "init; reset halt; nrf5 mass_erase" \
          -c "program $BL verify reset exit"
  ```
- **PCA10059:** nRF5 SDK `examples/dfu/open_bootloader/pca10059_usb_debug`,
  over SWD.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| ST-Link won't connect ("target voltage 0 V") | Target 3.3 V not on the sense pin |
| Connects, can't halt or read memory | Chip locked → [[#Locked chip (APPROTECT)]] |
| Dongle app doesn't run after SWD flashing | Bootloader gone → build for the `bare` variant |
| No UF2 drive | Double tap too slow; charge-only USB cable |
| `west flash`: no matching UF2 partition | Drive not mounted |
| No `/dev/ttyACM*` after flashing | App crashed early; user not in `dialout` |
| Board overlay has no effect | Wrong filename ([[docs/building#Board overlays]]) |
