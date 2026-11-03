---
title: Getting started
created: 2026-11-02
updated: 2026-11-02
tags:
  - xbx-nrf
  - howto
aliases:
  - getting started
  - quick start
---

# Getting started

Quick start with the pre-built files: flash the dongle and controller,
then set them up. Assumes the boards are built and have our UF2 bootloader
(installed once over SWD, [[docs/flashing#Custom boards — UF2 bootloader]]).
Building from source: [[docs/building]].

## Downloads

From the [releases page](https://github.com/zuidec/xbx-nrf/releases), the
newest of each:

| File | For |
|---|---|
| `xbx-dongle-<version>.uf2` | Dongle firmware |
| `xbx-controller-<version>.uf2` | Controller firmware |
| `xbx-config-<version>-linux-x86_64` | Configurator, Linux |

`SHA256SUMS` lists the checksums.

## Linux

1. **Configurator:** make it executable and start it:
   ```sh
   chmod +x xbx-config-*-linux-x86_64
   ./xbx-config-*-linux-x86_64
   ```
   Runs on Debian 12 or newer (glibc 2.36).
2. **Install files:** File → Install files, tick both:
   - **udev rule** (admin password): access to the devices without root,
     and the dongle and wired controller can wake the PC.
   - **SDL mapping:** Share and the paddles in Steam and SDL games, from the
     next login.
3. **Dongle:** plug it in.
   - Has firmware: Dongle tab → Firmware update, pick the dongle `.uf2`.
   - Bootloader only: the `XBXDONGLE` drive appears by itself; copy the
     `.uf2` onto it.
4. **Controller:** plug it in over USB.
   - Has firmware: Controller tab → Firmware update, pick the controller
     `.uf2`.
   - Bootloader only: copy the `.uf2` onto the `XBXCTRL` drive.
5. **Pair:** never-paired devices pair by themselves: replug the dongle,
   unplug the controller and press **Guide** within 30 s. Otherwise press
   the dongle's button and hold the controller's **Pair** ~3 s
   ([[docs/controller#Pairing]]).
6. **Calibrate:** Controller tab (over USB) → calibration, or on the
   controller alone ([[docs/controller#Calibration]]).

Log out and back in for the SDL mapping. File → Remove installed files
takes both files out again.

## Windows

No configurator build for Windows yet; flash by hand. Nothing to install:
no driver.

1. **Dongle:** hold **Pair** while plugging it in (a dongle with only the
   bootloader skips this). Copy the dongle `.uf2` onto the `XBXDONGLE`
   drive; it restarts by itself.
2. **Controller:** hold **LS + RS** while plugging it in, then copy the
   controller `.uf2` onto the `XBXCTRL` drive.
3. **Pair and calibrate:** as on Linux, steps 5 and 6; calibration on the
   controller alone ([[docs/controller#Calibration]]).

The configurator runs from source on Windows: [[docs/building#Windows]].

## macOS

Not tested yet, I don't own a Mac. If you'd like to try it on a Mac, open an 
issue on [GitHub](https://github.com/zuidec/xbx-nrf/issues).

## Next

Power, LEDs, wired and wireless, trigger effects: [[docs/controller]].
