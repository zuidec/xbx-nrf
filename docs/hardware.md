---
title: Hardware (stock top board, J3, dongle board)
created: 2026-09-29
updated: 2026-10-01
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
J2. We'll reuse the 2.4 GHz one, after verification (required before ordering
the PCB; [[todo#MCU / radio]]).

## Dongle board

KiCad: `pcb/xbx-nrf-dongle/`. Raytac MDBT50Q-U1MV2 (u.FL), normal voltage
mode: LDS3985M33R LDO from VBUS → VDD + VDDH (3.3 V); VBUS also to the module
for USB. 32.768 kHz crystal (FC-135, 7 pF) with 9 pF caps on XL1/XL2.

| Signal | Pin | Notes |
|---|---|---|
| MODE_SW | P1.11 | **Low = HID, high = XInput**; 10 kΩ to each rail, read once at boot (no pull) |
| PAIR_SW | P1.12 | Button to GND; internal pull-up |
| PAIR_LED (blue) | P0.02 | Active low, 180 Ω (high drive) |
| P1_LED…P4_LED (green) | P1.13, P1.14, P1.15, P0.03 | Active low, 1 kΩ |
| PWR_LED (red) | P0.29 | JP1 to GPIO: active low; JP1 to GND: always on |
| DBG_TX / DBG_RX | P0.21 / P0.20 | JST-SH UART, Raspberry Pi pinout |
| SWDIO / SWCLK | — | JST-SH SWD, Raspberry Pi pinout; NRST and VDD on test pads |
| TIMING | P0.17 | Test pad (scope) |
| SWO | P1.00 | Test pad |
| Spare | P0.04, P0.06, P0.26, P0.27, P0.30 (AIN6), P0.31 (AIN7) | Test pads |
