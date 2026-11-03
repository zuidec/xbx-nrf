# xbx-nrf

A replacement bottom board for the **Xbox Series X|S controller (model 1914)**
built on a Nordic **nRF52840**, with a **2.4 GHz USB dongle** instead of Xbox
Wireless / Bluetooth. The stock top board and shell stay.

> **Status:** early design and prototyping. Open tasks: [todo.md](todo.md).


## Who is this for

This is for those of us who love the Xbox controller's shape but hate how bad 
the bluetooth connectivity is. This started as a labor of love for my partner 
who can't find a controller she likes besides the Xbox controller but also can't 
stand the disconnects.

That being said, there are some real concessions. The most obvious is that the 
controller won't work with an Xbox anymore. Another concession is that I skipped 
the wiring for the rechargeable battery backs, as well as the expansion port and 
headphone jack.

> **[Getting started](docs/getting_started.md)**

## Architecture

```
┌─────────── Controller ───────────┐          ┌──── Dongle ────┐
│ Stock top board ─J3─ new board   │ 2.4 GHz  │ nRF52840 + USB │
│ buttons (PCAL6416)  nRF52840     │◀────────▶│ XInput / HID   │──▶ PC
│ power latch, boost  radio,       │   ESB    │ gamepad        │
│ USB-C, 2×AA input   sticks,      │          │                │
│                     triggers,    │          │                │
│                     rumble       │          │                │
└──────────────────────────────────┘          └────────────────┘
```

## Design decisions 

| Area | Decision |
|---|---|
| Target | Xbox Series X\|S (1914)|
| Scope | New bottom board, stock top board, shell, triggers, motors reused |
| MCU | nRF52840 for the controller and dongle |
| Wireless | NRF ESB 2.4 GHz |
| Multiplayer | Up to 7 paired, 4 connected |
| Polling rate| 1 kHz with 1–2 controllers, 500 Hz with 3–4 |
| Dongle USB | HID gamepad (1 player) and XInput |
| Battery | **AAs only** - basic rechargeables OK, but no Plug N Play |
| Rumble | reuses all the stock motors including triggers |
| Buttons | reuses stock with options for AUX trigger switches and 4 paddles |
| Audio | None in v1 |
| Expansion port | None in v1 |

> [!WARNING]
> **Never put a bare Li-ion cell on the AA contacts**: The boost converter
> appears to pass that straight through and the nrf can only handle 3v6
> ([docs/hardware.md](docs/hardware.md)).

## Hardware

The custom board plugs into the stock top board's 14-pin **J3**: power rails, 
USB, I2C to the button expander, and Guide / Pair / power-hold signals.

| Doc | Covers |
|---|---|
| [docs/hardware.md](docs/hardware.md) | Top board power, J3 pinout, antennas, dongle pin map |
| [docs/pcal6416.md](docs/pcal6416.md) | Button expander: button map, registers |
| [docs/a3910.md](docs/a3910.md) | Rumble drivers: wiring, logic table |
| [docs/teardown.md](docs/teardown.md) | Test controller teardown: motor wires |

## Firmware

Built using the nRF Connect SDK. Version is pinned at v3.4.1 to ensure future 
buildability (hopefully). The controller firmware has a target for the custom 
board and the dongle firmware has a target for my custom board as well as the 
PCA10059 dongle. There is a nice nano/pro micro target for the controller, but I 
just use this for testing.


| Doc | Covers |
|---|---|
| [docs/controller.md](docs/controller.md) | Using it: power, pairing, LED, wired modes, calibration, waking the PC |
| [docs/building.md](docs/building.md) | SDK setup, versions, build script |
| [docs/flashing.md](docs/flashing.md) | Flashing and debugging |
| [docs/protocol.md](docs/protocol.md) | Radio (pairing, time slots) and USB (HID, XInput) protocols |
| [docs/gip.md](docs/gip.md) | Xbox GIP protocol notes (optional mode) |


## Configurator

Desktop app for settings, calibration, input and rumble tests and firmware
updates of both devices over USB using Python + Tkinter. So far only tested on 
Linux, but in theory should be buildable on Mac and Windows.

```sh
python3 tools/configurator/xbx-config.py  # run from source
tools/configurator/build.sh --release     # one file, in output/
```

Details: [docs/configurator.md](docs/configurator.md).

## Repository layout

```
docs/                project notes (Obsidian format)
firmware/
  boards/            our board definitions (xbx_controller, xbx_dongle)
  bootloader/        UF2 bootloader build (upstream + our boards)
  common/            shared code (include/protocol.h)
  xbx-nrf/           controller app
  dongle/            dongle app
  ncs-version        pinned SDK + toolchain
  check-sdk.sh       version check for build.sh
  build.sh           build (+ flash) script
pcb/xbx-nrf/         KiCad project, controller board (lib/: symbols,
                     footprints, 3D)
pcb/xbx-nrf-dongle/  KiCad project, dongle board
mech/                FreeCAD parts (controller-bottom-board/: board outline)
references/          schematics, scans, datasheets, pinouts
tools/               configurator/, test tools (test-rumble.sh)
todo.md              task list (Obsidian)
```

## Disclosures

This was built with the help of Claude Code. PCB designs are completely hand 
designed by myself, and all code was either written or reviewed by myself. There 
may however be some things that I missed as is the nature when using AI.

## References

- Schematics, scans, pinouts and datasheets:
  [references/README.md](references/README.md) (third-party, not in the repo;
  source links there)
- [AcidMods — XB1 controller PCB scans](https://acidmods.com/forum/index.php?topic=44547.120)
- [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) (XInput
  reference)

## License

Copyright (c) 2026 zuidec. Firmware and scripts: **MPL-2.0**. Hardware
design: **CERN-OHL-S-2.0**. Documentation: **CC-BY-4.0**. Third-party CAD
parts (SnapMagic, KiCad libraries) keep their CC-BY-SA-4.0 licenses; see the
`ATTRIBUTION.md` in each library folder. Details: [LICENSE](LICENSE),
[REUSE.toml](REUSE.toml), full texts in [LICENSES/](LICENSES/).
