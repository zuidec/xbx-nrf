---
title: xbx-nrf TODO
created: 2026-09-28
updated: 2026-09-29
tags:
  - xbx-nrf
  - todo
---

# xbx-nrf TODO

Locked decisions: [README](README.md). Not yet locked:

> [!info] Working assumptions
> - **Antenna:** reuse the stock Xbox antennas via coax + matching network, if
>   they check out ([[#MCU / radio]]).
> - **Power switching:** stock top board latch; our board holds it via J3 pin 9
>   ([[docs/hardware]]).

## References

- Schematics: [[references/third-party/XB1 1914 TOP BOARD.pdf]],
  [[references/third-party/XB1_1914_BOTTOM_BOARD_SOME_VALUES.pdf]],
  [[references/third-party/XB1_1914_SOC_SOME_VALUES.pdf]]
- Pinouts: [[references/third-party/controller-pinouts.txt]],
  [[references/third-party/soc-pinout.txt]]
- Notes: [[docs/hardware|Hardware]], [[docs/pcal6416|PCAL6416]],
  [[docs/a3910|A3910]], [[docs/protocol|Protocols]], [[docs/gip|GIP]],
  [[docs/building|Building]], [[docs/flashing|Flashing]],
  [[docs/signing|Signing]]
- Datasheets: [[references/third-party/A1304-Datasheet.pdf]] (trigger sensor,
  7.7 mA,
  t_PO 50–70 µs), [[references/third-party/A3910-datasheet.pdf]],
  [[references/third-party/PCAL6416A-datasheet.pdf]]
- Outline: [[references/pcb-cad.FCStd]], [[references/pcb-cad-pcbSketch.dxf]];
  KiCad: `pcb/xbx-nrf/`

---

## 1. Verify / reverse-engineer (stock hardware)

### Power latch (top board)

> [!note] Current reading
> - **Guide** (SW13 → Q8) or **Pair** (SW2 → R46 → Q14) drives Q9 → Q6/Q7 →
>   `3v/B+` → boosts.
> - **Hold:** J3 pin 9 → R7 → D26 → Q9; same net as the
>   [[docs/pcal6416#Connections|PCAL6416 reset]].
> - Probably **not self-holding**: firmware must raise pin 9 before release.
> - **J3 pin 13** (via D30) is probably Pair sense, not a force-off input.

- [x] R46 → SW2 Pair / PCAL6416 P10 net (Pair power-on path).
- [ ] Bench test: bottom board out, 2.0–2.8 V on the AA wires, watch TP35:
	- [ ] Guide press: does 3.3 V drop on release?
	- [ ] Same with Pair.
	- [ ] How long does power stay up while held (boot-to-hold budget)?
	- [ ] Pin 9 pulled high (1 kΩ to 3.3 V) keeps power after release; letting
	      go of pin 9 turns it off.
- [ ] Trace R25's source.
- [ ] Q8 and Q14 polarity (PNP expected).
- [ ] Pin 9 holds power *and* resets the PCAL6416 (R4/EG2).
- [ ] Pin 13 reads low while Pair is pressed (with a pull-up).
- [ ] How the stock SoC powers off (pin 9 release only?).
- [ ] Standby current from the AAs, controller off.
- [ ] USB plugged in: U1 powers `3v/B+` regardless of the latch; Q6 isolates the
      AAs.

### J3/J5 connector (top ↔ bottom)
- [ ] Connector part number and pitch; source the mating half.
- [ ] Full pinout vs. schematic:
	- [ ] Pin 11: SoC pin 1 / J7 pin 1. Play & Charge?
	- [ ] Pin 13: SoC pin 10, probably Pair sense.
- [ ] Pin 2 (Guide) active-low, pull-up on our side.
- [ ] 3.3 V / 3.6 V / `3v/B+` on J3, controller on vs off.

### PCAL6416 expander (top board)
[[docs/pcal6416|PCAL6416 notes]]
- [x] Port → button map ([[docs/pcal6416#Port → button map]]).
- [x] INT not connected → poll.
- [x] Register map checked against the datasheet
      ([[docs/pcal6416#Register map]]).
- [ ] I2C scan finds `0x21`.
- [ ] Where are the I2C pull-ups?
- [ ] Every button on real hardware.

### Bottom board (being replaced)
- [x] Sticks are on the bottom board.
- [x] Stick footprint: Alps **RKJXV122400R** (SnapMagic), in
      `pcb/xbx-nrf/lib/RKJXV122400R/`; pads checked against
      [[references/third-party/product_catalog_rkjxv.pdf]].
- [ ] Stock sticks really are RKJXV122400R (calipers or 1:1 print).
- [ ] Each stick's rotation (which pot is X/Y, axis direction).
- [ ] Push-switch contact pairs (A/B/C/D).
- [ ] A1304 placement relative to the trigger magnets.
- [ ] Stick supply `AN+`: U6 TLV70718 (1.8 V LDO with enable).
- [ ] Trigger sensor supply `TRG+`: pulsed, 125 Hz, 17 % duty.
- [x] Rumble drivers: 2× A3910 (U4: LT + Heavy, U5: RT + Light)
      ([[docs/a3910#Stock wiring (bottom board)]]).
- [ ] Motor pad positions, VBB decoupling, CR1–CR4 parts
      ([[docs/a3910#To verify]]).
- [ ] Headset jack on this board? Position and footprint.
- [ ] **J7** (Play & Charge: VBUS / SDA2 / SCL2): footprint and position.
	- [ ] Sniff I2C2 with a pack: does it need commands to charge?
- [ ] **J8** (expansion port) pinout.
- [ ] Mounting holes, outline, keep-outs (calipers, from a datum hole).

---

## 2. Mechanical

- [ ] Fully constrained FreeCAD outline (datum hole at origin).
	- [ ] Check the image scale on a second horizontal and vertical distance
	      (X was ~4.9 % off).
	- [ ] Holes, J3, stick centers, trigger sensors, J7 from calipers.
- [ ] DXF → KiCad `Edge.Cuts`; holes → MountingHole footprints.
- [ ] 1:1 paper print fit check.
- [ ] STEP fit check in the shell.
- [ ] Trigger stops (independent of the side rails):
	- [ ] Blu-Tack clearance test behind each trigger.
	- [ ] Fixed stop / adjustable screw (non-magnetic) / modified triggers.
	- [ ] Click switches: hard stop within the switch's overtravel.

---

## 3. Hardware design — controller board

### Power (rechargeable AA)
- [ ] nRF on J3 3.3 V (VDD, normal voltage mode; max 3.6 V).
- [ ] Hold / off:
	- [ ] GPIO → J3 pin 9 (hold + expander reset); low at reset, raised early.
	- [ ] GPIO ← J3 pin 2 (Guide), pull-up.
	- [ ] GPIO ← J3 pin 13 (Pair), pull-up, if confirmed; readable while the
	      expander is in reset.
- [ ] Battery sense: switched ADC divider on `3v/B+` (J3 pin 3).
	- [ ] NiMH cutoff (~1.0 V/cell = 2.0 V) and warning threshold.
	- [ ] Flat NiMH curve → coarse gauge; maybe coulomb counting.
	- [ ] Regulated Li-ion AAs: no gauge possible.
- [ ] VBUS detect (J3 pin 12).
- [ ] **Charging:** external (default) / VBUS to J7 for Play & Charge / in
      controller (needs chemistry detection; probably not worth it).
- [ ] Stick supply: LDO with enable (like AN+) or load switch.
- [ ] Trigger sensor supply: switched, pulsed (on, ≥ 100 µs, sample, off).
- [ ] No back-powering: float GPIOs before a rail goes down.

### MCU / radio
- [x] MCU: **nRF52840** (no audio in v1).
- [ ] Module: pre-certified nRF52840 **with an RF pad or u.FL** (for the stock
      antennas); check its size.
- [ ] **Stock antennas: verify before ordering the PCB.** Top board: `ANT` (Z4,
      Z5, Z6 → J5/J6), `ANT1` (Z1, Z2, Z3 → J8). Stock SoC board: coax J1, J2.
      One was Xbox Wireless, one Bluetooth.
	- [ ] Which connector goes where; connector/cable type.
	- [ ] Find the **2.4 GHz** antenna (return loss). VNA: NanoVNA V2 / SAA-2N
	      (3 GHz) or LiteVNA 64 (6.3 GHz); NanoVNA-H/H4 only reach ~1.5 GHz.
	- [ ] Top board's matching network at 2.4 GHz.
	- [ ] Pi network on our board (3 footprints, VNA-tuned, 0 Ω bypass).
	- [ ] Fallback: chip-antenna module at the shell edge.
- [ ] Analog inputs: 8 on the nRF52840; 4 stick + 2 trigger + 1 battery = 7.
- [ ] RF layout: short 50 Ω CPW to the coax, ground vias, away from motors and
      battery wiring.
- [ ] SWD header / pads.
- [ ] USB D+/D− from J3 (90 Ω pair); ESD on the top board?

### Inputs
- [ ] Sticks: stock pots or TMR/Hall?
- [ ] Triggers: A1304 at the stock positions.
- [ ] I2C to the PCAL6416 + pull-ups ([[docs/pcal6416#Connections]]).
- [ ] B, LSC, RSC switches.

### Rumble
[[docs/a3910|A3910 notes]]
- [x] Stock wiring: 2× A3910, 8 GPIOs (LIN to GND on Heavy/Light frees 2).
- [ ] Schematic block ([[docs/a3910#Schematic checklist]]).
- [ ] A3910 symbol (draw, or `easyeda2kicad` C150818).
- [ ] Motor pads at the stock positions.

### Connectors
- [ ] J3 mating connector.
- [ ] J7 Play & Charge contacts (if used).
- [ ] J8 expansion port: keep or drop?

---

## 4. Hardware design — dongle

- [ ] Prototype on the PCA10059.
- [ ] Custom dongle PCB (optional, if the PCA10059 falls short).

---

## 5. Firmware

### Link
Code: `firmware/{xbx-nrf,dongle}/src/main.c`,
`firmware/common/include/protocol.h`. [[docs/building]], [[docs/flashing]].

> [!note] M1 results (2026-09-29, two Pro Micros)
> | Test | Result |
> |---|---|
> | Desk, 0 dBm | ~993/s (k_timer rounding), RSSI −46 dBm |
> | Desk, TIMER3 + seq fix | 1000/s, lost 0, RSSI −55 dBm; `failed` > 0 with `lost` 0 = lost ACKs only |
> | Across the room, 0 dBm | RSSI −79…−89 dBm, 0–65 % delivered |
> | Across the room, +8 dBm | RSSI −69…−74 dBm, 98–99.8 % delivered |
> | Scope, P0.17 | One-way ~232 µs (max 232.2); round trip mean 350.5 µs (332.8 min, 983.9 max = retry, ~2 % of reports); period mean 999.94 µs (999.91–1010) |
>
> Clone antennas are weak. Remaining loss looks like fades/interference →
> channel hopping.

- [ ] **Update the default boards in `build-unsigned.sh`** once real hardware
      exists (custom board; `nrf52840dongle/nrf52840`).
- [x] Test firmware: ESB 2 Mbps, 1 ms fake reports, ACK payloads, stats,
      P0.17 timing pin.
- [x] Report timing from hardware TIMER3 (exact 1 kHz; ESB uses TIMER2).
- [x] Sequence number advances only on sent reports; 16-bit (protocol v1,
      23-byte report); gap ≥ 0x8000 = restart.
- [x] TX power +8 dBm both ends (`XBX_TX_POWER_DBM`).
- [ ] Check controller `skipped` when the dongle shows `rx` < 1000 with low
      `lost` (seen once: `rx 942 lost 2`).
- [ ] If still marginal: 1 Mbps (+3–4 dB; recheck `RETRANSMIT_DELAY_US`).
- [x] Latency on the scope (P0.17; results above). A 33 µs FRFR minimum was
      a measurement artifact.
- [ ] Start each transmission from the TIMER3 interrupt (or PPI) instead of a
      woken thread: today's period jitter is up to ~10 µs, which eats TDMA slot
      margin.
- [ ] Busy Wi-Fi, distance, other channels (`XBX_RF_CHANNEL`).
- [ ] Tune retransmit delay/count (min delay 435 µs → one retry per 1 ms).
- [ ] Dongle timing pin once usable PCA10059 pads are known.
- [x] Packet format v1 ([[docs/protocol#Radio (controller ↔ dongle)]]).
- [ ] Channel hopping (per-dongle sequence, driven by the TDMA schedule).

### Pairing & multiple controllers (protocol v2)
Design: [[docs/protocol#Pairing & multiple controllers (planned, protocol v2)]].
Up to 7 paired (least recently connected replaced), 4 connected; 1 kHz for
1–2, 500 Hz for 3–4.
- [ ] Per-dongle random address; pipe 0 = pairing, pipes 1–7 = controllers;
      pairing table in flash (settings).
- [ ] Pairing mode (Pair hold / dongle button / unpaired at plug-in), channel
      scan, PAIR_REQ/OFFER/CONFIRM/DONE (repeat until answered), RSSI check.
- [ ] Channel choice at first use (quietest of the candidate list).
- [ ] TDMA: dongle frame + slots, timing correction in ACKs, controller TIMER3
      trim; 1 ms / 2 ms frame switching. Measured transaction 333–345 µs →
      ~155 µs margin in a 500 µs slot.
- [ ] Join / full / drop handling; player number = slot.
- [ ] USB: 4 players via the XInput mode (Xbox 360 Wireless Receiver); HID
      stays single-player.
- [ ] Test: 2 controllers at 1 kHz (two Pro Micros + PCA10059), then 4 at 500
      Hz (more Pro Micro clones).
- [ ] Factory reset combo; error blinks (mismatch, ambiguity, full).
- [ ] Tune: pairing timeout, RSSI threshold (drop timeout set: 1000 ms).

### Dongle
USB modes, in order; one active at a time.
1. [ ] **HID gamepad, 1 player** ([[docs/protocol#USB HID mode (planned, M2)]]):
	- [x] Move the ESB receiver code into `radio.c` (no behaviour change);
	      re-run the link test.
	- [x] USB device: own context, VID `0x1209` / test PID `0x0001`, strings,
	      serial from `DEVICEID`; board's CDC-at-boot off.
	- [x] `CONFIG_XBX_USB_CONSOLE`: CDC ACM console as a composite function, on
	      in `build-unsigned.sh`, off in `build-signed.sh`.
	- [x] HID interface + report descriptor; check with `lsusb -v`, `evtest`,
	      SDL `testcontroller`. evtest: every control maps as designed.
	- [x] Radio → HID: button remap, D-pad → hat, Y inversion; latest-wins
	      submit; neutral report on link loss (1000 ms). evtest: ~1000
	      updates/s, neutral report 0.999 s after unplugging.
	- [x] Output report → rumble/LED in the ACK payload (replaces fake
	      rumble); reset on USB and link loss. Test: `printf` to
	      `/dev/hidrawN`, watch `rumble[…] led` on the controller. OUT
	      endpoint must exceed the report size (else reports merge).
	- [ ] Steam: detected, correct layout, Steam Input works.
	- [ ] Finalize the descriptor in `protocol.md`.
2. [ ] **XInput, 1–4 players** as an **Xbox 360 Wireless Receiver** (4
       interfaces, in-band connect/disconnect; after GP2040-CE / `xpad`): 8-bit
       triggers, 2 motors, no Share.
3. [ ] **HID PID force feedback** (HID mode game rumble: DirectInput on
       Windows, SDL/evdev on Linux via `hid-pidff`). PID descriptor and USB
       side (effect IDs, block load, pool) on the dongle. Effect engine on the
       dongle or the **controller** (smoother under packet loss, less radio
       traffic, scales to 4 players; needs a reliable radio command channel).
       Check: `hid-pidff` has no `FF_RUMBLE`, so SDL may fall back to one
       combined sine strength.
4. [ ] **Mode switching** at plug-in, stored in flash.
5. [x] Open-source license + `LICENSE` file (MIT / CERN-OHL-S-2.0 /
       CC-BY-4.0, REUSE compliant).
6. [ ] USB IDs: pid.codes test VID/PID `0x1209:0x0001` for now; apply for our
       own PID once the repo is public.

### Controller
- [ ] Power state machine: hold pin 9 early → PCAL6416 setup → run;
      long-press Guide / idle / no dongle / low battery → release hold.
- [ ] Boot reason: Guide / Pair (→ pairing) / USB.
- [ ] USB-powered "off" state.
- [ ] PCAL6416 polling ([[docs/pcal6416#Firmware notes]]).
- [ ] Stick/trigger ADC, trigger supply pulsed in sync.
- [ ] Trigger calibration + remapping (stops, hair trigger).
- [ ] Stick deadzones, calibration in flash.
- [ ] Rumble PWM ([[docs/a3910#Firmware notes]]).
- [x] Rumble safety: rumble/LED values off if no ACK payload for 100 ms
      (`OUTPUT_TIMEOUT_MS`); the motor driver will read these values.
- [ ] Battery monitoring, low-battery shutdown.
- [ ] Wired USB gamepad mode.

### Build & versions
[[docs/building|Building]]
- [x] SDK pinned in `firmware/ncs-version` (v3.4.1, toolchain 8285d8ad56),
      checked by the build scripts.
- [x] App `VERSION` files (0.1.0); boot log shows version, build ID, protocol.
- [x] `git tag v0.1.0` once M1 is done.

### Firmware signing
[[docs/signing|Signing]]
- [x] One key per device, in `~/.config/xbx-nrf/keys/`.
- [x] `.gitignore` key patterns.
- [x] `sysbuild-signed.conf` + `build-signed.sh`; dongle build verified.
- [x] Image version from `VERSION` (`0.1.0+0`).
- [x] **Back up both keys offline.**
- [ ] Update method: MCUboot serial recovery over USB (`mcumgr`/`smpmgr`).
- [ ] Downgrade protection.
- [ ] Release: lock APPROTECT.
- [ ] **MCUboot vs. power hold:** MCUboot runs before the app raises pin 9.
      Measure; if too slow, raise pin 9 from an MCUboot hook.

---

## 6. Prototyping milestones

- [x] **M1:** 1 kHz link, latency and loss measured ([[#Link]]).
- [ ] **M2:** dongle works as HID, then XInput ([[#Dongle]]).
- [ ] **M2b:** pairing; 2 controllers at 1 kHz, then 4 at 500 Hz
      ([[#Pairing & multiple controllers (protocol v2)]]).
- [ ] **M3:** dev board on a stock top board via J3: buttons, Guide, power
      hold/off.
- [ ] **M4:** first custom board fits; inputs + rumble work.
- [ ] **M5:** battery life on NiMH AAs.
- [ ] **M6 (later):** Elite Series 2 port (broken 1797 donor, AcidMods scans).

---

## Future / v2

### Headset audio
- New board revision: codec (MAX9867 / WM8960 / NAU88C22), 3.5 mm jack with
  plug detect.
- Consider the **nRF5340** then (audio clock, dual core); **not pin-compatible**
  with the nRF52840. On the nRF52840, I2S plus a software resampler.
- Firmware: USB Audio Class; mic first, then ADPCM, then LC3; drift
  compensation, jitter buffer, loss concealment.

### Dynamic TX power
- +8 dBm costs battery at 1 kHz. Dongle sends RSSI back in the ACK payload;
  controller steps power down while strong, up fast on weak RSSI or lost ACKs
  (with hysteresis). Dongle does the same for ACKs.
- Decide after real-hardware link margin and M5.

### Trigger rumble (controller-side)
Games rarely drive the trigger motors, and XInput can't (2 motors only).
- **Rumble mixing:** feed part of the heavy/light rumble into the trigger
  motors when the host sends only 2 values. Configurable strength.
- **Local trigger effects:** click when a trigger crosses a threshold, buzz at a
  trigger stop; generated on the controller, pairs with hair-trigger mode.
- **4-value HID output report** for our own tools (and maybe Steam later).

### GIP dongle mode
Optional third USB mode speaking Microsoft's protocol ([[docs/gip]]). Gains:
impulse triggers in Windows games that use them, native Share, 10-bit triggers.
Feasible (no auth on PC via the opt-out GUID; GP2040-CE as reference), but
metadata needs Microsoft's compiler, and on Linux `xpad` drops trigger rumble.
- [ ] **Test first:** genuine Series controller over USB, SDL `testcontroller`
      trigger rumble, with and without Steam, on Linux. No buzz → GIP isn't
      worth it on Linux.
- [ ] Get the "gipdocs" download (metadata compiler, gamepad JSON template).
- [ ] Decide VID/PID (Microsoft's for `xpad` auto-binding, or bind manually).

---

## Open questions

- Is the latch self-holding (expected: no)?
- Is J3 pin 13 Pair sense? What is pin 11?
- Does a Play & Charge pack need I2C commands to charge?
- Stock stick pots or TMR?
