---
title: xbx-nrf TODO
created: 2026-09-28
updated: 2026-09-30
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
- Outline (stock bottom board, FreeCAD; DXF for KiCad `Edge.Cuts`):
  [[mech/controller-bottom-board/controller-bottom-board.FCStd]],
  [[mech/controller-bottom-board/controller-bottom-board-outline.dxf]];
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
- [ ] Each stick's orientation (which output is X/Y, axis direction; check
      again with the TMR modules).
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
- [ ] Stick supply: **1.8 V** LDO with enable (like AN+; lower consumption),
      pulsed around each sample like the trigger supply. Check the TMR sticks'
      1.8 V rating, current and settling time.
- [ ] Trigger sensor supply: switched, pulsed (on, ≥ 100 µs, sample, off).
- [ ] No back-powering: float GPIOs before a rail goes down.

### MCU / radio
- [x] MCU: **nRF52840** (no audio in v1).
- [ ] Module: **Raytac MDBT50Q-U1MV2** preferred (as the dongle; u.FL to the
      stock antennas via coax); check its size and fit.
- [ ] **Stock antennas: verify before ordering the PCB.** Top board: `ANT` (Z4,
      Z5, Z6 → J5/J6), `ANT1` (Z1, Z2, Z3 → J8). Stock SoC board: coax J1, J2.
      One was Xbox Wireless, one Bluetooth.
	- [ ] Which connector goes where; connector/cable type.
	- [ ] Find the **2.4 GHz** antenna (return loss). VNA: NanoVNA V2 / SAA-2N
	      (3 GHz) or LiteVNA 64 (6.3 GHz); NanoVNA-H/H4 only reach ~1.5 GHz.
	- [ ] Top board's matching network at 2.4 GHz.
	- [ ] Pi network on our board (3 footprints, VNA-tuned, 0 Ω bypass).
	- [ ] Fallback: chip-antenna module at the shell edge.
- [ ] Analog inputs: 8 on the nRF52840; 4 stick + 2 trigger + 1 battery = 7
      (8 if the 1.8 V stick rail is measured).
- [ ] RF layout: short 50 Ω CPW to the coax, ground vias, away from motors and
      battery wiring.
- [ ] SWD header / pads.
- [ ] USB D+/D− from J3 (90 Ω pair); ESD on the top board?

### Inputs
- [x] Sticks: **TMR** (low noise, no wear drift).
- [ ] Stick wiring gives the report's directions (right, up = positive),
      so no firmware inversion is needed. The breadboard stick reads X
      reversed.
- [ ] Calibration button on its own GPIO, reachable without opening the
      shell (battery bay?) ([[#Stick and trigger calibration]]).
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

- [ ] PCA10059: prototype and supported off-the-shelf dongle (overlay with
      the mode switch on P0.29; build default). Test HID, XInput, rumble on
      it.
- [ ] **Custom dongle PCB** (the plan), module **Raytac MDBT50Q-U1MV2**
      (u.FL; 32.768 kHz crystal; USB with VBUS and VDDH):
	- [ ] Power from USB 5 V on VBUS + VDDH (no LDO); VDD becomes REG0's
	      output: decoupling per the module's high-voltage-mode reference.
	- [ ] USB-C (CC: 5.1 kΩ to GND each), ESD diodes at the connector,
	      VBUS ≤ 10 µF.
	- [ ] Antenna: Kyocera AVX **1003893FT-AA10L0050** (FPC, 87 %, 50 mm
	      cable, u.FL) flat on the lid, over no PCB copper; flat puck on a
	      USB cable, USB at the rear. Backup: Taoglas CBD01.07.0100C cable
	      dipole along the front wall (straight, ≥ 15 mm from metal).
	      Compare both with the dongle's rx/lost/RSSI stats.
	- [ ] USB mode switch read at boot ([[#Dongle]] step 4), pair button,
	      status LED (sized for the VDD chosen below); avoid P0.09/P0.10
	      (NFC) and P0.18 (reset).
	- [ ] SWD on J1 (JST-SH, Pi pinout): first flash over SWD.
	- [x] Firmware: board `xbx_dongle/nrf52840` (`firmware/boards/xbx/`),
	      pins from the Rev1 schematic. Normal voltage mode (3.3 V LDO), so
	      no REGOUT0 change.
	- [ ] Order 5–6 modules in one DigiKey Marketplace order ($25 flat
	      shipping), shared with the controller.

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

- [ ] **Update the default boards in `build-unsigned.sh`** as hardware moves
      on: dongle → PCA10059 done; controller → custom board when it exists.
      Custom dongle board: `xbx_dongle/nrf52840` becomes the default once
      Rev1 works; PCA10059 stays supported.
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
- [x] Step 1: fixed test pipes 1–4 (`CONFIG_XBX_TEST_PIPE`), per-pipe stats
      and ACK payloads on the dongle, pipe n = XInput slot n − 1; HID mode
      follows the first controller. Fake input: circling sticks, random
      buttons. Two Pro Micros: both received, slots connect/disconnect
      independently. Without TDMA one controller starves: ~40 of 1000
      reports/s get through (avg attempts 1.97), its slot flaps. The phase
      doesn't drift (crystals), so it stays starved; the retry collides too.
      Rumble routing: test after TDMA.
- [x] Step 4.1: pairing mode and storage. Dongle: random address (base 1 +
      7 prefixes, hardware RNG) created once, `pair/addr`; pair button
      (`pair-sw`), blue LED (`pair-led`) blinks. Controller: Pair = P1.01
      to GND held 3 s or at power-on, on-board LED blinks. Both enter
      pairing at boot while unpaired; 30 s timeout. No radio change yet.
      Tested: address persists, LEDs/buttons/pin enter pairing.
- [ ] Per-dongle random address; pipe 0 = pairing, pipes 1–7 = controllers;
      pairing table in flash (settings).
- [ ] Pairing mode (Pair hold / dongle button / unpaired at plug-in), channel
      scan, PAIR_REQ/OFFER/CONFIRM/DONE (repeat until answered), RSSI check.
- [ ] Channel choice at first use (quietest of the candidate list).
- [x] Step 2, TDMA (protocol v2): 1 ms dongle frame (TIMER3), 2 slots, first
      free slot per link; timing error in ACKs, controller shifts one
      TIMER3 period; no retries. Two Pro Micros: both 971–1001/s (was ~40
      for the starved one), err within ±30 µs (mostly 0/±11: thread
      jitter), weaker link (−48 dBm) loses ~1 % without retries; rumble
      reaches the right controller.
- [x] Step 3: join (sparse attempts at random phases), full flag, 2 ms frame
      for 3–4 controllers and back (slots compacted). Two Pro Micros with
      `CONFIG_XBX_FAST_FRAME_MAX=1`: switches between 1 kHz and 500 Hz as
      controllers are plugged and replugged; replugged controllers rejoin.
      Default build: both at 1 kHz.
- [ ] TDMA: margin check. Measured transaction
      333–345 µs → ~155 µs margin in a 500 µs slot.
- [ ] USB: 4 players via the XInput mode (Xbox 360 Wireless Receiver); HID
      stays single-player.
- [ ] Test: 2 controllers at 1 kHz (two Pro Micros + PCA10059), then 4 at 500
      Hz (more Pro Micro clones).
- [ ] Factory reset combo; error blinks (mismatch, ambiguity, full).
- [ ] Tune: pairing timeout, RSSI threshold (drop timeout set: 1000 ms).

### Dongle
USB modes, in order; one active at a time.
1. [x] **HID gamepad, 1 player** ([[docs/protocol#USB HID mode]]):
	- [x] Move the ESB receiver code into `radio.c` (no behaviour change);
	      re-run the link test.
	- [x] USB device: own context, VID `0x1209` / test PID `0x0001`, strings,
	      serial from `DEVICEID`; board's CDC-at-boot off.
	- [x] `CONFIG_XBX_USB_CONSOLE`: CDC ACM console as a composite function, on
	      in `build-unsigned.sh`, off in `build-signed.sh`.
	- [x] HID interface + report descriptor; check with `lsusb -v`,
	      `evtest`. evtest: every control maps as designed.
	- [x] Radio → HID: button remap, D-pad → hat, Y inversion; latest-wins
	      submit; neutral report on link loss (1000 ms). evtest: ~1000
	      updates/s, neutral report 0.999 s after unplugging.
	- [x] Output report → rumble/LED in the ACK payload (replaces fake
	      rumble); reset on USB and link loss. Test: `printf` to
	      `/dev/hidrawN`, watch `rumble[…] led` on the controller. OUT
	      endpoint must exceed the report size (else reports merge).
	- [x] Steam: detected, correct layout, Steam Input works (breadboard
	      input). Rumble: step 3 (PID).
	- [x] Finalize the descriptor in `protocol.md`.
2. [ ] **XInput, 1–4 players** as an **Xbox 360 Wireless Receiver** (4
       interfaces, in-band connect/disconnect; after GP2040-CE / `xpad`): 8-bit
       triggers, 2 motors, no Share. pid.codes IDs: `xpad` binds any `0x1209`
       interface of the receiver type (FF/5D/81). Windows untested: check
       whether its driver binds the receiver type with our IDs. Slot 0 done;
       slots 1–3 with M2b.
	- [x] Mode strap (P0.06 to GND = XInput, PID `0x0002`) and XInput USB
	      class: 4 interfaces, interrupt IN/OUT 32 bytes at 1 ms, presence
	      packets and presence-query replies. Test: `xpad` binds all 4,
	      no gamepad until a controller connects.
	- [x] Slot 0 bridge: radio → 360 report, connect on link up,
	      disconnect after the link timeout. Test: `evtest`, Steam.
	- [x] Output: rumble → heavy/light separately, LED pattern → Guide
	      LED, power-off command. Test: `fftest`, `sdl-rumble` low/high.
	- [x] XInput section in `protocol.md`.
3. [x] **HID PID force feedback** (HID mode game rumble via `hid-pidff`).
       Sine only: the kernel emulates `FF_RUMBLE` as a sine (⅔ strong +
       ⅓ weak), so SDL/Steam rumble works but as one blended strength;
       separate heavy/light needs XInput. Engine on the dongle:
       output = 4 motor levels in the existing output report, no radio
       change; trigger mixing stays controller-side
       ([[#Trigger rumble (controller-side)]]).
	- [x] PID descriptor (sine + envelope, required reports), effect table,
	      block load / pool / free, device control, gain. Test: no
	      `hid-pidff` errors in `dmesg`, `fftest` uploads a sine.
	- [x] Engine: active effects → motor strength every 1 ms (magnitude ×
	      effect gain × device gain; duration, delay, loops), max with the
	      vendor report, both motors. Tested: `fftest` (gain, delay,
	      duration, summing), SDL `SDL_JoystickRumble`.
	- [x] PID section in `protocol.md`.
4. [ ] **Mode switching:** physical switch on the dongle PCB, read once at
       boot before USB starts (Pro Micro: jumper, see step 2; PCA10059: a
       jumper on a spare pad, or its button held at plug-in).
5. [x] Open-source license + `LICENSE` file (MIT / CERN-OHL-S-2.0 /
       CC-BY-4.0, REUSE compliant).
6. [ ] USB IDs: pid.codes test PIDs `0x0001`–`0x0004` for now (dongle HID /
       XInput, controller HID / XInput: one per interface set, since Windows
       and SDL/Steam cache drivers and mappings per VID:PID). Apply for our
       own once the repo is public; decide first whether MCUboot serial
       recovery needs one too.

### Controller
- [ ] Power state machine: hold pin 9 early → PCAL6416 setup → run;
      long-press Guide / idle / no dongle / low battery → release hold.
- [ ] Boot reason: Guide / Pair (→ pairing) / USB.
- [ ] Power-off from the PC: the dongle already decodes XInput power-off
      (`00 00 08 C0`, sent by `xpad` when Guide is held 5 s); forward it as
      a bit in the output report `flags` → controller shuts down (needs the
      power state machine). HID mode has no such command.
- [ ] Find out how Steam turns controllers off: it can over Bluetooth;
      unknown whether it does for `xpad` / USB receivers.
- [ ] USB-powered "off" state.
- [ ] PCAL6416 polling ([[docs/pcal6416#Firmware notes]]).
- [ ] Stick/trigger ADC: internal 0.6 V reference, gain 1/3 → 0–1.8 V at
      12 bits (not ratiometric; calibration absorbs the LDO offset; if it
      drifts, measure the 1.8 V rail on the 8th analog input). Stick and
      trigger supplies pulsed around each sample.
- [ ] Rumble PWM ([[docs/a3910#Firmware notes]]).
- [x] Rumble safety: rumble/LED values off if no ACK payload for 100 ms
      (`OUTPUT_TIMEOUT_MS`); the motor driver will read these values.
- [ ] Battery monitoring, low-battery shutdown.
- [ ] **Wired USB gamepad mode:** a PC enumerating the controller = wired
      (radio off); a charger alone keeps it wireless. `CONFIG_XBX_WIRED`
      (default on) off for wireless tests with the USB console attached.
	- [x] Share the dongle's HID gamepad and PID code in `firmware/common`
	      (dongle unchanged).
	- [x] Controller USB device "XBX-NRF Gamepad", test PID `0x0003`: HID
	      gamepad + PID + CDC console (development builds).
	- [x] Wired switch: enumerated → radio off, input → HID, rumble (PID /
	      vendor report) → local motors; unplugged → radio again.
	- [x] Mode at plug-in, 8BitDo-style: X held → XInput, B held →
	      HID; remembered in flash until changed. Read at boot for now;
	      plugged in while running needs a USB restart (power state
	      machine).
	- [x] Wired XInput (test PID `0x0004`): wired 360 interface (FF/5D/01),
	      20-byte report, rumble `00 08 …`, LED `01 03 …`, `xpad`'s vendor
	      "magic" request.
	- [ ] Windows: HID PID rumble (Windows' PID driver likely also wants a
	      PID State report and Axes Enable) and wired XInput (driver binds
	      by interface class?).

### Stick and trigger calibration
Framework now on the breadboard (raw ADC counts, board-independent); tune the
constants (deadzone, drift window, settle time) on the real TMR sticks.
- [x] Calibration data in flash (Zephyr settings on the storage partition):
      per axis min / centre / max and direction.
- [x] On-device routine: calibration button → move sticks and triggers
      through their full range → press again → saved. Breadboard: a button
      combo stands in (all header pins are used).
- [x] Radial inner deadzone, outer saturation (full deflection = ±32767).
- [x] Boot-time centre check: re-centre within a small window of the stored
      centre, else keep it (stick held at power-up).
- [ ] Triggers on the same data: stops, remapping, hair trigger.
- [ ] Commands for the host program ([[#Calibration program]]).
- [ ] User page `docs/controller.md`: wired / wireless, X / B mode,
      calibration controls. Write once the calibration button and Guide LED
      exist (the routine's controls change with the hardware).

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
- [ ] Controller signed build (needs an MCUboot-capable board;
      `release.conf` checked with an unsigned build).
- [ ] Update method: MCUboot serial recovery over USB (`mcumgr`/`smpmgr`).
- [ ] Downgrade protection.
- [ ] Release: lock APPROTECT.
- [ ] **MCUboot vs. power hold:** MCUboot runs before the app raises pin 9.
      Measure; if too slow, raise pin 9 from an MCUboot hook.

---

## 6. Prototyping milestones

- [x] **M1:** 1 kHz link, latency and loss measured ([[#Link]]).
- [x] **M2:** dongle works as HID, then XInput ([[#Dongle]]).
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
- **4-value output report:** exists (HID vendor report: heavy, light, LT,
  RT); games and Steam don't use it.

### GIP dongle mode
Optional third USB mode speaking Microsoft's protocol ([[docs/gip]]). Gains:
impulse triggers in Windows games that use them, native Share, 10-bit triggers.
Feasible (no auth on PC via the opt-out GUID; GP2040-CE as reference), but
metadata needs Microsoft's compiler, and on Linux `xpad` drops trigger rumble.
- [ ] **Test first:** genuine Series controller over USB, SDL trigger rumble
      (`SDL_JoystickRumbleTriggers`), with and without Steam, on Linux. No
      buzz → GIP isn't worth it on Linux.
- [ ] Get the "gipdocs" download (metadata compiler, gamepad JSON template).
- [ ] Decide VID/PID (Microsoft's for `xpad` auto-binding, or bind manually).

### Calibration program
Small OS-agnostic host tool (Python + `hidapi`, or similar): live stick and
trigger view, guided calibration, deadzone / curve / trigger-stop settings.
- [ ] Transport: vendor HID interface (no driver on any OS). Over the
      controller's wired USB first; via the dongle needs a reliable radio
      command channel.
- [ ] Command set, shared with the on-device routine's stored data.

---

## Open questions

- Is the latch self-holding (expected: no)?
- Is J3 pin 13 Pair sense? What is pin 11?
- Does a Play & Charge pack need I2C commands to charge?
