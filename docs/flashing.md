---
title: Flashing & debugging the nRF52840 boards
created: 2026-09-28
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

How to program and debug each nRF52840 board in this project. Related tasks: [[todo#5. Firmware]].

> [!warning] Not tested yet
> These are notes collected before any hardware was on the bench, and some commands depend on tool versions. Update this page with what actually works (✅) once each method has been tried.

## Hardware

| Board | Role | Built-in programming | Board target (Zephyr / NCS) |
|---|---|---|---|
| Pro Micro nRF52840 (nice!nano clone) | Controller prototype, dongle stand-in | nice!nano UF2 bootloader (Adafruit nRF52, **confirmed**) | `promicro_nrf52840/nrf52840/uf2` |
| Nordic nRF52840 Dongle (PCA10059) | Dongle (prototype, maybe final) | Nordic USB DFU bootloader (nrfutil) | `nrf52840dongle/nrf52840` |
| Custom bottom board | Controller | None; SWD only | Custom board definition (TBD) |

| Debug probe | Notes |
|---|---|
| **ST-Link** (on hand) | Works with nRF52 through OpenOCD / pyOCD. Recovering a locked chip may not work. See [[#Locked chip (APPROTECT)]] |
| Raspberry Pi Debug Probe (CMSIS-DAP) | ~$12 fallback if the ST-Link can't unlock a chip. Works with OpenOCD / pyOCD / probe-rs. See [[#Raspberry Pi Debug Probe (CMSIS-DAP)]] |

> [!note] No Nordic J-Link tools
> Neither probe works with Nordic's J-Link-based tools (`nrfutil device`, nRF Connect Programmer over SWD, the nRF Connect for VS Code debugger). Use OpenOCD, pyOCD or probe-rs instead.

Board target names follow the Zephyr "hardware model v2" naming. Older SDKs use `nrf52840dongle_nrf52840`-style names. Check with `west boards | grep -E "promicro|dongle"`.

> [!important] Board overlay filenames
> App overlays in `boards/` must use the **full** board target with `/` → `_`, e.g. `promicro_nrf52840_nrf52840_uf2.overlay` for `promicro_nrf52840/nrf52840/uf2`. A plain `promicro_nrf52840.overlay` is silently ignored. Check the build log for `-- Found devicetree overlay:`.

## SWD wiring

| Probe | Target |
|---|---|
| SWDIO | SWDIO |
| SWCLK | SWCLK |
| GND | GND |
| 3.3 V / VAPP / TVCC | Target 3.3 V (**voltage sense only**; power the target from USB) |
| NRST (optional) | RESET |

> [!important] Target voltage sense
> An official ST-Link needs the target's 3.3 V on its voltage-sense pin or it won't connect. Don't also power the target from the probe while it's plugged into USB.

- **Pro Micro nRF52840:** SWD (SWDIO, SWCLK) is on **pads on the underside** on most nice!nano clones. Check your clone's silkscreen or seller pinout, then use pogo pins or soldered wires.
- **PCA10059 dongle:** SWD is on pads on the underside. Check the pinout in the Nordic *nRF52840 Dongle User Guide*.
- **Custom board:** include an SWD connector or Tag-Connect footprint (see [[todo#MCU / radio]]).

> [!warning] SWD flashing erases the bootloader
> Programming over SWD (a full erase or a recover) wipes the UF2 or Nordic USB bootloader. After that, program the board through the probe, or re-flash the bootloader (see [[#Restoring bootloaders]]).

## ST-Link + OpenOCD

### Flash
```sh
openocd -f interface/stlink.cfg -f target/nrf52.cfg \
        -c "program build/zephyr/zephyr.hex verify reset exit"
```

From Zephyr, if the board supports the OpenOCD runner:
```sh
west flash --runner openocd
```

### Debug
```sh
# terminal 1 — GDB server on :3333
openocd -f interface/stlink.cfg -f target/nrf52.cfg

# terminal 2
arm-none-eabi-gdb build/zephyr/zephyr.elf -ex "target extended-remote :3333"
```
In **VS Code**, use the **Cortex-Debug** extension (`"servertype": "openocd"`, configFiles `interface/stlink.cfg`, `target/nrf52.cfg`). Nordic's VS Code extension expects a J-Link.

### RTT logging (optional)
OpenOCD has an RTT server. With the GDB server running:
```
# in an OpenOCD telnet session (telnet localhost 4444)
rtt setup 0x20000000 0x40000 "SEGGER RTT"
rtt start
rtt server start 9090 0
```
Then `nc localhost 9090`. Zephyr: `CONFIG_USE_SEGGER_RTT=y`, `CONFIG_LOG_BACKEND_RTT=y`. The simple fallback is logging over UART or USB CDC.

### pyOCD alternative
```sh
pyocd flash -t nrf52840 build/zephyr/zephyr.hex
pyocd gdbserver -t nrf52840
```
pyOCD may need the nRF52840 target pack: `pyocd pack install nrf52840`.

## Raspberry Pi Debug Probe (CMSIS-DAP)

A standard **CMSIS-DAP** probe. Everything in [[#ST-Link + OpenOCD]] works the same way, with a different interface config. Advantages over the ST-Link:
- **Full low-level debug access**, so unlocking a locked chip works reliably → [[#Locked chip (APPROTECT)]]
- A built-in **USB-UART bridge** (`/dev/ttyACM*`) for firmware logs, so no separate serial adapter is needed.

### Wiring
The probe has two 3-pin JST-SH connectors:

| Probe port | Pins | Connect to |
|---|---|---|
| **D** (debug) | SC (SWCLK), GND, SD (SWDIO) | Target SWD pads (see [[#SWD wiring]]) |
| **U** (UART) | TX, GND, RX | Target UART (probe TX → target RX, probe RX → target TX) |

- The probe's I/O runs at 3.3 V, matching the nRF52840 boards. It has no voltage-sense pin, so only GND and the signals are needed. Power the target from its own USB.
- There's no reset wire. Resetting over SWD (`reset` in OpenOCD) works fine for the nRF52.

### OpenOCD
```sh
# flash
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg \
        -c "program build/zephyr/zephyr.hex verify reset exit"

# GDB server on :3333 (then connect GDB as in the ST-Link "Debug" section)
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg

# unlock a locked chip (full erase)
openocd -f interface/cmsis-dap.cfg -f target/nrf52.cfg -c "init; nrf52_recover; exit"
```
Cortex-Debug in VS Code: same as for the ST-Link, with configFiles `interface/cmsis-dap.cfg` and `target/nrf52.cfg`. OpenOCD's RTT server works the same way ([[#RTT logging (optional)]]).

### pyOCD
```sh
pyocd flash -t nrf52840 build/zephyr/zephyr.hex   # probe detected automatically
pyocd gdbserver -t nrf52840
```

### probe-rs (alternative)
A newer single-binary tool with flashing, RTT and unlocking built in. It also supports the ST-Link.
```sh
probe-rs download --chip nRF52840_xxAA build/zephyr/zephyr.hex    # flash
probe-rs reset --chip nRF52840_xxAA
probe-rs erase --chip nRF52840_xxAA --allow-erase-all              # unlock a locked chip
probe-rs attach --chip nRF52840_xxAA build/zephyr/zephyr.elf       # RTT log viewer
probe-rs gdb --chip nRF52840_xxAA                                  # GDB server
```
Install with the one-line installer from probe-rs.rs, or `cargo install probe-rs-tools`. It needs its udev rules (from the probe-rs docs) to access probes without root.

### UART logs
```sh
picocom -b 115200 /dev/ttyACM1   # the probe's UART port; check `ls /dev/ttyACM*` to see which one it is
```
Zephyr: enable the UART console on the pins wired to the probe (board overlay), e.g. `CONFIG_CONSOLE=y`, `CONFIG_UART_CONSOLE=y`.

## Locked chip (APPROTECT)

Newer nRF52840 chips **lock debug access by default** unless the running firmware unlocks it. Symptoms: the probe connects but can't halt, read or write memory.

**Recover** = full chip erase through Nordic's CTRL-AP. This erases everything, including the bootloader and UICR.

1. Try OpenOCD's built-in helper. This needs the **DAP-mode** ST-Link interface (`stlink-dap.cfg`), not the HLA one, and a recent OpenOCD (0.12+):
   ```sh
   openocd -f interface/stlink-dap.cfg -f target/nrf52.cfg -c "init; nrf52_recover; exit"
   ```
2. If `nrf52_recover` doesn't exist in your OpenOCD, write the CTRL-AP registers directly (AP 1: `RESET` = 0x000, `ERASEALL` = 0x004, `ERASEALLSTATUS` = 0x008):
   ```
   nrf52.dap apreg 1 0x04 0x01   ; start ERASEALL
   sleep 500                      ; (poll 0x08 until 0)
   nrf52.dap apreg 1 0x04 0x00
   nrf52.dap apreg 1 0x00 0x01   ; reset
   nrf52.dap apreg 1 0x00 0x00
   ```
3. If the ST-Link can't do either one (old V2 or clone firmware without DAP mode), use a **CMSIS-DAP probe** (Raspberry Pi Debug Probe) for the recover, then go back to the ST-Link. See [[#Raspberry Pi Debug Probe (CMSIS-DAP)]] for the `openocd` and `probe-rs` commands.

**Keep debug unlocked during development:** the firmware must not re-lock the chip on boot. In Zephyr / NCS this is set by a Kconfig choice (`CONFIG_NRF_APPROTECT_USE_UICR` default, `CONFIG_NRF_APPROTECT_LOCK`, `CONFIG_NRF_APPROTECT_USER_HANDLING`). Check the options for your SDK version and keep debug access open until release.

## Pro Micro nRF52840 — UF2 bootloader (no probe)

Our clones run the **nice!nano UF2 bootloader** (Adafruit nRF52 bootloader, confirmed 2026-09-29). The `uf2` board variant builds a `zephyr.uf2` and places the app after the SoftDevice area the bootloader expects.

1. **Enter the bootloader:** short **RST to GND twice** quickly (or double-press the reset button if the clone has one). The LED fades in and out, and a USB drive appears (usually named `NICENANO`).
2. **Flash**, either way:
   ```sh
   cd firmware/xbx-nrf
   west build -b promicro_nrf52840/nrf52840/uf2
   west flash                 # uf2 runner: copies zephyr.uf2 to the mounted drive
   ```
   or drag `build/<app>/zephyr/zephyr.uf2` onto the drive (`cp` works too). The board reboots into the app when the copy finishes.
3. The app's USB serial console appears as `/dev/ttyACM*` (the board's default console is USB CDC ACM).

`west flash` needs the drive **mounted** (desktop auto-mount, or `udisksctl mount -b /dev/sdX`).

> [!note] P0.13 = external VCC switch
> On nice!nano-style boards, **P0.13** switches the power on the external `VCC` pin (**active low**: low = on). It doesn't matter for the radio test, but when powering sticks or sensors from `VCC`, drive P0.13 low in firmware, or power them from `3.3V` instead. Check your clone's pinout.

## nRF52840 Dongle (PCA10059) — USB DFU (no probe)

1. **Press the sideways RESET button**. The red LED pulses, meaning the bootloader is active.
2. Make a DFU package and flash it with `nrfutil`:
   ```sh
   nrfutil install nrf5sdk-tools

   nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req=0x00 \
       --application build/zephyr/zephyr.hex --application-version 1 dongle.zip

   nrfutil nrf5sdk-tools dfu usb-serial -pkg dongle.zip -p /dev/ttyACM0
   ```
   The GUI alternative is the **nRF Connect for Desktop → Programmer** app.

> [!note] Dongle memory layout
> The Zephyr board definition leaves room for the Nordic bootloader (the application doesn't start at 0x0). If the bootloader has been erased by SWD flashing, build for the **bare** variant (`nrf52840dongle/nrf52840/bare`, present in SDK v3.4.1). Otherwise the app won't boot.

## Restoring bootloaders

- **Pro Micro (nice!nano clone):** flash the nice!nano bootloader hex (e.g. `nice_nano_bootloader-0.9.2_s140_6.1.1.hex` from the nice!nano / Adafruit nRF52 bootloader releases) over SWD after a full erase:
  ```sh
  openocd -f interface/stlink.cfg -f target/nrf52.cfg \
          -c "init; reset halt; nrf5 mass_erase; program nice_nano_bootloader-0.9.2_s140_6.1.1.hex verify reset exit"
  ```
- **PCA10059:** the open bootloader hex is in the nRF5 SDK (`examples/dfu/open_bootloader/pca10059_usb_debug`). Flash it over SWD.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| ST-Link: "target voltage 0 V" / won't connect | Target 3.3 V not connected to the probe's voltage-sense pin |
| Connects, but can't halt or read memory | Chip locked → [[#Locked chip (APPROTECT)]] |
| Flashed over SWD, but the app doesn't run on the dongle | Build uses the bootloader offset but the bootloader is gone → build for the bare variant |
| No UF2 drive appears | RST-to-GND double tap too slow; charge-only USB cable |
| `west flash` can't find the UF2 drive | Drive not mounted → mount it, or copy `zephyr.uf2` by hand |
| No `/dev/ttyACM*` after flashing | App crashed early, or user not in the `dialout` group |
| Board overlay has no effect | Wrong overlay filename → see the board overlay note under [[#Hardware]] |
