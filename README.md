# xbx-nrf

A replacement bottom board for the **Xbox Series X|S controller (model 1914)**
built on a Nordic **nRF52840**, with a **2.4 GHz USB dongle** instead of Xbox
Wireless / Bluetooth: a low-latency proprietary link, like 8BitDo or Logitech
Lightspeed. The stock top board and shell stay.

> **Status:** early design and prototyping. Open tasks: [todo.md](todo.md).

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

## Design decisions (locked)

| Area | Decision |
|---|---|
| Target | Xbox Series X\|S (1914); Elite Series 2 (1797) maybe later |
| Scope | New **bottom board only**; stock top board, shell, triggers, motors reused |
| MCU | **nRF52840** modules on the controller and the dongle |
| Audio | **None in v1** |
| Wireless | **ESB** 2.4 GHz with our own pairing and time slots; rumble/LED in ACK payloads |
| Multiplayer | One dongle: up to **7 paired**, **4 connected**; 1 kHz with 1–2, 500 Hz with 3–4 (time slots) |
| Dongle USB | **HID gamepad** (1 player; first) and **XInput** as an Xbox 360 Wireless Receiver (1–4 players, hot-plug), switchable. No Xbox console support |
| Battery | **Rechargeable AAs only** (NiMH or regulated 1.5 V Li-ion AAs) |
| Power | Stock top board latch and boost converters, via J3 |
| Rumble | 2× **Allegro A3910**, stock wiring, 8 GPIOs |
| Buttons | Mostly via the top board's **PCAL6416** I2C expander |

> [!WARNING]
> **Never put a bare Li-ion cell on the AA contacts**: the stock boost
> converters pass it straight to the 3.3 V rail
> ([docs/hardware.md](docs/hardware.md)).

## Hardware

Our board plugs into the stock top board's 14-pin **J3**: power rails, USB, I2C
to the button expander, and Guide / Pair / power-hold signals.

| Doc | Covers |
|---|---|
| [docs/hardware.md](docs/hardware.md) | Top board power, J3 pinout, antennas, dongle pin map |
| [docs/pcal6416.md](docs/pcal6416.md) | Button expander: button map, registers |
| [docs/a3910.md](docs/a3910.md) | Rumble drivers: wiring, logic table |

## Firmware

Two Zephyr apps, `firmware/xbx-nrf` (controller) and `firmware/dongle`, on the
**nRF Connect SDK v3.4.1** (pinned). Prototyped on Pro Micro nRF52840 boards
and the nRF52840 Dongle: pairing, up to 4 controllers (tested with 2), HID and
XInput.

```sh
cd firmware
./build-unsigned.sh xbx-nrf [-f <device>]  # development build (+ flash)
ncs-shell                                  # SDK environment + signing keys
./build-signed.sh dongle <board>           # signed release build
```

| Doc | Covers |
|---|---|
| [docs/building.md](docs/building.md) | SDK setup, versions, build scripts |
| [docs/flashing.md](docs/flashing.md) | Flashing and debugging |
| [docs/protocol.md](docs/protocol.md) | Radio (pairing, time slots) and USB (HID, XInput) protocols |
| [docs/gip.md](docs/gip.md) | Xbox GIP protocol notes (optional mode) |
| [docs/signing.md](docs/signing.md) | Firmware signing (MCUboot, keys) |

## Repository layout

```
docs/                project notes (Obsidian)
firmware/
  boards/            our board definitions (xbx_dongle)
  common/            shared code (include/protocol.h)
  xbx-nrf/           controller app
  dongle/            dongle app
  ncs-version        pinned SDK + toolchain
  check-sdk.sh       version check for the build scripts
  build-unsigned.sh  development build
  build-signed.sh    signed release build
pcb/xbx-nrf/         KiCad project, controller board (lib/: symbols,
                     footprints, 3D)
pcb/xbx-nrf-dongle/  KiCad project, dongle board
mech/                FreeCAD parts (controller-bottom-board/: board outline)
references/          schematics, scans, datasheets, pinouts
tools/               test tools (test-rumble.sh)
todo.md              task list (Obsidian)
```

## References

- Schematics, scans, pinouts and datasheets:
  [references/README.md](references/README.md) (third-party, not in the repo;
  source links there)
- [AcidMods — XB1 controller PCB scans](https://acidmods.com/forum/index.php?topic=44547.120)
- [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) (XInput
  reference)

## License

Copyright (c) 2026 zuidec. Firmware and scripts: **MIT**. Hardware design:
**CERN-OHL-S-2.0**. Documentation: **CC-BY-4.0**. Third-party CAD parts
(SnapMagic, KiCad libraries) keep their CC-BY-SA-4.0 licenses; see the
`ATTRIBUTION.md` in each library folder. Details: [LICENSE](LICENSE),
[REUSE.toml](REUSE.toml), full texts in [LICENSES/](LICENSES/).
