---
title: Hardware (stock top board, J3, controller and dongle boards)
created: 2026-09-29
updated: 2026-11-03
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
Related: [PCAL6416 (buttons)](pcal6416.md), [A3910 (rumble)](a3910.md),
[Verify / reverse-engineer (stock hardware)](../todo.md#verify--reverse-engineer-stock-hardware).

Sources:
[XB1 1914 TOP BOARD.pdf](../references/third-party/XB1%201914%20TOP%20BOARD.pdf),
[controller-pinouts.txt](../references/third-party/controller-pinouts.txt),
[soc-pinout.txt](../references/third-party/soc-pinout.txt).

> [!WARNING]
> **No bare Li-ion cells**
>
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

The latch is **not self-holding**: Guide or Pair keeps 3.3 V up for as long
as it is held, and drops shortly after release (one early reading showed
~3 s; not repeated). Pulling J3 pin 9 high holds power until it is released,
so firmware must raise pin 9 while the button is still pressed: in the
bootloader, or early in boot
([todo](../todo.md)). Top board alone, AAs side: < 0.1 µA off,
0.35 mA held on.

With USB plugged in, all rails (`3v/B+`, 3.3 V, 3.6 V) are up without a
button press, and pin 9 has no effect: firmware needs its own "off" state on
USB. The AA side still sees ~0.25 mA (source unclear: through the gate
network it would be only ~10 µA).

Latch network (top board, measured): R23 10 kΩ from the Q6/Q7 gate node to
the battery, R21 220 kΩ to 5vB, R22/R26 0 Ω, R25 25.5 kΩ to J3 pin 11. Held
on, ~0.24 mA flows through R23 and Q9.

## J3: top ↔ bottom board connector

| Pin | Signal | Direction (bottom board) | Notes |
|---|---|---|---|
| 1 | 3.6 V | Power in | Rumble supply |
| 2 | GU (Guide) via D27 | Input | Active-low; needs a pull-up on our side (the nRF's internal one) |
| 3 | 3v/B+ | Power in | Switched battery rail; battery sense |
| 4 | 3.3 V | Power in | nRF supply |
| 5 | SCL | Output | I2C to the PCAL6416; no pull-up on the top board (ours required) |
| 6 | LED control | Output | Guide LED (Q3) |
| 7 | SDA | Bidirectional | I2C to the PCAL6416; no pull-up on the top board |
| 8 | USB D+ | Bidirectional | From the USB-C port |
| 9 | ~RESET / power hold | Output | High = power held + expander out of reset (both measured). Power: no effect on USB; the expander reset still applies |
| 10 | USB D− | Bidirectional | |
| 11 | Battery switch status | Input | Q9 drain via R25 (25.5 kΩ): VBAT = off, 0 V = on (measured). Also J7 pin 1 and SoC pin 1. Leave unloaded: a pull-down partly turns on Q6/Q7 |
| 12 | VBUS | Input | USB detection |
| 13 | Pair sense via D30 | Input | Active-low: low while Pair is pressed, not Guide (measured) |
| 14 | GND | — | |

Rails on pins 1, 3, 4 measured as expected with the controller on.

**Connector (measured, no markings):** 2 × 7, 1.27 × 1.27 mm.

| | Top board J3 (male) | Bottom board J5 (female, ours) |
|---|---|---|
| Body | Shrouded header, 4 mm tall incl. 0.5 mm floor | 9.35 × 3.15 × 3.5 mm |
| Shroud opening | 9.5 × 3.25 mm, 45° chamfer at pin 1 (~1.2 mm hypotenuse, ~0.85 mm legs) | Chamfered to match at pin 1 |
| Keying | Two slots | Two tabs on the even-pin side, 5.3 mm apart, 0.5 mm wide |
| Contacts | 3.5 mm tapered pins | Entry holes ~0.8 mm |
| Mounting | – | Gull-wing SMT, odd pins one row, even the other |

- **Stack:** boards ~5.2 mm apart; J5 sits ~2.3 mm deep in the shroud (its
  mating surface), ~1.2 mm gap to J3's floor. The screw bosses set the stack.
- **Fit:** a 2 × 7 socket body is ~9.3 mm long, so a square corner only
  clears the shroud's chamfer if (9.5 − length) + (3.25 − width) ≥ 1.7 mm:
  impossible for two rows. A replacement needs the pin 1 chamfer (≥ ~0.75 mm
  legs), or the chamfered fill gets cut out of J3's shroud corner. The tabs
  can be omitted.
- **Sourcing:** no direct match found (open). Target: ~3.5 mm tall, body
  within 9.5 × 3.25 mm, SMT.
- KiCad footprint (generic 1.27 mm 2 × 7 SMD socket):
  `Connector_PinSocket_1.27mm:PinSocket_2x07_P1.27mm_Vertical_SMD`; mark the
  pin 1 chamfer on Fab/silkscreen and the shroud outline on the courtyard.

**J7 (Play & Charge contacts, bottom board):** 1 → J3 pin 11, 2 SCL2 (via R15,
0 Ω), 3 SDA2, 4 VBUS. No GND pin: the pack's ground is the AA− contact. Not
on our v1 board: no Play & Charge support, charge the AAs externally.

*Verify* / *Unknown*:
[todo](../todo.md#verify--reverse-engineer-stock-hardware).

## Antennas (the stock ones)

Two feeds on the top board (`ANT` via J5/J6, `ANT1` via J8), each with a
matching network; the stock SoC board reaches them through coax connectors J1,
J2. Both cover 2.4 GHz and arrive matched to 50 Ω, so the module's u.FL
connects to one by a coax pigtail with no tuning. Rev1.1 uses this and
links at ~0 % loss.

## Controller board

KiCad: `pcb/xbx-nrf/`. Rev1.1 built and working; outline and part positions from
`mech/controller-bottom-board/` (measured, fit-checked in the shell). No Play &
Charge in v1. Open items:
[todo](../todo.md#verify--reverse-engineer-stock-hardware).

Board target `xbx_controller/nrf52840/uf2`, with our UF2 bootloader
(`firmware/boards/xbx/xbx_controller/`, the default in `build.sh`); flashing:
[Custom boards (SWD)](flashing.md#custom-boards-swd).

### Sticks (RKJXV122400R footprint)

TMR drop-in sticks on the Alps RKJXV footprint. Pinout from the stock bottom
board
([XB1_1914_BOTTOM_BOARD_SOME_VALUES.pdf](../references/third-party/XB1_1914_BOTTOM_BOARD_SOME_VALUES.pdf))
and board scans; same for both sticks. Checked on the test controller: wipers
(TP64/63/112/65) and click (TP42/43) by continuity.

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

> [!WARNING]
> **Never swap 1.8 V and GND on a TMR stick**
>
> A pot only inverts its axis; a TMR stick's sensor can be damaged. Axis
> direction is fixed by this pinout and corrected in firmware.

- **Directions (measured on the stock pots; recheck with the TMR modules):**
  X reads higher to the right (matches the report); Y reads higher
  downwards, so Y is inverted in firmware.
- **Supply:** stock U8 TLV70718 (1.8 V LDO from 3.3 V, EN from the SoC with
  a 100 kΩ pull-down); ours 1V8_STICK ([Supplies](#supplies)), on while the
  controller is on. 100 nF from 1.8 V to GND at each stick. The stock SoC
  uses this rail as its ADC reference (ratiometric); the nRF52840 can't, so
  AIN0 measures it.
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

All 8 SAADC inputs are used. Any AIN can take any signal (reference and gain
are per channel, mapping in the devicetree); the order below is the Rev1.1
routing.

Unlike an STM32 there is no external reference: each channel's full scale is
reference ÷ gain, set per channel. Pins never above VDD + 0.3 V regardless.

| AIN | Pin | Signal | Reference | Gain | Full scale |
|---|---|---|---|---|---|
| AIN0 | P0.02 | 1V8_STICK: stick supply | Internal 0.6 V | 1/4 | 2.4 V (rail at ~75 %, no clipping) |
| AIN1 | P0.03 | RT (A1304) | VDD/4 | 1/4 | VDD (ratiometric: TRG+ ≈ VDD) |
| AIN2 | P0.04 | LSY | Internal 0.6 V | 1/3 | 1.8 V |
| AIN3 | P0.05 | LSX | Internal 0.6 V | 1/3 | 1.8 V |
| AIN4 | P0.28 | LT (A1304) | VDD/4 | 1/4 | VDD |
| AIN5 | P0.29 | RSY | Internal 0.6 V | 1/3 | 1.8 V |
| AIN6 | P0.30 | VBAT: `3v/B+` via 100 kΩ, 100 nF at the pin, no divider | Internal 0.6 V | 1/6 | 3.6 V |
| AIN7 | P0.31 | RSX | Internal 0.6 V | 1/3 | 1.8 V |

Sticks are read as a ratio to AIN0 (same reference, so its error cancels;
fixed gain differences are absorbed by calibration). Run the SAADC offset
calibration at start-up.

All eight go in one scan per tick (~40 µs), in SAADC channel order (not AIN
order): 1V8_STICK, LSX, LSY, RSX, RSY, VBAT, LT, RT. Every channel uses a
3 µs acquisition: the 100 nF at each pin supplies the sampling charge, so the
source impedance (VBAT's 100 kΩ included) doesn't matter. The A1304 needs
50–70 µs after power-on, so a timer alarm turns the trigger supply on 80 µs
before the scan.

VBAT needs no divider: `3v/B+` stays below 3.6 V (2 AAs ≤ ~3.0 V; on USB below
3.3 V, as U5/U6 only boost) and is 0 V when off (behind Q6/Q7), so nothing
drains. The 100 kΩ limits current into the pin's protection diode while
`3v/B+` comes up before 3.3 V; the 100 nF supplies the sampling charge.

### Supplies

The top board supplies 3.3 V (logic, nRF) and 3.6 V (rumble) over J3; the
sensor rails are ours. Both enables default to off, so the rails stay down
through reset.

| Rail | Part | From | Enable | Load |
|---|---|---|---|---|
| 1V8_STICK | TLV70018DDCR (1.8 V LDO, SOT-23-5) | J3 3.3 V | GPIO high = on, 100 kΩ pull-down | Sticks, AIN0 sense |
| TRG+ | P-FET (AO3401A), one for both triggers | J3 3.3 V | GPIO low = on, gate pull-up (off through reset) | 2× A1304 |

- **TLV70018 (DDC):** 1 IN, 2 GND, 3 EN, 4 NC (open or GND), 5 OUT. 1 µF
  X5R/X7R on IN and OUT. EN on above 0.9 V, max VIN + 0.3 V; start-up
  ~100 µs; ±2 %; no active output discharge. KiCad:
  `Regulator_Linear:TLV70018_SOT23-5`.
- **TRG+:** a P-FET instead of the stock LDO-as-switch: our GPIOs are 3.3 V,
  so no level shifting, and it drops less (A1304 needs ≥ 3.0 V). Switch-on
  slowed by a gate resistor: the A1304 needs a VCC ramp of at least 5 µs.
- **A1304 (LH, SOT-23W):** 1 VCC, 2 VOUT, 3 GND. 3.0–3.6 V, 7.7 mA typ
  (9 mA max) each; output ratiometric to VCC (50 % at no field); power-on
  50–70 µs (10 nF load). On the pulsed trigger inputs keep the filter fast:
  ~10 nF, 0 Ω (100 nF + 1 kΩ would take ~0.5 ms to settle).
- Stock for comparison: U8 TLV70718 (sticks) and U9 TLV70732 from 3.3 V used
  as a switch for TRG+ (EN from the SoC via R38, 100 kΩ pull-down).

### Pins

From the Rev1.1 schematic; analog pins in [Analog inputs](#analog-inputs). All
buttons are active low on the nRF's internal pull-up (none on the board): B, LS,
RS and the AUX pads switch to GND through 1 kΩ; Guide and Pair come from the top
board through its diodes (D27, D30).

| Signal | Pin | Notes |
|---|---|---|
| PCAL_SCL / PCAL_SDA | P0.24 / P0.25 | J2 pins 5 / 7, I2C to the PCAL6416; 2.2 kΩ pull-ups (R8, R9) |
| PWR_HOLD | P1.12 | J2 pin 9: hold power + expander reset |
| GUIDE_BTN | P1.15 | J2 pin 2, active low |
| PAIR_BTN | P1.11 | J2 pin 13, active low |
| GUIDE_LED | P1.13 | J2 pin 6 |
| JS_EN | P0.15 | 1V8_STICK enable, high = on |
| TRG_EN | P1.10 | TRG+ enable (Q1), low = on |
| B / LS / RS buttons | P1.05 / P1.02 / P1.06 | B membrane contact SW2, stick clicks |
| AUX1–AUX4 | P0.14, P0.13, P1.14, P1.07 | Paddle pads TP9–TP12 (GND: TP5–TP8); held face-up, AUX1/2 are on the left, AUX3/4 on the right (tested); AUX1 and AUX4 are the lower paddles. Firmware P1–P4 = AUX2, AUX1, AUX3, AUX4 |
| LT_AUX / RT_AUX | P0.22 / P1.04 | Trigger switch pads TP13 / TP15 (GND: TP14, TP16) |
| RMB_LT HIN / LIN | P0.20 / P0.21 | U5, LT motor (SQ1/SQ2) |
| RMB_HV HIN / LIN | P0.17 / P0.19 | U5, heavy motor (SQ3/SQ4) |
| RMB_RT HIN / LIN | P0.06 / P0.07 | U6, RT motor (SQ5/SQ6) |
| RMB_WK HIN / LIN | P1.08 / P0.08 | U6, weak motor (SQ7/SQ8) |
| DBG_TX / DBG_RX | P0.27 / P0.26 | J3 UART (JST-SH, Raspberry Pi pinout): pin 1 TX, pin 3 RX |
| SWCLK / SWDIO | — | J1 SWD (JST-SH, Raspberry Pi pinout) |
| NRST | P0.18 | SW1 reset button, TP1 |
| TIMING | P1.09 | TP4 (scope) |
| SWO | P1.00 | TP3 |
| XL1 / XL2 | P0.00 / P0.01 | Y1, 32.768 kHz crystal (as the dongle) |

## Dongle board

Two hardware options, chosen by the build's board target (each has its own
overlay in `firmware/dongle/boards/`): the custom board below, or the
off-the-shelf [nRF52840 Dongle (PCA10059)](#nrf52840-dongle-pca10059).

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
| MODE_SW | P1.09 | SW3, read at boot (a change restarts the dongle after 1 s): **low = XInput** (1 kΩ to GND), **high = HID** (open, internal pull-up) |
| PAIR_SW | P1.12 | SW2 to GND; internal pull-up. Press = pairing mode, hold 10 s = factory reset |
| PAIR_LED (blue) | P0.08 | D1, active low, 180 Ω, high drive; pairing indicator; `pwm1` ch 0 |
| P1_LED…P4_LED (green) | P1.13, P1.14, P1.15, P0.03 | D2–D5, active low, 1 kΩ; `pwm0` ch 0–3 |
| PWR_LED (red) | P0.22 | D6, 1 kΩ, via JP1 (open by default; see the warning below): 2–3 = GPIO (`pwm1` ch 1), active low; 1–2 = GND, always on |
| DBG_TX / DBG_RX | P0.20 / P0.21 | J3 UART (JST-SH, Raspberry Pi pinout): pin 1 TX, pin 3 RX; console for samples (the app logs over USB) |
| SWCLK / SWDIO | — | J1 SWD (JST-SH, Raspberry Pi pinout) |
| NRST | P0.18 | SW1 reset button, TP1 |
| TIMING | P0.17 | TP4 (scope) |
| SWO | P1.00 | TP3 |
| Spare | P0.04, P0.06, P0.26, P0.27, P0.30 (AIN6), P0.31 (AIN7) | TP14–TP17, TP12, TP13 |
| Power | — | +3V3: TP2, TP8–TP10; VBUS: TP11; GND: TP5–TP7 |

> [!WARNING]
> **JP1: bridge one side only**
>
> Bridge 2–3 (LED on P0.22) **or** 1–2 (LED always on), never both: that
> shorts P0.22 to GND, and the GPIO driving high into it can be damaged. The
> board definition drives the LED pins open-drain (never high, also on PWM)
> as a backstop.

### nRF52840 Dongle (PCA10059)

Nordic's USB dongle, for anyone not building the custom board. Board target
`nrf52840dongle/nrf52840` (`xbx-dongle-pca10059` target); flashing over its USB
bootloader:
[nRF52840 Dongle (PCA10059) — USB DFU (no probe)](flashing.md#nrf52840-dongle-pca10059--usb-dfu-no-probe).
Runs from VDDH (USB), GPIOs at 3.0 V (set by Zephyr's board code).

| Signal | Pin | Notes |
|---|---|---|
| MODE_SW | P0.29 (edge pad) | Add a switch to GND: **closed = XInput, open = HID**; internal pull-up |
| PAIR_SW | P1.06 | On-board SW1: press = pairing mode, hold 10 s = factory reset |
| LED (green) | P0.06 | On-board LED1, active low |
| RGB LED | P0.08 (R), P1.09 (G), P0.12 (B) | On-board LED2, active low; blue = pairing indicator (PWM) |
| Reset | P0.18 | On-board side button (also enters the USB bootloader) |
