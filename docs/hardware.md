---
title: Hardware (stock top board & J3)
created: 2026-09-29
updated: 2026-09-29
tags:
  - xbx-nrf
  - reference
  - hardware
aliases:
  - hardware
  - J3
  - top board
---

# Hardware: stock top board & J3

The stock **top board** and the **J3** connector our bottom board plugs into.
Related: [[docs/pcal6416|PCAL6416 (buttons)]], [[docs/a3910|A3910 (rumble)]],
[[todo#1. Verify / reverse-engineer (stock hardware)]].

Sources: [[references/XB1 1914 TOP BOARD.pdf]],
[[references/controller-pinouts.txt]], [[references/soc-pinout.txt]].

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
