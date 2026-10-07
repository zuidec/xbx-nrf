---
title: Test controller teardown notes
created: 2026-10-05
updated: 2026-10-05
tags:
  - xbx-nrf
  - hardware
  - howto
aliases:
  - teardown
---

# Test controller teardown notes

Observations from the test controller (1914), for taking it apart and putting
it back together. Related: [[docs/hardware]], [[docs/a3910]],
[[todo#1. Verify / reverse-engineer (stock hardware)]].

## Motor wires (bottom board)

The grip motors sit in the handles: **heavy (strong) on the left, weak
(light) on the right**, seen from the front. The wires are soldered to pads
SQ1–SQ8; odd pads are driven (A3910 OUTx), even pads are GND.

| Motor | Driven pad (+) | GND pad (−) | Driver |
|---|---|---|---|
| Heavy (left grip) | SQ3, red | SQ4, black | U4 OUT2 |
| Weak (right grip) | SQ7, red | SQ8, black | U5 OUT2 |
| LT impulse trigger | SQ1, grey | SQ2, black | U4 OUT1 |
| RT impulse trigger | SQ5, grey | SQ6, black | U5 OUT1 |

Black is always GND. Note the colours before desoldering; the trigger motors
use grey, not red, for the driven side.

## Measurements

### Sticks (stock board intact)

- [x] Continuity: TP64 → VR1_2 (LSX), TP63 → VR2_2 (LSY), TP112 → RSX,
      TP65 → RSY, TP42 / TP43 → click (LSC / RSC).
- [x] Directions: wiper voltage rises with the stick pushed **right** (X) and
      **down** (Y).

### Power latch (bottom board out)

Bench supply on the AA contacts (2.4 V, 200–300 mA limit), no USB. Probe
TP35 (3.3 V) and TP34 (`3v/B+`). J3 pins by continuity: TP35 → 4, TP34 → 3,
TP36 → 1, GND → 14; that gives pin 9 (hold) and pin 13 (Pair sense). Always
use the resistors below, never a direct short.

- [x] Standby current, controller off: < 0.1 µA (meter reads 0.0 µA).
      Bypass the meter while powering up.
- [x] Held on by pin 9, nothing else connected: 0.35 mA.
- [x] Guide / Pair: 3.3 V stays up while held, drops shortly after release.
      One early reading held ~3 s (not repeated).
- [x] Hold: 1 kΩ from pin 9 to TP35 keeps power after a Guide or Pair tap;
      removing it turns power off.
- [x] I2C (nice!nano test app, top board on USB, 4.7 kΩ pull-ups to TP35):
      PCAL6416 ACKs at `0x21`; gone while pin 9 is low (reset); every button
      reads on its mapped bit. Without the pull-up setup the inputs read
      `0x0000`.
- [x] Top board I2C pull-ups: none (pin 5 / 7 open to TP35, unpowered).
- [x] Guide on J3 pin 2: active-low, read with the nice!nano's pull-up.
- [x] Q8 (Guide) and Q14 (Pair): both PNP (diode test).
- [x] Pair sense: pull-up from pin 13 to 3.3 V; low while Pair is pressed,
      not Guide.
- [x] Rails while on: pins 1 (3.6 V), 3 (`3v/B+`), 4 (3.3 V) as expected.
- [x] USB plugged in, no button: all rails up. AA side still ~0.25 mA
      (direction not checked).
- [ ] `3v/B+` voltage on USB (TP34): must stay below 3.6 V for the battery
      sense input.
- [x] Pin 11: 0.1 Ω to J7 pin 1; no continuity to GND or any rail (the
      "2.4 kΩ" first read was a protection diode to GND on the bottom board).
      Battery switch status: VBAT when off, 0 V when held on (Guide or pin 9).
      Rails show a slowly climbing resistance: decoupling caps charging,
      normal.

### Layout positions (stock bottom board)

Tip: a 600 dpi flatbed scan of both sides is true to scale (0.042 mm per
pixel); import it into KiCad or FreeCAD, align it on two mounting holes and
place footprints over the stock parts. Calipers then only for heights.

Datum: centre of the left M2.5 hole. View: the side facing the top board.
Positions are part centres (pin 1 for connectors); note the rotation.

Critical:
- [ ] Board thickness: → ___ mm (ours: 1.6 mm)
- [ ] J5: pin 1 → (___, ___); row direction ___; pad span, outer edges →
      ___ mm (KiCad footprint: 5.7 mm)
- [ ] Left stick: lever axis → (___, ___); VR1 pins face ___
- [ ] Right stick: lever axis → (___, ___); VR1 pins face ___
- [ ] LT sensor (A1304): centre → (___, ___); rotation ___; board side ___;
      branded face up / down
- [ ] RT sensor (A1304): centre → (___, ___); rotation ___; board side ___;
      branded face up / down
- [ ] Mounting holes match our outline; screw boss diameter → ___ mm

Contacts:
- [ ] B contact: centre → (___, ___); finger direction ___
- [ ] Motor pads SQ1–SQ8: centres and pad size → ___ (wires have little
      slack, so keep the stock positions)
- [ ] J3 ground spring: position → (___, ___); pad size ___; mates with ___
      (marker on the tip, assemble, look for the imprint)
- [ ] J7 (Play & Charge): pin 1 → (___, ___); pitch ___ mm; contact height
      ___ mm; pad size ___

Clearances:
- [ ] Old SoC board area: free height above → ___ mm, below → ___ mm
      (Raytac module + u.FL plug)
- [ ] Maximum part height by zone (sketch or photo with notes)
- [ ] Expansion port opening / bottom edge: area to keep clear → ___
- [ ] Antenna: top board connector for `ANT` / `ANT1` → (___, ___); pigtail
      length ___ mm
- [ ] Photos of both sides with a ruler in frame (fallback)
