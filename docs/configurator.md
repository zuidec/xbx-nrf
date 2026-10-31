---
title: Configurator
created: 2026-10-31
updated: 2026-10-31
tags:
  - xbx-nrf
  - firmware
  - tools
  - decision
aliases:
  - configurator
---

# Configurator

Desktop app for settings, calibration, testing and firmware updates of the
controller and the dongle. **Design, not built yet.** Steps:
[[todo#Configurator]].

## Decisions

- **Python + Tkinter (ttk)**, one file per OS with PyInstaller (Nuitka if
  Windows antivirus flags it). Tkinter is in Python's standard library; the
  only other dependency is `hidapi` (bundled). Python and C are both
  reviewable here; Rust was ruled out for that reason, a web page for needing
  a Chromium browser.
- **Direct USB only** for now: the controller is configured over its own USB
  port, not through the dongle. Relaying over the radio can come later
  without changing the commands.
- **Vendor HID interface** on both devices, in every USB mode: no driver on
  Windows or macOS, `hidapi` everywhere ([[#USB channel]]).
- Same code as a **CLI** (`info`, `bootloader`, …) for development and
  tests.

## App

Code: `tools/configurator/` (planned), one code base for every OS. **Linux
first.** Files the app writes onto the user's system (e.g. the udev rule) are
kept as the real files in `tools/configurator/files/`, exactly as installed;
`build.sh` bundles them, so fixing one means editing that file.

Finds devices by VID `0x1209`, the app PIDs (`0x0001`–`0x0004`) and the
channel's usage page; lists every one found.

### Controller tab

| Section | Does |
|---|---|
| Info | Firmware version, USB mode, serial, board, link (paired or not) |
| Input test | Live buttons, paddles, sticks (raw and calibrated, with the range), triggers, battery |
| Rumble test (suggested) | Each motor at a chosen level for a moment |
| Stick calibration | Guided: centre at rest → sweep → save, with live coverage; cancel, erase |
| Trigger calibration (suggested) | Rest and full pull (now auto-ranging at boot) |
| Paddles | On / off; XInput binding per paddle (any button or none) |
| LED brightness | Guide LED |
| USB mode (suggested) | Stored default, HID or XInput (now: X / B held at boot) |
| Factory reset | Settings, calibration, pairing: each or all |
| Firmware update | Enter the bootloader, copy the `.uf2`, wait for the restart |

### Dongle tab

| Section | Does |
|---|---|
| Info | Firmware version, USB mode (SW3), serial, paired controllers (suggested) |
| LED brightness | Pair, player and power LEDs |
| Factory reset (suggested) | Forget all controllers (now: Pair held 10 s) |
| Firmware update | As the controller |

### Firmware update

The user always picks the `.uf2` file; the app ships no firmware.

1. `BOOTLOADER` command; the device restarts into the UF2 bootloader.
2. The app waits for the drive (`XBXCTRL` / `XBXDONGLE`) and checks that
   the file is for that device, so a dongle image can't go on a controller
   ([[#Image marker]]).
3. Copies the `.uf2`; the bootloader restarts into the app, which the app
   finds again by VID / PID.

Linux: the drive must be mounted (desktops do it automatically); the app
reports when it isn't.

### Platforms

- **Linux:** a udev rule (`files/70-xbx-nrf.rules`) gives the user the
  `hidraw` nodes. The app detects missing access and offers to install it
  into `/etc/udev/rules.d/` (admin prompt).
- **macOS:** unsigned: right-click → Open the first time.
- **Windows:** no driver needed.
- `build.sh` builds one file into `build/`, named for the OS and CPU (e.g.
  `xbx-config-linux-x86_64`, `xbx-config-windows-x86_64.exe`). PyInstaller
  doesn't cross-build: each OS builds its own, Linux now; Windows and macOS
  later (their machines or CI).

## USB channel

One extra HID interface on the controller and the dongle, present in HID
mode, XInput mode and with the console. It's a vendor collection, so hosts
don't treat it as a gamepad (Linux: `hidraw` only, no input device; SDL and
Steam ignore it).

| Item | Value |
|---|---|
| Report descriptor | Usage page `0xFF00`, usage `0x01`, application collection; input 64 bytes, output 64 bytes, no report IDs |
| Endpoints | Interrupt IN and OUT, 64 bytes, 10 ms |
| Firmware | `config_usb.c` on both (planned), handled on the system work queue |

Endpoint budget: the dongle in XInput mode with the console uses 6 of the
nRF52840's 7 IN endpoints (4 slots + CDC); this one is the 7th.

### Messages

Every message is 64 bytes. Requests (PC → device, output report):

| Byte | Field |
|---|---|
| 0 | Command |
| 1 | Sequence number (echoed) |
| 2–63 | Arguments |

Responses (device → PC, input report), one per request:

| Byte | Field |
|---|---|
| 0 | Command (echoed) |
| 1 | Sequence number |
| 2 | Status: 0 ok, 1 unknown command, 2 bad argument, 3 busy, 4 unsupported here |
| 3–63 | Result |

Input-test data comes unrequested as `STREAM` reports (command `0x80`) while
streaming is on.

### Commands

| Code | Command | Device | Arguments → result |
|---|---|---|---|
| `0x01` | `INFO` | Both | → channel version, device type (controller / dongle), firmware version, board, serial, USB mode |
| `0x02` | `SETTING_GET` | Both | id → value ([[#Settings]]); id 0 → the ids this firmware has |
| `0x03` | `SETTING_SET` | Both | id, value → applied at once, saved to flash ~1 s after the last change (sliders don't wear flash) |
| `0x04` | `STREAM` | Controller | rate (0 = off, up to 100 Hz; 100 Hz is enough for the display) |
| `0x05` | `CALIBRATE` | Controller | step: start (centre at rest), save, cancel, erase |
| `0x06` | `RUMBLE` | Controller | 4 levels, duration (≤ 2 s) |
| `0x07` | `FACTORY_RESET` | Both | scope bits: settings, calibration, pairing; then restart |
| `0x08` | `BOOTLOADER` | Both | → ok, then `GPREGRET = 0x57` and reset |
| `0x09` | `REBOOT` | Both | → ok, then reset |

`STREAM` report: the radio input report (buttons, `buttons_ext`, sticks,
triggers), raw ADC counts per axis, calibration step and range, battery mV,
motor levels.

### Settings

Each setting has an id and a fixed-size value, so app and firmware of
different versions agree on what both know; unknown ids answer "unsupported
here". Stored by `config.c` (one versioned entry per device).

| Id | Setting | Device | Value |
|---|---|---|---|
| `0x01` | Paddles on | Controller | u8 (0 / 1) |
| `0x02` | Paddle XInput bindings | Controller | 4 × u16 `XBX_BTN_*` (0 = none) |
| `0x03` | LED brightness | Both | u8 (0–255) |
| `0x04` | Stored USB mode | Controller | u8 (0 HID, 1 XInput) |

### Versioning

`INFO` carries the channel version (major.minor). The app refuses a different
major version and hides what a lower minor version lacks.

## Image marker

Both `.uf2` files carry the same family ID (`0xada52840`, which the
bootloader requires), so each firmware build puts a small marker at a fixed
place in its image naming the device (controller / dongle). The app reads it
from the file before copying and refuses a mismatch. Layout: with the
firmware work.
