---
title: Hardware (stock top board, J3, dongle board)
created: 2026-09-29
updated: 2026-10-02
tags:
  - xbx-nrf
  - reference
  - hardware
aliases:
  - hardware
  - J3
  - top board
  - dongle board
---

# Hardware: stock top board, J3, dongle board

The stock **top board** and the **J3** connector our bottom board plugs into.
Related: [[docs/pcal6416|PCAL6416 (buttons)]], [[docs/a3910|A3910 (rumble)]],
[[todo#1. Verify / reverse-engineer (stock hardware)]].

Sources: [[references/third-party/XB1 1914 TOP BOARD.pdf]],
[[references/third-party/controller-pinouts.txt]],
[[references/third-party/soc-pinout.txt]].

> [!warning] No bare Li-ion cells
> U5/U6 are boost-only: a 4.2 V cell on the AA contacts passes through to the
> 3.3 V rail, above the nRF52840's 3.6 V max. Use rechargeable AAs (NiMH, or
> regulated 1.5 V Li-ion AAs).

## Top board power (stock, reused)

| Part | Function |
|---|---|
| Q6/Q7 | Battery → `3v/B+` switch; disconnects the AAs when USB is present |
| U5 TPS613221 | `3v/B+` → **3.3 V** (logic) |
| U6 TPS613226 | `3v/B+` → **3.6 V** (rumble) |
| U1 MP1476L | USB 5 V → `3v/B+`; powered on whenever USB is plugged in |
| SW13 + Q8 / SW2 + Q14 → Q9 | Guide or Pair powers on; J3 pin 9 holds it |

The latch is probably **not self-holding**: firmware must raise J3 pin 9 before
the button is released ([[todo#Power latch (top board)]]).

## J3: top ↔ bottom board connector

| Pin | Signal | Direction (bottom board) | Notes |
|---|---|---|---|
| 1 | 3.6 V | Power in | Rumble supply |
| 2 | GU (Guide) via D27 | Input | Active-low; pull-up on our board |
| 3 | 3v/B+ | Power in | Switched battery rail; battery sense |
| 4 | 3.3 V | Power in | nRF supply |
| 5 | SCL | Output | I2C to the PCAL6416 |
| 6 | LED control | Output | Guide LED (Q3) |
| 7 | SDA | Bidirectional | I2C to the PCAL6416 |
| 8 | USB D+ | Bidirectional | From the USB-C port |
| 9 | ~RESET / power hold | Output | High = power held + expander out of reset. *Verify* |
| 10 | USB D− | Bidirectional | |
| 11 | ? | — | Also J7 pin 1 (Play & Charge?). *Unknown* |
| 12 | VBUS | Input | USB detection |
| 13 | Pair sense via D30 | Input | Suspected; active-low. *Verify* |
| 14 | GND | — | |

*Verify* / *Unknown*: [[todo#J3/J5 connector (top ↔ bottom)]].

## Antennas (plan: reuse the stock ones)

Two feeds on the top board (`ANT` via J5/J6, `ANT1` via J8), each with a
matching network; the stock SoC board reaches them through coax connectors J1,
J2. Both cover 2.4 GHz and arrive matched to 50 Ω, so the module's u.FL
connects to one by a coax pigtail with no tuning; a functional check picks
which ([[todo#MCU / radio]]).

## Dongle board

Two hardware options, chosen by the build's board target (each has its own
overlay in `firmware/dongle/boards/`): the custom board below, or the
off-the-shelf [[#nRF52840 Dongle (PCA10059)]].

### Custom board

KiCad: `pcb/xbx-nrf-dongle/`. Raytac MDBT50Q-U1MV2 (u.FL), normal voltage
mode: LDS3985M33R LDO from VBUS → VDD + VDDH (3.3 V); VBUS also to the module
for USB. 32.768 kHz crystal (FC-135, 7 pF) with 9 pF caps on XL1/XL2.
Antenna: Kyocera AVX 1003893FT-AA10L0050 (FPC, u.FL, 50 mm cable) flat on the
enclosure lid, away from the PCB; backup Taoglas CBD01.07.0100C.

Board target `xbx_dongle/nrf52840` (`firmware/boards/xbx/xbx_dongle/`);
flashing over J1 (SWD). Pins from the Rev1.1 schematic:

| Signal | Pin | Notes |
|---|---|---|
| MODE_SW | P1.09 | SW3, read once at boot: **low = XInput** (1 kΩ to GND), **high = HID** (open, internal pull-up) |
| PAIR_SW | P1.12 | SW2 to GND; internal pull-up. Press = pairing mode, hold 10 s = factory reset |
| PAIR_LED (blue) | P0.08 | D1, active low, 180 Ω, high drive; pairing indicator |
| P1_LED…P4_LED (green) | P1.13, P1.14, P1.15, P0.03 | D2–D5, active low, 1 kΩ |
| PWR_LED (red) | P0.22 | D6, 1 kΩ, via JP1 (open by default; see the warning below): 2–3 = GPIO, active low; 1–2 = GND, always on |
| DBG_TX / DBG_RX | P0.20 / P0.21 | J3 UART (JST-SH, Raspberry Pi pinout): pin 1 TX, pin 3 RX; console for MCUboot and samples (the app logs over USB) |
| SWCLK / SWDIO | — | J1 SWD (JST-SH, Raspberry Pi pinout) |
| NRST | P0.18 | SW1 reset button, TP1 |
| TIMING | P0.17 | TP4 (scope) |
| SWO | P1.00 | TP3 |
| Spare | P0.04, P0.06, P0.26, P0.27, P0.30 (AIN6), P0.31 (AIN7) | TP14–TP17, TP12, TP13 |
| Power | — | +3V3: TP2, TP8–TP10; VBUS: TP11; GND: TP5–TP7 |

> [!warning] JP1: bridge one side only
> Bridge 2–3 (LED on P0.22) **or** 1–2 (LED always on), never both: that
> shorts P0.22 to GND, and the GPIO driving high into it can be damaged. The
> board definition drives the LED pins open-drain (never high) as a backstop.

### nRF52840 Dongle (PCA10059)

Nordic's USB dongle, for anyone not building the custom board. Board target
`nrf52840dongle/nrf52840` (the build default); flashing over its USB
bootloader: [[docs/flashing#nRF52840 Dongle (PCA10059) — USB DFU (no probe) ✅]].
Runs from VDDH (USB), GPIOs at 3.0 V (set by Zephyr's board code).

| Signal | Pin | Notes |
|---|---|---|
| MODE_SW | P0.29 (edge pad) | Add a switch to GND: **closed = XInput, open = HID**; internal pull-up |
| PAIR_SW | P1.06 | On-board SW1: press = pairing mode, hold 10 s = factory reset |
| LED (green) | P0.06 | On-board LED1, active low |
| RGB LED | P0.08 (R), P1.09 (G), P0.12 (B) | On-board LED2, active low; blue = pairing indicator |
| Reset | P0.18 | On-board side button (also enters the USB bootloader) |
