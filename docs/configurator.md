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
controller and the dongle. Controller and dongle tabs and the CLI work, from
source or as one Linux file ([[#Building]]). Steps: [[todo#Configurator]].

## Decisions

- **Python + Tkinter (ttk)**, one file per OS with PyInstaller (Nuitka if
  Windows antivirus flags it). Tkinter is in Python's standard library.
  Linux talks to `hidraw` directly (standard library only); Windows and
  macOS use `hidapi` (bundled). Python and C are both
  reviewable here; Rust was ruled out for that reason, a web page for needing
  a Chromium browser.
- **Direct USB only** for now: the controller is configured over its own USB
  port, not through the dongle. Relaying over the radio can come later
  without changing the commands.
- **Vendor HID interface** on both devices, in every USB mode: no driver on
  any OS ([[#USB channel]]).
- Same code as a **CLI** (`info`, `bootloader`, …) for development and
  tests.

## App

Code: `tools/configurator/`, one code base for every OS. **Linux first.**
Run from source: `python3 tools/configurator/xbx-config.py` (the window), or
with a command for the CLI (`--help`). Files the app writes onto the user's
system (e.g. the udev rule) are kept as the real files in
`tools/configurator/files/`, exactly as installed; `build.sh` bundles them,
so fixing one means editing that file.

| Module (`xbxconfig/`) | Does |
|---|---|
| `protocol.py` | Message layouts, ids, constants |
| `transport.py` | Finding and opening the interface (`hidraw` / `hidapi`) |
| `device.py` | Requests; a reader thread splits responses from `STREAM` reports |
| `uf2.py` | File check (marker), bootloader drive, the update |
| `files.py` | Bundled files, udev rule install and uninstall |
| `cli.py`, `gui/` | CLI; window (`app.py`, `panels.py`, `widgets.py`) |

Finds devices by VID `0x1209`, the app PIDs (`0x0001`–`0x0004`) and the
channel's usage page; lists every one found.

### Controller tab

| Section | Does |
|---|---|
| Info | Firmware version, USB mode, serial, board, link (paired or not) |
| Input test | Live buttons, paddles, sticks (raw and calibrated, with the range), triggers, battery |
| Rumble test | Each motor at a chosen level for a moment |
| Stick calibration | Guided: centre at rest → sweep → save, with live coverage; cancel, erase |
| Trigger calibration | Guided: rest → full pull → save; alone or with the sticks |
| Paddles | On / off; XInput binding per paddle (any button or none) |
| LED brightness | Guide LED, 1–255 (can't be off: it shows pairing and the link) |
| Factory reset | Settings, calibration, pairing: each or all |
| Firmware update | Enter the bootloader, copy the `.uf2`, wait for the restart |

### Dongle tab

| Section | Does |
|---|---|
| Info | Firmware version, USB mode (SW3), serial, paired controllers (suggested) |
| LED brightness | Pair, player and power LEDs, 1–255 each; player and power can be switched off, pair can't. A change lights that group for 1.5 s (the pair LED is off outside pairing) |
| Factory reset | Settings (LED brightness), pairing (forget all controllers; also: Pair held 10 s): each or both |
| Firmware update | As the controller |

### Firmware update

The user always picks the `.uf2` file; the app ships no firmware.

1. `BOOTLOADER` command; the device restarts into the UF2 bootloader.
2. The app waits for the drive (`XBXCTRL` / `XBXDONGLE`) and checks that
   the file is for that device, so a dongle image can't go on a controller
   ([[#Image marker]]).
3. Copies the `.uf2` and syncs it; the bootloader restarts into the app,
   which the app finds again by serial and checks that it runs the file's
   version.

Linux: a drive the desktop didn't mount is mounted with `udisksctl` (no root)
and unmounted again after the sync, never before; without `udisksctl` the
app asks the user to mount it.

### Platforms

- **Linux:** a udev rule (`files/70-xbx-nrf.rules`) gives the user the
  `hidraw` nodes. The app detects missing access and offers to install it
  into `/etc/udev/rules.d/` (`pkexec`, or `sudo` from the CLI).
- **macOS:** unsigned: right-click → Open the first time.
- **Windows:** no driver needed.
- **Uninstall:** File → Remove installed files (CLI `uninstall`) removes what
  the app put on the system: only the Linux udev rule; Windows and macOS
  have nothing. Then delete the app file. Firmware updates write only to
  the device's drive, and the one-file build unpacks into a temporary
  folder that it deletes on exit.

### Building

`tools/configurator/build.sh` builds one file into
`tools/configurator/output/`, named for the version, OS and CPU; work files
(venvs, PyInstaller's) stay in `build/`. The version comes first: a name
ending in `.1` reads as a man page to file managers, which then won't run it.

| Command | File | Runs on |
|---|---|---|
| `build.sh --release` (Linux) | `xbx-config-0.2.2-linux-x86_64` | Debian 12 (glibc 2.36) and newer: built in a Debian 12 container |
| `build.sh` (Linux) | `xbx-config-0.2.2-linux-x86_64-local` | This system and any with the same or newer glibc |
| `build.sh` (Windows, macOS) | `xbx-config-0.2.2-windows-x86_64.exe`, `xbx-config-0.2.2-macos-arm64` | That OS |

- Needs Python 3 with tkinter and venv, and `objdump` on Linux; `--release`
  needs only podman or docker. It checks first and lists what's missing.
- PyInstaller is pinned in `requirements-build.txt` and installed into the
  build's own venv. `-p` starts the venv over and deletes earlier versions
  of the same file (same OS, CPU and kind); other builds in `output/` stay.
  The bundle's contents: `xbx-config.spec`; the container: `Containerfile`.
- Linux: fontconfig and freetype aren't bundled; the system's are used (a
  bundled older fontconfig warns about newer config files).
- PyInstaller doesn't cross-build: each OS and CPU builds its own. Linux
  tested; Windows (Git Bash) and macOS later.

## USB channel

One extra HID interface on the controller and the dongle, present in HID
mode, XInput mode and with the console. It's a vendor collection, so hosts
don't treat it as a gamepad (Linux: `hidraw` only, no input device; SDL and
Steam ignore it).

| Item | Value |
|---|---|
| Report descriptor | Usage page `0xFF00`, usage `0x01`, application collection; input 64 bytes, output 64 bytes, no report IDs |
| Endpoints | Interrupt IN and OUT, 64 bytes, 10 ms |
| Firmware | `common/src/config_usb.c` with each app's `src/configurator.c`, handled on the system work queue |

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

One request at a time: a request sent before the previous response arrived
is dropped. Input-test data comes unrequested as `STREAM` reports (command
`0x80`) while streaming is on.

### Commands

| Code | Command | Device | Arguments → result |
|---|---|---|---|
| `0x01` | `INFO` | Both | → [[#INFO result]] |
| `0x02` | `SETTING_GET` | Both | id → id, value ([[#Settings]]); id 0 → count, the ids this firmware has |
| `0x03` | `SETTING_SET` | Both | id, value → applied at once, saved to flash 1 s after the last change (sliders don't wear flash) |
| `0x04` | `STREAM` | Controller | rate in Hz (0 = off, ≤ 100); stops when the interface goes down ([[#STREAM report]]) |
| `0x05` | `CALIBRATE` | Controller | step, targets → [[#CALIBRATE result]] |
| `0x06` | `RUMBLE` | Controller | levels heavy, weak, LT, RT (u8), duration ms (u16 LE, ≤ 2000, 0 stops); overrides the host while it runs |
| `0x07` | `FACTORY_RESET` | Both | scope bits: `0x01` settings (with the stored USB mode), `0x02` calibration, `0x04` pairing; then restart. Dongle: settings and pairing |
| `0x08` | `BOOTLOADER` | Both | → ok, then `GPREGRET = 0x57` and reset |
| `0x09` | `REBOOT` | Both | → ok, then reset |

Resets come 100 ms after the response, after pending settings are written.

### INFO result

| Byte | Field |
|---|---|
| 3–4 | Channel version major, minor |
| 5 | Device: 1 controller, 2 dongle |
| 6–8 | Firmware version major, minor, patch |
| 9 | USB mode: 0 HID, 1 XInput |
| 10–17 | Serial (chip DEVICEID, as the USB serial number) |
| 18– | Board target, NUL-terminated (e.g. `xbx_controller/nrf52840/uf2`) |

### STREAM report

Command `0x80`, byte 1 counts reports, byte 2 is 0. Values LE.

| Byte | Field |
|---|---|
| 3–4 | `buttons` (`XBX_BTN_*`) |
| 5 | `buttons_ext` (`XBX_BTN_EXT_*`) |
| 6–13 | lx, ly, rx, ry (i16, shaped) |
| 14–17 | lt, rt (u16, 0–1023) |
| 18–29 | Raw ADC counts lx, ly, rx, ry, lt, rt (i16) |
| 30 | Calibration state: 0 idle, 1 measuring rest, 2 sweep, 3 saved |
| 31 | Flags: `0x01` wired, `0x02` linked to the dongle, `0x04` calibration save refused |
| 32–33 | Battery mV (0 = unknown) |
| 34 | Battery % (`0xFF` = unknown) |
| 35–38 | Motor levels heavy, weak, LT, RT |

Buttons and axes are the report before the XInput paddle mapping;
`buttons_ext` includes Pair (input test only: the dongle ignores it). Live
coverage while calibrating comes from the raw counts.

### CALIBRATE

The on-device routine ([[todo#Stick and trigger calibration]]), driven by
the app; the button combos still work.

| Step | Does | Allowed in |
|---|---|---|
| 0 | Status only | Any state |
| 1 | Start; targets `0x01` sticks, `0x02` triggers, 0 = all there are | Idle |
| 2 | Save (refused: an axis is flagged, keep going) | Sweep |
| 3 | Cancel (previous calibration back) | Rest, sweep |
| 4 | Erase (back to auto-ranging) | Idle |

Wrong state: "busy"; triggers without analog triggers: "unsupported here".
Saving keeps the stored axes not calibrated now. A trigger is refused when
its pull is under 1/16 of the ADC range, or when it moved both ways from
rest (pulled while the rest was measured). It reports full from a share of
its travel (setting `0x05`, default 95 %), so a pull a little short still
reaches full.

### CALIBRATE result

| Byte | Field |
|---|---|
| 3 | State (as in the stream) |
| 4 | Targets being calibrated |
| 5 | Axes the last save refused (bit per axis: lx, ly, rx, ry, lt, rt) |
| 6 | Axes stored in flash (same bits) |
| 7 | LT full pull threshold, % of the travel |
| 8–43 | Per axis min, centre, max (i16 LE, raw counts; triggers: centre = rest). While sweeping: seen so far |
| 44 | RT full pull threshold, % |
| 45 | Stick axes reported against their counts (same bits; 0 before 0.8.1) |

### Settings

Each setting has an id and a fixed-size value, so app and firmware of
different versions agree on what both know; unknown ids answer "unsupported
here". Stored in flash 1 s after the last change: the controller's
`config.c` (one versioned entry), the dongle's `configurator.c` (`cfg/leds`).

LED levels (`common/include/led_level.h`): 1–255 spread over a visible
range (level 1 = 24 of 255, so no level looks off), gamma 2 so the steps
look even; off is a separate switch.

| Id | Setting | Device | Value |
|---|---|---|---|
| `0x01` | Paddles on | Controller | u8 (0 / 1) |
| `0x02` | Paddle XInput bindings | Controller | 4 × u16 LE `XBX_BTN_*`, P1–P4 (0 = none) |
| `0x03` | Guide LED brightness | Controller | u8 1–255 (default 255) |
| `0x05` | Trigger full pull threshold | Controller | 2 × u8 %, LT, RT (50–100; default 95): calibrated triggers report full from there |
| `0x06` | LED brightness | Dongle | 4 × u8: pair, player, power (1–255, default 255); on bits `0x01` player, `0x02` power (default both) |

### Versioning

`INFO` carries the channel version (major.minor). The app refuses a different
major version and hides what a lower minor version lacks.

## Image marker

Both `.uf2` files carry the same family ID (`0xada52840`, which the
bootloader requires), so each firmware build carries a 12-byte marker naming
the device. Its address varies between builds: the app joins the file's UF2
payloads and searches for the magic, then refuses a mismatch.

| Byte | Field |
|---|---|
| 0–7 | Magic `XBXIMG` `01` `00` (marker version 1) |
| 8 | Device: 1 controller, 2 dongle |
| 9–11 | Firmware version major, minor, patch |

`config_usb.c` defines it in its own section; `common/image_marker.ld` keeps
it (otherwise the compiler folds it into `INFO` and drops it). Each app sets
`XBX_DEVICE` in its CMakeLists.
