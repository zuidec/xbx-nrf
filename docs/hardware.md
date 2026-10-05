---
title: Hardware (stock top board, J3, controller and dongle boards)
created: 2026-09-29
updated: 2026-10-05
tags:
  - xbx-nrf
  - reference
  - hardware
aliases:
  - hardware
  - J3
  - top board
  - dongle board
  - controller board
---

# Hardware: stock top board, J3, controller and dongle boards

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

## Controller board

KiCad: `pcb/xbx-nrf/`. Design in progress:
[[todo#3. Hardware design — controller board]].

### Sticks (RKJXV122400R footprint)

TMR drop-in sticks on the Alps RKJXV footprint. Pinout from the stock bottom
board ([[references/third-party/XB1_1914_BOTTOM_BOARD_SOME_VALUES.pdf]]) and
board scans; same for both sticks. Wipers as the middle pin: by elimination,
not yet measured.

| Pin | Function | Net (left / right) |
|---|---|---|
| VR1_1 | X pot end "L" | GND |
| VR1_2 | X wiper | LSX / RSX |
| VR1_3 | X pot end "R" | 1.8 V (trace to VR2_1) |
| VR2_1 | Y pot end "D" | 1.8 V |
| VR2_2 | Y wiper | LSY / RSY |
| VR2_3 | Y pot end "U" | GND |
| A, C | Click, signal side | LSC / RSC (stock: 33 Ω series, cap to GND) |
| B, D | Click, other side | GND |
| SH1–SH4 | Shield | GND |

> [!warning] Never swap 1.8 V and GND on a TMR stick
> A pot only inverts its axis; a TMR stick's sensor can be damaged. Axis
> direction is fixed by this pinout and corrected in firmware.

- **Directions (stock convention, check with the TMR modules):** X reads
  higher to the right (matches the report); Y reads higher downwards, so Y is
  inverted in firmware.
- **Supply:** stock U8 TLV70718 (1.8 V LDO from 3.3 V, EN from the SoC with
  a 100 kΩ pull-down); ours the same, on while the controller is on. 100 nF
  from 1.8 V to GND at each stick. The stock SoC uses this rail as its ADC
  reference (ratiometric); the nRF52840 can't, so AIN7 measures it.
- **Filtering:** per axis, series R + C to GND at the nRF's AIN pin (C
  closest to the pin): 0 Ω + 100 nF, R optional for tuning (up to 1 kΩ). The
  SAADC input is > 1 MΩ, so 1 kΩ costs < 0.1 % gain error (absorbed by
  calibration); the 100 nF supplies the sampling charge, and R isolates the
  sensor output from the cap. Filter the 1V8 sense the same way. Stock: caps
  only, no series R (C24 LSX, C22 + C322 LSY, C25 RSX, C23 RSY); wipers go
  straight to the SoC.
- Stock test points: TP64 LSX, TP63 LSY, TP112 RSX, TP65 RSY, TP103 1.8 V,
  TP36 1.8 V enable, TP42 LSC, TP43 RSC.

### Analog inputs

All 8 SAADC inputs are used; reserve them before assigning digital pins. Any
AIN can take any signal (reference and gain are per channel, mapping in the
devicetree), so swap for routing as needed.

| AIN | Pin | Signal | Range / ADC setting |
|---|---|---|---|
| AIN0 | P0.02 | LSX | 0–1.8 V: internal 0.6 V ref, gain 1/3 |
| AIN1 | P0.03 | LSY | 0–1.8 V |
| AIN2 | P0.04 | RSX | 0–1.8 V |
| AIN3 | P0.05 | RSY | 0–1.8 V |
| AIN4 | P0.28 | LT (A1304) | Up to TRG+ (3.3 V): gain 1/6 (3.6 V full scale) |
| AIN5 | P0.29 | RT (A1304) | Same as LT |
| AIN6 | P0.30 | VBAT: switched divider on `3v/B+` | Divided to below 1.8 V |
| AIN7 | P0.31 | 1V8_STICK: stick supply | Ratio for the stick readings |

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
| DBG_TX / DBG_RX | P0.20 / P0.21 | J3 UART (JST-SH, Raspberry Pi pinout): pin 1 TX, pin 3 RX; console for samples (the app logs over USB) |
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
