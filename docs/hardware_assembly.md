---
title: Hardware assembly
created: 2026-11-03
updated: 2026-11-04
tags:
  - xbx-nrf
  - hardware
  - howto
aliases:
  - hardware assembly
  - assembly
  - BOM
---

# Hardware assembly

Ordering the boards and parts, placing them, and fitting the controller
board into the shell.

> [!CAUTION]
> **Soldering level: Advanced**
>
> Seriously, if this is your first soldering project *please* do not attempt
> this without at least having the proper equipment. The Raytac modules are
> **extremely** difficult for someone without a fair bit of experience to
> solder.

If you are still dead-set on moving forward, I would encourage you to get PCBA
(PCB assembly) for at least the Raytac module. Once you've paid the PCBA setup
fee, you may as well have the whole board assembled. Last I checked, LCSC
stocks the Raytac module: part number **C2688382**. The fab needs its own BOM
format with LCSC part numbers; our BOM has DigiKey numbers, so fill those in
yourself. `…_positions.csv` covers placement.

Design notes: [hardware](hardware.md).
Once built: [Getting started](getting_started.md).

## Downloads

Each board has its own release, from the
[PCB releases](https://github.com/zuidec/xbx-nrf/releases?q=pcb&expanded=true):
`xbx-controller-pcb-<rev>.zip` and `xbx-dongle-pcb-<rev>.zip`. Each file in
them starts with the board and revision, e.g. `XBX-NRF_Gamepad_Rev1.1_`:

| File | For |
|---|---|
| `…_gerbers.zip` | Upload to the PCB fab |
| `…_bom-digikey.csv` | Parts, with DigiKey part numbers |
| `…_positions.csv` | Part positions (pick and place) |
| `…_netlist.ipc` | Optional: the fab's electrical test |
| `…_ibom.html` | Interactive BOM for placing parts (see below) |


If you prefer to build from the KiCad projects instead: export them with KiCad's
fabrication outputs and use the InteractiveHtmlBom plugin.

## Ordering the PCBs

| Board | KiCad project | Layers | Thickness | Finish |
|---|---|---|---|---|
| Controller | `pcb/xbx-nrf/` | 4 | 1.6 mm | ENIG |
| Dongle | `pcb/xbx-nrf-dongle/` | 2 | 1.6 mm | Any (HASL is fine) |

1. Upload `…_gerbers.zip` to any PCB fab (JLCPCB, PCBWay, OSH Park, ...) with
   the settings above; everything else at the fab's defaults. For the
   controller, **ENIG is highly recommended**: the B button is a bare
   contact pad that the rubber dome presses on, and gold doesn't oxidise;
   the flat pads also make the module much easier to solder.
2. Order a **stencil** with at least one of the boards (see below).

> [!TIP]
> **Get a stencil**
>
> Even if you hand-solder the rest, use a stencil and solder paste for the
> Raytac module. Its pads are underneath it and can't be reached with an iron
> afterwards. If you only order one stencil, get the controller one; for the
> dongle, tape over every aperture except the Raytac module's (its footprint
> is the same on both boards). Skip the stencil if the fab assembles the
> boards (PCBA).

## Bill of materials

Parts with DigiKey part numbers, one CSV per board (also in the release
zips):

- Controller: [bom-digikey.csv](../pcb/xbx-nrf/bom-digikey.csv)
- Dongle: [bom-digikey.csv](../pcb/xbx-nrf-dongle/bom-digikey.csv)

Parts from the stock board can be reused instead of bought (below).

### Reusing parts from the stock board

These come off the stock bottom board and fit our footprints:

| Part | Stock board | Ours | Notes |
|---|---|---|---|
| Sticks (Alps RKJXV type) | | S1, S2 | Or any Series X\|S replacement stick |
| Trigger Hall sensors (A1304) | U10, U11 | U2, U3 | SOT-23W |
| Rumble drivers (A3910) | U4, U5 | U5, U6 | DFN-8, 2 × 2 mm |
| Top-board connector | J5 | J2 | See [The J2 connector](#the-j2-connector) |

Also keep the rumble motors and the stock top board; the motors' wires go to
SQ1–SQ8 ([Motor wires](teardown.md#motor-wires-bottom-board)).

### Substitutions

Most parts can be swapped for any equivalent. Watch these:

- **Crystal and its caps** (controller Y1, C24/C26; dongle Y1, C6/C7): the
  9 pF caps match the FC-135's 7 pF load capacitance. A different crystal
  needs caps for its own: C ≈ 2 × (C_L − ~2.5 pF stray). The caps must be
  C0G/NP0, and the crystal's ESR within the nRF52840's limit (datasheet,
  32.768 kHz crystal oscillator).
- **Bulk caps** (10 µF, 4.7 µF, 1 µF): X5R or X7R, rated ≥ 10 V. Ceramic
  caps lose capacitance under DC voltage; a lower rating or Y5V/Z5U can
  drop the LDOs below the capacitance they need to stay stable.

> [!WARNING]
> **Use the u.FL module: MDBT50Q-U1MV2**
>
> The boards are laid out for the u.FL version and an external antenna.
> They don't keep the copper-free area the chip-antenna version
> (MDBT50Q-P1MV2) needs under and around its antenna, so that version
> would have very poor range on these boards.

## Part placement

Open the release's `…_ibom.html` in a browser; it works offline. Click a BOM
line to highlight its parts on the board, and tick parts off as you place
them.

> [!CAUTION]
> **The Raytac module's pads are small**
>
> They're hard to get all soldered well in one go. Use a stencil and check
> every edge with a magnifier. Rev1.1 has no test points for its pins: the
> first SWD flash is the first check. If OpenOCD can't connect, or a button,
> stick or motor doesn't work later, reflow the module.

## The J2 connector

The new J2 has to fit inside the top board's shrouded J3, which has a 45°
chamfer at pin 1 ([J3 connector](hardware.md#j3-top--bottom-board-connector)).
Either:

- **Reuse the stock connector** (J5 on the stock bottom board), which
  already fits; or
- **File the new one:** gently file the pin 1 corner of the M50-3120745's
  body to match the chamfer (~0.75 mm legs). Test-fit before soldering.

## Antenna

The module's u.FL connects to the top board's stock antenna feed `ANT` (pad
J5 on the top board) through a coax pigtail. The feed is already matched to
50 Ω, so no tuning is needed. Any u.FL pigtail works, e.g. a u.FL-to-SMA
adapter cable with the SMA end cut off.

```
     Top board (back)

     J5 (ANT)      GND pad
      ┌──┐         ┌─────┐
      │  │●━━━━━━━━│▓▓▓▓▓│━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
      └──┘ centre  └─────┘ braid                   ┃ coax, through
           2–3 mm    (gap between)                 ┃ the slot in
                                                   ┃ our board
                                                   ●  u.FL on the
                                                      Raytac module
```

1. Strip 2–3 mm of the free end: just enough centre conductor to solder.
2. Solder the centre conductor to **J5**. Its GND pad sits in line, a little
   further along: solder the braid to it, leaving a gap between the two.
3. Route the cable back through the slot in our board and plug it into the
   module's u.FL.

## Fitting in the shell

- **Extra GND pad (J5 on our board):** where the stock board had a ground
  spring. Wire it to ground on the top board, e.g. the AA − terminal.

> [!WARNING]
> **Tape down the unused antenna**
>
> Cover the leftover antenna that isn't connected with non-conductive tape
> (e.g. Kapton), so it can't short against the board.
