# xbx-nrf

A replacement bottom (MCU) board for the **Xbox Series X|S controller (model 1914)**, built on a Nordic **nRF52840**, paired with a custom **2.4 GHz USB dongle**. It replaces the stock Xbox Wireless / Bluetooth connection with a low-latency proprietary link, similar to how 8BitDo and Logitech Lightspeed devices work.

The stock **top board** (face buttons, bumpers, USB-C, power latch, voltage regulators) and the shell stay in place. Only the bottom board is replaced.

> **Status:** early design and reverse engineering. See [todo.md](todo.md) for open tasks.

## Architecture

```
┌──────────────── Controller ────────────────┐           ┌──────── Dongle ────────┐
│ Stock top board ──J3── New bottom board    │  2.4 GHz  │ nRF52840 + USB         │
│  buttons (PCAL6416)     nRF52840 + radio   │ ◀───────▶ │  XInput / HID gamepad  │ ──USB──▶ PC
│  power latch, boosts    sticks, triggers,  │ ESB/Gazell│                        │
│  USB-C, 2×AA input      rumble, B/LSC/RSC  │           │                        │
└────────────────────────────────────────────┘           └────────────────────────┘
```

## Design decisions (locked)

| Area | Decision |
|---|---|
| Target controller | Xbox Series X\|S, model 1914. An Elite Series 2 (1797) port may follow later |
| Scope | Replace the **bottom board only**; keep the stock top board, shell, sticks' mechanical parts, triggers, and motors |
| MCU | **nRF52840** on both the controller (pre-certified module) and the dongle (Nordic PCA10059 for prototyping) |
| Audio | **No headset audio in v1.** It would need a new board revision; the nRF5340 is not pin-compatible with the nRF52840 |
| Wireless | nRF52840 on both ends, **ESB/Gazell** proprietary 2.4 GHz link. Controller → dongle input reports at **1 kHz**; rumble/LED data returns in ACK payloads |
| Dongle USB | Two switchable modes: **HID gamepad** (built first; macOS, Android, browsers, Steam) and **XInput** (`045E:028E`; most Windows games). The radio link carries full-resolution data; the dongle reduces it per mode. Xbox consoles are **not** supported (no authentication) |
| Battery | **Rechargeable AA cells only** (NiMH, or USB-C Li-ion AAs with a regulated 1.5 V output) |
| Power switching | Reuse the top board's **hard power latch** and boost converters. The bottom board is powered from J3 |
| Rumble | 2× **Allegro A3910** half-bridge drivers, copying the stock wiring: 8 GPIOs (drive, brake and coast for each motor). See [docs/a3910.md](docs/a3910.md) |
| Button input | Most buttons are read over I2C from the top board's **PCAL6416** expander (U4). See [docs/pcal6416.md](docs/pcal6416.md) |

> [!WARNING]
> **Never connect a bare Li-ion cell to the AA contacts.** The top board's TPS613221 (3.3 V) and TPS613226 (3.6 V) are boost-only converters. A 4.2 V input passes through to the 3.3 V rail and exceeds the nRF's 3.6 V absolute maximum.

## Top board power (stock, reused)

| Part | Function |
|---|---|
| Q6/Q7 | Battery → `3v/B+` switch; isolates the AAs when USB is present |
| U5 TPS613221 | `3v/B+` → **3.3 V** (logic) |
| U6 TPS613226 | `3v/B+` → **3.6 V** (rumble supply) |
| U1 MP1476L | USB 5 V → `3v/B+`. With USB plugged in, the controller is powered regardless of the latch |
| SW13 + Q8 / SW2 + Q14 → Q9 | Guide or Pair press turns power on; the bottom board keeps it on via J3 pin 9 |

## J3 — top ↔ bottom board connector

| Pin | Signal | Direction (bottom board) | Notes |
|---|---|---|---|
| 1 | 3.6 V | Power in | Rumble motor supply |
| 2 | GU (Guide) via D27 | Input | Active-low; pull-up on the bottom board |
| 3 | 3v/B+ | Power in | Battery rail after the switch; battery voltage sense |
| 4 | 3.3 V | Power in | Logic supply for the nRF |
| 5 | SCL | Output | I2C to the PCAL6416 |
| 6 | LED control | Output | Guide LED (Q3 on the top board) |
| 7 | SDA | Bidirectional | I2C to the PCAL6416 |
| 8 | USB D+ | Bidirectional | From the top board's USB-C |
| 9 | ~RESET / power hold | Output | High = hold power on + release the expander reset. *Verify* |
| 10 | USB D− | Bidirectional | |
| 11 | ? | — | Also connects to J7 pin 1 (Play & Charge?). *Unknown* |
| 12 | VBUS | Input | USB detection |
| 13 | Pair sense via D30 | Input | Suspected; active-low. *Verify* |
| 14 | GND | — | |

Pins marked *Verify* / *Unknown* are tracked in [todo.md](todo.md).

## Firmware

Built with the **nRF Connect SDK v3.4.1** (Zephyr), installed with `nrfutil sdk-manager` into `~/ncs/`. Both apps are standalone Zephyr applications that share `firmware/common/include/protocol.h` (radio packet formats).

| App | Folder | Board targets |
|---|---|---|
| Controller | `firmware/xbx-nrf` | `promicro_nrf52840/nrf52840/uf2` (Pro Micro nice!nano clone, prototype) |
| Dongle | `firmware/dongle` | `nrf52840dongle/nrf52840`, `promicro_nrf52840/nrf52840/uf2` (stand-in) |

**Current state (M1 link test):** the controller sends a fake input report every 1 ms over ESB. The dongle answers with an output report in the ACK payload. Both print link statistics once per second on their USB serial console, and pulse pin **P0.17** (Pro Micro "D2" position) for scope timing.

Development builds (unsigned, no bootloader) use `firmware/build-unsigned.sh`, which knows each app's default board and builds into `<app>/build/`:

```sh
ncs-shell    # toolchain environment (see below)

firmware/build-unsigned.sh xbx-nrf          # incremental build
firmware/build-unsigned.sh dongle -p        # pristine rebuild
firmware/build-unsigned.sh dongle -p -b nrf52840dongle/nrf52840   # other board
```

The default boards are currently the Pro Micro prototypes; they change when the real hardware exists (tracked in `todo.md`). Plain `west build -b <board>` inside an app folder also works.

`ncs-shell` is a small personal script (`~/bin/ncs-shell`, not in the repo):

```bash
#!/bin/bash
export XBX_KEY_DIR="$HOME/.config/xbx-nrf/keys"      # signing keys, see docs/signing.md
export ZEPHYR_BASE="$HOME/ncs/v3.4.1/zephyr"         # lets `west build` work outside ~/ncs
nrfutil sdk-manager toolchain launch --ncs-version v3.4.1 --shell
```

Board-specific settings go in `<app>/boards/`, named after the **full** board target (`promicro_nrf52840_nrf52840_uf2.overlay`); a shorter name is silently ignored.

Flashing and debugging: see [docs/flashing.md](docs/flashing.md).

**Signed release builds** (MCUboot + ECDSA P-256, one key per device, keys kept outside the repo): `firmware/build-signed.sh <xbx-nrf|dongle> <board>` from `ncs-shell`. See [docs/signing.md](docs/signing.md).

## Repository layout

```
docs/          project notes (Obsidian format)
  pcal6416.md  U4 expander notes and button map
  a3910.md     rumble driver notes
  flashing.md  programming/debugging the nRF52840 boards
  signing.md   firmware signing (MCUboot, keys, release builds)
firmware/
  common/      shared code (include/protocol.h: radio packet formats)
  build-unsigned.sh  development build (default board per app)
  build-signed.sh    signed release build (MCUboot)
  xbx-nrf/     controller firmware
  dongle/      dongle firmware
pcb/
  xbx-nrf/     KiCad project for the replacement bottom board
    lib/       project symbol/footprint/3D libraries
references/    schematics, board scans, datasheets, pinouts, CAD outline
todo.md        task list (Obsidian format)
```

## References

- [Top board schematic](references/XB1%201914%20TOP%20BOARD.pdf)
- [Bottom board schematic](references/XB1_1914_BOTTOM_BOARD_SOME_VALUES.pdf)
- [SoC module schematic](references/XB1_1914_SOC_SOME_VALUES.pdf)
- [Connector pinouts and test points](references/controller-pinouts.txt)
- [SoC pinout](references/soc-pinout.txt)
- [A1304 trigger Hall sensor datasheet](references/A1304-Datasheet.pdf)
- [A3910 rumble driver datasheet](references/A3910-datasheet.pdf)
- [PCAL6416A I/O expander datasheet](references/PCAL6416A-datasheet.pdf)
- [AcidMods — XB1 controller PCB scans, traces and info](https://acidmods.com/forum/index.php?topic=44547.120)
- [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) — reference for the XInput implementation
