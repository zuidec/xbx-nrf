---
title: Hardware assembly
created: 2026-11-03
updated: 2026-11-03
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
board into the shell. Assumes you can solder fine-pitch SMD parts. Design
notes: [hardware](hardware.md). Once built:
[Getting started](getting_started.md).

## Downloads

Each board has its own release, from the
[PCB releases](https://github.com/zuidec/xbx-nrf/releases?q=pcb&expanded=true):
`xbx-controller-pcb-<rev>.zip` and `xbx-dongle-pcb-<rev>.zip`. Each holds:

| File | For |
|---|---|
| `gerbers.zip` | Upload to the PCB fab |
| `bom.csv` | Parts, with DigiKey part numbers |
| `positions.csv` | Part positions (pick and place) |
| `netlist.ipc` | Optional: the fab's electrical test |
| `ibom.html` | Interactive BOM for placing parts (see below) |

Building from the KiCad projects instead: export them with KiCad's
fabrication outputs and the InteractiveHtmlBom plugin.

## Ordering the PCBs

| Board | KiCad project | Layers | Thickness | Finish |
|---|---|---|---|---|
| Controller | `pcb/xbx-nrf/` | 4 | 1.6 mm | ENIG |
| Dongle | `pcb/xbx-nrf-dongle/` | 2 | 1.6 mm | Any (HASL is fine) |

1. Upload `gerbers.zip` to any PCB fab (JLCPCB, PCBWay, OSH Park, ...) with
   the settings above; everything else at the fab's defaults. For the
   controller, **ENIG is highly recommended**: flat pads make the module and
   its other fine-pitch parts much easier to solder.
2. Order a **stencil** with the boards (see below).

> [!TIP]
> **Get a stencil**
>
> Even if you hand-solder the rest, use a stencil and solder paste for the
> Raytac module. Its pads are underneath it and can't be reached with an iron
> afterwards.

## Bill of materials

TODO: table per board (designator, value, footprint, part number), or a link
to the BOM files. Parts from the stock board below can be skipped.

### Controller

TODO

### Dongle

TODO

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

## Part placement

Open the release's `ibom.html` in a browser; it works offline. Click a BOM
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
