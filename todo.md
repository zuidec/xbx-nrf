---
title: xbx-nrf TODO
created: 2026-09-28
tags:
  - xbx-nrf
  - todo
---

# xbx-nrf TODO

Replacement bottom (MCU) board for the Xbox Series X|S controller (model 1914), built around a Nordic nRF, plus a 2.4 GHz USB dongle. The stock top board (buttons, USB-C, power latch, boost converters) stays in place.

> [!info] Design decisions so far
> - **Radio:** nRF on both ends, ESB/Gazell proprietary 2.4 GHz link, 1 kHz input reports, rumble/LED in ACK payloads.
> - **MCU:** **nRF52840** (locked), on both the controller and the dongle. **No headset audio in v1**; see [[#Future / v2]].
> - **Dongle USB:** HID gamepad mode (built first) + XInput mode (`045E:028E`, default for Windows games), switchable. Xbox consoles unsupported (no auth).
> - **Power:** **rechargeable AA cells only** (NiMH or USB-C Li-ion AAs with regulated 1.5 V output). **No bare Li-ion cell** — the top board's boosts (TPS613221/TPS613226) are boost-only and would pass >3.6 V to the 3.3 V rail.
> - **Antenna (for now):** reuse the stock Xbox antennas on the top board (coax to our board + matching network). To be verified; see [[#MCU / radio]].
> - **Power switching:** reuse the top board's hard power latch (Q6/Q7 switch, Q9 driven by Guide via Q8 or Pair via Q14, held on through J3 pin 9). No load switch needed for the top board.
> - **Build order:** Series X|S first; Elite Series 2 port later.

## References

- [[references/XB1 1914 TOP BOARD.pdf]] — top board schematic (power latch, boosts, PCAL6416 expander, J3)
- [[references/XB1_1914_BOTTOM_BOARD_SOME_VALUES.pdf]] — bottom board schematic (sticks, triggers, rumble, headset, J7/J8)
- [[references/XB1_1914_SOC_SOME_VALUES.pdf]] — SoC module schematic
- [[references/controller-pinouts.txt]] — J3/J5 pinout + test points
- [[references/soc-pinout.txt]] — SoC module pinout
- [[docs/pcal6416|PCAL6416 notes]] — U4 button expander: port → button map, connections, firmware notes
- [[docs/flashing|Flashing & debugging]] — programming/debugging the Pro Micro boards, the dongle and the custom board (ST-Link/OpenOCD, bootloaders, APPROTECT)
- [[docs/a3910|A3910 notes]] — U4/U5 (bottom board) rumble drivers: stock wiring, logic table, design + firmware notes
- [[references/A3910-datasheet.pdf]] — Allegro A3910 datasheet (Rev. 3)
- [[references/PCAL6416A-datasheet.pdf]] — NXP PCAL6416A datasheet (Rev. 7.1)
- [[references/A1304-Datasheet.pdf]] — trigger Hall sensor (7.7 mA typ / 9 mA max, t_PO 50–70 µs)
- [[references/pcb-cad.FCStd]] / [[references/pcb-cad-pcbSketch.dxf]] — FreeCAD outline sketch + DXF export
- KiCad project: `pcb/xbx-nrf/`

---

## 1. Verify / reverse-engineer (stock hardware)

### Power latch (top board)

> [!note] Current reading of the schematic
> - **Guide** (SW13) → Q8 → Q9 gate, and **Pair** (SW2, via R46) → Q14 → Q9 gate. Either button turns power on (Q9 → Q6/Q7 → `3v/B+` → boosts).
> - **Hold:** J3 pin 9 → R7 (0 Ω) → D26 → Q9 gate. The same net drives the [[docs/pcal6416#Connections|PCAL6416 reset]].
> - The latch is **probably not self-holding**, so firmware must drive pin 9 high before the button is released.
> - **J3 pin 13** → D30 is probably the **Pair sense** line (active-low, like Guide on pin 2 via D27), **not** a force-off input.

- [x] Trace **R46**'s left-hand connection → **SW2 Pair / PCAL6416 P10 net** (the Pair power-on path, not a latch loop).
- [ ] With the bottom board unplugged, power the AA wires from a bench supply (2.0–2.8 V) and tap Guide. Watch TP35 (3.3 V):
	- [ ] Does 3.3 V **drop on release** (expected: not self-holding)?
	- [ ] Repeat with Pair; it should also power on.
	- [ ] Hold the button down and time how long the power stays up. Boot → pin 9 high must happen well within a normal button press.
	- [ ] While holding, pull J3 pin 9 high (about 1 kΩ to 3.3 V), release the button, and confirm the power stays on. Then release pin 9 and confirm it turns off.
- [ ] Trace **R25**'s source.
- [ ] Confirm **Q8** and **Q14** polarity (both PNP expected) using their part markings or a diode test.
- [ ] Confirm **J3 pin 9 (`~RESET`)** holds power via R7 → D26 → Q9 gate *and* drives the PCAL6416 reset (R4/EG2).
- [ ] Confirm **J3 pin 13** is the Pair sense line: with a pull-up to 3.3 V on pin 13, it should read low while Pair is pressed.
- [ ] Figure out how the stock SoC turns the controller off. Releasing pin 9 is expected to be enough; confirm there's no separate off path.
- [ ] Measure **standby current** from the AA supply with the controller off. This is the floor for the standby budget.
- [ ] Check behavior with **USB plugged in**: U1 (MP1476L) powers `3v/B+` from 5 V regardless of the latch, and Q6 isolates the AAs.

### J3/J5 connector (top ↔ bottom)
- [ ] Identify the connector part number and pitch; source the mating half.
- [ ] Confirm the full pinout against the schematic. Pins 11 and 13 are unlabeled in [[references/controller-pinouts.txt]].
	- [ ] Pin 11: SoC pin 1 / J7 pin 1. Is it Play & Charge related?
	- [ ] Pin 13: SoC pin 10. Probably the Pair sense line via D30 (see the power latch section).
- [ ] Confirm pin 2 (GU via D27) is active-low and needs a pull-up on the bottom-board side.
- [ ] Measure 3.3 V / 3.6 V / `3v/B+` on J3 with the controller on vs off.

### PCAL6416 expander (top board)
Details: [[docs/pcal6416|PCAL6416 notes]]
- [x] Map every expander port (P00–P17) to its button → [[docs/pcal6416#Port → button map]]
- [x] Note that INT is **not connected**, so button reads must be polled.
- [x] Add the PCAL6416A datasheet to `references/` and verify the register map → [[docs/pcal6416#Register map]]
- [ ] Confirm the I2C address `0x21` (ADDR tied to 3.3 V) with an I2C scan.
- [ ] Check whether the I2C pull-ups are on the top board or the bottom board.
- [ ] Check each button against the map on real hardware.

### Bottom board (being replaced)
- [x] Confirm the thumbstick modules are soldered to the bottom board (yes, per teardown).
- [x] Stick footprint: Alps **RKJXV122400R** (SnapMagic symbol, footprint and STEP) in `pcb/xbx-nrf/lib/RKJXV122400R/`, imported into the KiCad project. Pads checked against [[references/product_catalog_rkjxv.pdf]] drawing No. 1.
- [ ] Verify that the stock sticks really are RKJXV122400R (caliper the pin pitch and pegs, or do a 1:1 print fit check).
- [ ] Record each stick's rotation on the stock board. This decides which of VR1/VR2 is X or Y, and the direction of each axis.
- [ ] Find the push-switch contact pairs (A/B/C/D) with a continuity test on a loose stick.
- [ ] Record the trigger Hall sensor (A1304) placement relative to the trigger magnets.
- [ ] Record the stick pot supply: `AN+` from U6 TLV70718 (1.8 V LDO with enable).
- [ ] Record the trigger sensor supply pulsing: `TRG+` at 125 Hz, 17% duty.
- [x] Identify the rumble drivers: **2× Allegro A3910** (U4: LT + Heavy, U5: RT + Light) → [[docs/a3910#Stock wiring (bottom board)]]
- [ ] Record the motor connection pads (SQ1–SQ8) positions, VBB decoupling capacitor values and CR1–CR4 parts → [[docs/a3910#To verify]]
- [ ] Check whether the headset jack is on the bottom board, and record its position and footprint.
- [ ] **J7** (Play & Charge contacts: VBUS / SDA2 / SCL2): record the footprint and position.
	- [ ] Sniff the I2C2 traffic between the stock SoC and a Play & Charge pack. Does it need commands before it will charge?
- [ ] **J8** (expansion port): record its pinout.
- [ ] Record the mounting holes, board outline and component keep-outs using calipers, relative to a datum hole.

---

## 2. Mechanical

- [ ] Finish the fully constrained outline sketch in FreeCAD (datum = mounting hole at origin).
	- [ ] Confirm the image plane scale along a second horizontal and vertical distance (X was stretched about 4.9%).
	- [ ] Mounting holes, J3 position, stick centers, trigger sensor positions and J7 position, all from caliper measurements.
- [ ] Export the DXF → import onto `Edge.Cuts` in KiCad → replace the hole circles with MountingHole footprints.
- [ ] Do a 1:1 paper print fit check against the stock board.
- [ ] Export a STEP and fit-check it in the shell.
- [ ] Trigger stops (independent of the side rails):
	- [ ] Blu-Tack clearance test behind each trigger.
	- [ ] Choose the approach: fixed internal stop / adjustable screw stop (non-magnetic screw) / modified triggers.
	- [ ] If using click switches at the stop: place the switch on the PCB so the hard stop is reached within the switch's overtravel.

---

## 3. Hardware design — controller board

### Power (rechargeable AA)
- [ ] Power the nRF from J3 3.3 V (VDD, normal voltage mode). Confirm the rail tolerance is within the nRF's VDD max of 3.6 V.
- [ ] Hold / off control:
	- [ ] GPIO → J3 pin 9 (hold + expander reset). Default low at reset; firmware drives it high early in boot.
	- [ ] GPIO ← J3 pin 2 (Guide), with a pull-up to 3.3 V.
	- [ ] GPIO ← J3 pin 13 (Pair sense), with a pull-up to 3.3 V, if confirmed. Lets firmware read Pair while the expander is still in reset.
- [ ] Battery voltage sense: ADC divider on `3v/B+` (J3 pin 3), switched so it draws nothing when idle.
	- [ ] Decide the NiMH low-battery cutoff (about 1.0 V/cell, i.e. 2.0 V pack) and the warning threshold.
	- [ ] Note: NiMH discharge is flat, so the gauge is coarse. Consider coulomb counting in firmware.
	- [ ] Note: USB-C Li-ion AAs output a flat 1.5 V until cutoff, so no gauge is possible with those.
- [ ] VBUS detection (J3 pin 12) → nRF.
- [ ] **Decide: charge in the controller or charge externally?**
	- [ ] Option A (default): charge the AAs externally. No charging hardware needed.
	- [ ] Option B: pass VBUS through to J7 so Play & Charge packs keep working (see the J7 sniffing task).
	- [ ] Option C: an in-controller NiMH charger. Needs a safe way to **detect cell chemistry** so it never charges alkalines. Probably not worth it.
- [ ] Stick supply: LDO with enable (copy AN+ / TLV70718) or a load switch.
- [ ] Trigger Hall sensor supply: switched, and pulsed by firmware (power on, wait ≥ 100 µs, sample, power off).
- [ ] Make sure GPIOs don't back-power switched-off rails (float them before a rail goes down).

### MCU / radio
- [x] Choose the MCU: **nRF52840** (locked; no headset audio in v1).
- [ ] Choose the module: a pre-certified nRF52840 module **with an external-antenna RF pad or u.FL connector** (not the chip-antenna version), since the plan is to reuse the stock antennas. Check its size against the free space on the board.
- [ ] **Stock antennas (plan: reuse them).** The top board has two antenna feeds: `ANT` (Z4, Z5, Z6 network → J5/J6) and `ANT1` (Z1, Z2, Z3 network → J8). The stock SoC board has two coax connectors (J1, J2), each with its own matching network. The stock radio used one antenna for Xbox Wireless and one for Bluetooth.
	- [ ] Trace which top-board connector (J5/J6, J8) goes to which SoC-board connector (J1, J2), and identify the connector/cable type.
	- [ ] Measure both antennas (return loss / resonance) to find the one tuned for **2.4 GHz**. The Bluetooth one should be; the Xbox Wireless one's band is unknown. Needs a VNA that covers 2.4 GHz: **NanoVNA V2 / SAA-2N** (to 3 GHz) or **LiteVNA 64** (to 6.3 GHz). The NanoVNA-H/H4 only reaches ~1.5 GHz.
	- [ ] Check the top board's existing matching network (ANT or ANT1) at 2.4 GHz.
	- [ ] Design a pi matching network on our board between the module's RF pin and the coax connector (footprints for 3 parts; values tuned with the VNA). Include a 0 Ω / DNI option to bypass it.
	- [ ] Fallback if the stock antennas are unsuitable: a module with a built-in chip antenna, placed at the shell edge.
- [ ] Analog input budget: the nRF52840 has **8 analog inputs** (AIN0–AIN7). 4 stick axes + 2 triggers + 1 battery sense = 7, which leaves 1 spare. Assign pins with this in mind.
- [ ] RF layout: a short 50 Ω trace (coplanar waveguide) from the module RF pin to the coax connector, with ground vias alongside; keep motors and battery wiring away from the coax route.
- [ ] SWD programming header / test pads.
- [ ] USB D+/D- from J3 → nRF USB (90 Ω differential pair, short). Check whether the top board already has ESD protection.

### Inputs
- [ ] Sticks: reuse the stock pots or switch to TMR/Hall sticks. Decide.
- [ ] Triggers: A1304 Hall sensors placed at the stock positions (magnet alignment).
- [ ] I2C to the PCAL6416 (J3 pins 5/7) + pull-ups on this board → [[docs/pcal6416#Connections]]
- [ ] Stick click switches (LSC/RSC) and any extra inputs.

### Rumble
Details: [[docs/a3910|A3910 notes]]
- [x] Driver choice: **copy the stock wiring**, 2× A3910, **8 GPIOs** (HIN + LIN per motor). If pins run short later, tie LIN to GND on Heavy/Light (−2 GPIOs).
- [ ] A3910 schematic block: VBB from J3 pin 1 (3.6 V), decoupling, clamp diodes, exposed pad + thermal vias → [[docs/a3910#Schematic checklist]]
- [ ] A3910 symbol (not in KiCad's libraries): draw one, or import LCSC C150818 with `easyeda2kicad`.
- [ ] Motor connection pads matching the stock ones.

### Connectors
- [ ] J3 mating connector (top board).
- [ ] J7 Play & Charge contacts (if Option B).
- [ ] J8 expansion port (keep or drop?).

---

## 4. Hardware design — dongle

- [ ] Prototype on a Nordic PCA10059 (nRF52840 USB dongle).
- [ ] Custom dongle PCB (later): nRF52840, USB-A or USB-C, pairing button, status LED.

---

## 5. Firmware

### Link
Code: `firmware/xbx-nrf/src/main.c` (ESB transmitter), `firmware/dongle/src/main.c` (ESB receiver), `firmware/common/include/protocol.h`. Build: `firmware/build-unsigned.sh <xbx-nrf|dongle> [-p]` (README "Firmware" section). Flash: [[docs/flashing]].
- [ ] **Update the default boards in `firmware/build-unsigned.sh` once the real hardware exists**: `xbx-nrf` → the custom bottom board's board definition, `dongle` → `nrf52840dongle/nrf52840` (both are `promicro_nrf52840/nrf52840/uf2` for now).
- [x] M1 test firmware: ESB at 2 Mbps, fake input every 1 ms, ACK payload replies, per-second stats on USB serial, P0.17 timing pin. Builds for the Pro Micro nRF52840 and the dongle.
- [x] Run it on two Pro Micro nRF52840s (nice!nano clones, nice!nano UF2 bootloader confirmed). **Desk-range results (2026-09-29):** ~993 reports/s, ok 989–994/s, failed 0–2/s, avg attempts 1.00, ACK payloads ≈ ok, RSSI −46 dBm, bad 0, ack-full 0. Data arrives intact both ways.
	- ~993/s instead of 1000: `k_timer` runs on the 32768 Hz system tick, so 1 ms rounds to 33 ticks (1.007 ms).
	- Dongle `lost` matched controller `skipped`: the sequence number advanced on skipped ticks too, so `lost` counted reports that were never sent.
- [x] Fix: report timing from hardware **TIMER3** (16 MHz, exactly 1000 Hz) via the counter driver; ESB uses TIMER2.
- [x] Fix: sequence number only advances when a report is actually sent, so dongle `lost` = lost over the air, controller `skipped` = ticks dropped.
- [x] Re-run with the fixes. **Results (2026-09-29):** exactly 1000 reports/s both sides, skipped 0, avg attempts 1.00, acks = ok, bad 0, ack-full 0, RSSI −55 dBm, **lost 0**. Controller `failed` 0–4/s while dongle `lost` 0 → those reports *were* received; only the ACK back was lost (costs 1 ms of rumble latency, no input data). **Desk-range M1 goal met.**
- [x] First range test (2026-09-29), controller on a battery bank across the room: RSSI −79 to −89 dBm, 0–65 % of reports delivered, bursts of near-total loss. At the 2 Mbps sensitivity limit (~−89 dBm). The clone antennas are weak: only −46 to −55 dBm at the desk.
- [x] TX power 0 → **+8 dBm** on both ends (`XBX_TX_POWER_DBM` in `protocol.h`), so the dongle's ACKs get it too.
- [x] 16-bit input report sequence number (protocol **v1**, input report now 23 bytes), so `lost` counts outages longer than 256 reports correctly. A gap ≥ 0x8000 is treated as a controller restart, not as loss.
- [x] Re-test across the room with +8 dBm (2026-09-29, same spot): RSSI **−69 to −74 dBm** (~10–12 dB better), **98–99.8 % delivered** in most seconds (1–16 lost/s, one second with 78). Remaining loss is likely fades/interference rather than signal margin (~15 dB above the receiver limit) → channel hopping is the better fix than 1 Mbps. Good enough to move on with clone antennas.
	- One second showed `rx 942 lost 2`: ~56 reports never sent (controller-side skips). Check the controller's `skipped` next time both logs are side by side.
- [ ] If still marginal: try 1 Mbps (~+3–4 dB receiver sensitivity, ~2× airtime; recheck `RETRANSMIT_DELAY_US`).
- [ ] Measure latency with the scope on P0.17 ("D2" position; controller: high while a report is in flight; dongle: pulse on receive).
- [ ] Repeat next to busy Wi-Fi and at a distance; try other channels (`XBX_RF_CHANNEL`).
- [ ] Tune `RETRANSMIT_DELAY_US` / `RETRANSMIT_COUNT` (the ESB minimum delay is 435 µs, so only one retry fits in 1 ms).
- [ ] Add a timing pin to the dongle's board overlay once the PCA10059's usable pads are confirmed.
- [x] Packet format v0: 22-byte input report (XInput-style button bits, sticks, triggers, battery, timestamp), 8-byte output report (4 rumble motors, LED) → `firmware/common/include/protocol.h`
- [ ] Pairing flow (pair button / Guide combo).
- [ ] Channel hopping / avoiding Wi-Fi interference.

### Dongle
USB modes, in build order. Only one mode is active at a time; games would see two controllers if both were presented.
1. [ ] **HID gamepad** (Zephyr's built-in USB HID class): report descriptor with 16-bit sticks, 10-bit triggers, all buttons including Share; rumble via an output report. Proves the full path controller → radio → dongle → PC.
2. [ ] **XInput** (custom USB class driver; reference: GP2040-CE, MIT licensed): Microsoft IDs (`045E:028E`), triggers scaled to 8 bits, 2 rumble motors (heavy/light), no Share.
3. [ ] **Mode switching**: choose the mode at plug-in (e.g. hold a button on the controller or dongle), remember it in flash.

### Controller
- [ ] Power state machine: boot → assert hold (J3 pin 9) as early as possible → configure PCAL6416 → run; long-press Guide / idle timeout / no dongle / low battery → shut down → release hold.
- [ ] Boot reason: Guide (pin 2) vs Pair (pin 13) vs USB (VBUS). A Pair wake goes straight into pairing mode.
- [ ] USB-powered "off" state (the latch can't cut power while USB is plugged in).
- [ ] PCAL6416 init + polling (INT not connected) → [[docs/pcal6416#Firmware notes]]
- [ ] Stick and trigger ADC sampling, with trigger sensor pulsing synchronized to the ADC.
- [ ] Trigger range calibration (automatic min/max) + remapping for trigger stops / hair-trigger mode.
- [ ] Stick deadzones and calibration stored in flash.
- [ ] Rumble: PWM on HIN (drive/coast), LIN brake pulse to stop, all inputs low when idle → [[docs/a3910#Firmware notes]]
- [ ] Battery monitoring + low-battery shutdown.
- [ ] Wired mode over USB (controller acts as a USB gamepad directly).

### Firmware signing
Details: [[docs/signing|Firmware signing]]. MCUboot + ECDSA P-256; release builds via `firmware/build-signed.sh`.
- [x] Generate signing keys, one per device: `~/.config/xbx-nrf/keys/controller-p256.pem`, `dongle-p256.pem` (outside the repo; `XBX_KEY_DIR` exported by `~/bin/ncs-shell` only).
- [x] `.gitignore` private key patterns (`*.pem`, `*.key`, `*.p12`, `*.pfx`, `*.der`, `keys/`).
- [x] `sysbuild-signed.conf` per app + `firmware/build-signed.sh`. Signed dongle build tested: validates with the dongle key only.
- [ ] **Back up both keys offline** (password manager / USB stick).
- [ ] Choose the update method: MCUboot serial recovery over USB (`mcumgr`/`smpmgr`) → configure MCUboot (serial recovery, CDC ACM, entry button).
- [ ] Image versioning (per-app `VERSION` file) + downgrade protection.
- [ ] Release checklist: lock the debug port (APPROTECT) on shipped devices.
- [ ] **Controller + MCUboot boot time:** the power latch probably isn't self-holding, so J3 pin 9 must go high before the user releases Guide. MCUboot's signature check runs *before* the app. Measure boot-to-hold time; if it's too long, drive pin 9 from MCUboot (a boot hook) instead.

---

## 6. Prototyping milestones

- [ ] **M1:** two dev boards, fake input at 1 kHz, latency and packet loss measured. Firmware is written and builds; see [[#Link]].
- [ ] **M2:** dongle recognized by Windows/Linux as a **HID gamepad**, then as **XInput** (see [[#Dongle]]).
- [ ] **M3:** dev board wired to a stock top board through J3: buttons via PCAL6416, Guide, power latch hold/off.
- [ ] **M4:** first custom bottom board revision fits the shell; inputs + rumble working.
- [ ] **M5:** battery life measurement on NiMH AAs.
- [ ] **M6 (later):** Elite Series 2 port. Source a broken 1797 as a donor and check the AcidMods 1797 scans.

---

## Future / v2

Out of scope for v1. Kept here so the notes aren't lost.

### Headset audio
- Needs a new board revision: a codec (MAX9867 / WM8960 / NAU88C22), a 3.5 mm jack with plug detect at the stock position, and the audio wiring.
- Consider moving to the **nRF5340** at that point (tunable audio clock, dual core, Nordic's audio reference app). It is **not pin-compatible** with the nRF52840, as a chip or as a module. On the nRF52840, audio is possible over I2S, but clock drift then needs a software resampler.
- Firmware: dongle USB Audio Class interface; mic only (16 kHz mono) first, then stereo ADPCM, then LC3; clock drift compensation; jitter buffer and packet loss concealment.

### Dynamic TX power
- Fixed at +8 dBm for now, which costs battery life at a 1 kHz packet rate.
- Idea: adjust TX power from link quality. The dongle already measures RSSI on every report, and could send it back in the output report (ACK payload). The controller then steps its power down while the link is strong and back up when RSSI or ACKs drop. The dongle can do the same for its ACK power.
- Needs hysteresis (and a fast step up on loss) so the power doesn't oscillate; nRF52840 steps: −40 … +8 dBm.
- Decide after measuring the real hardware's link margin and the battery life at +8 dBm (M5).

---

## Open questions

- Confirm the latch is not self-holding, i.e. firmware must assert pin 9 before the button is released. (Expected from the schematic; see the power latch section.)
- Is J3 pin 13 the Pair sense line?
- What does J3 pin 11 do?
- Does a Play & Charge pack need I2C commands before it will charge?
- Keep the stock stick pots or upgrade to TMR?
