---
title: xbx-nrf TODO
created: 2026-09-28
updated: 2026-10-31
tags:
  - xbx-nrf
  - todo
---

# xbx-nrf TODO


## Verify / reverse-engineer (stock hardware)
- [ ] Source the J5 replacement: female, SMT, ~3.5 mm tall, body within the
      9.5 × 3.25 mm shroud, pin 1 chamfer (or the shroud corner gets cut). No
      direct match found yet; test-fit candidates on J3.
- [ ] Headset jack: dropped for v1 (no audio; I/O is short).
- [ ] **J7** (Play & Charge: 1 status, 2 SCL2, 3 SDA2, 4 VBUS): dropped for
      v1; charge the AAs externally.
- [ ] **J8** (expansion port): dropped for v1. Stock: 1/12 GND, 2 SDA2, 3
      SCL2, 6 3.3 V, the rest likely digital audio. Keep its opening clear.
- [ ] No back-powering: float GPIOs before a rail goes down.
- [ ] Next rev: a test point on every module GPIO, to check each joint
      after assembly (short to GND / drive and read back over SWD).

---

## Firmware
- [ ] Adaptive channel instead of hopping: the dongle watches loss and noise
      on its channel and scans the other candidates in the background; when
      the channel degrades it announces "channel N at frame F" in the ACK
      payloads and everyone switches at that frame. A controller that misses
      it loses the link and rejoins. Hopping every frame would need a shared
      sequence and frame count, a dongle retune (~40–130 µs) in the last
      slot's margin, and harder joining and pairing. Only if interference
      shows up in use.
- [ ] Tune: pairing timeout, RSSI threshold (drop timeout set: 1000 ms).
- [ ] Power on bug: controller turns on PC then turns off instead of staying on
2. [ ] **XInput, 1–4 players** as an **Xbox 360 Wireless Receiver** (4
       interfaces, in-band connect/disconnect; after GP2040-CE / `xpad`): 8-bit
       triggers, 2 motors, no Share. pid.codes IDs: `xpad` binds any `0x1209`
       interface of the receiver type (FF/5D/81). Windows untested: check
       whether its driver binds the receiver type with our IDs. All 4 slots
       work (tested with 4 controllers); open only for Windows.
6. [ ] USB IDs: pid.codes test PIDs `0x0001`–`0x0004` for now (dongle HID /
       XInput, controller HID / XInput: one per interface set, since Windows
       and SDL/Steam cache drivers and mappings per VID:PID). Apply for our
       own once the repo is public, bootloaders too (now `0x0005` dongle,
       `0x0006` controller).
- [ ] Test `build.sh <xbx-controller|xbx-dongle>-bootloader -f swd --hex`
      with a release `.hex` on both boards.
- [ ] Boot reason: Guide / Pair (→ pairing) / USB.
- [ ] Pair shouldn't power the controller on (SW2 → R46 → Q14 does in
      hardware). On AAs, with Pair held at boot and Guide not, release the
      hold; or remove R46 (Pair still reaches the PCAL6416).
- [ ] Find out how Steam turns controllers off: it can over Bluetooth;
      unknown whether it does for `xpad` / USB receivers. Steam shows no
      "Turn off" for the dongle (`1209:0002`), likely as its own drivers
      only know listed IDs.
- [ ] USB-powered "off" state: System OFF for now (Guide wakes it, as a
      reset); the stick supply stays on.
- [ ] Steam / SDL map Share and the paddles only with our mapping
      ([[docs/protocol#Input report (dongle → PC)]]): the configurator
      installs it per user (Linux, HID mode; tested in Steam). Built in
      needs our own USB IDs and an SDL_GameControllerDB entry (one per
      device: dongle, wired).
- [ ] Steam / SDL don't show it: SDL's Linux joystick backend never reads
      `power_supply` (SDL2 reports "unknown"). Look into an upstream SDL
      patch; pairs with our own USB ID and an SDL_GameControllerDB entry.
- [ ] **Wired USB gamepad mode:** a PC enumerating the controller = wired
      (radio off); a charger alone keeps it wireless. `CONFIG_XBX_WIRED`
      (default on) off for wireless tests with the USB console attached.
- [ ] Windows: HID PID rumble (Windows' PID driver likely also wants a
      PID State report and Axes Enable) and wired XInput (driver binds
      by interface class?).

### Stick and trigger calibration
- [ ] Tune `CAL_MIN_TRAVEL` and the default threshold with use.
- [ ] Triggers: stops, remapping, hair trigger.

### Configurator
- [ ] Windows and macOS builds (on those systems; `hidapi` path untested).
---

## Future / v2

### Battery packs
- Play & Charge (after v1): what does a pack do with J7 pin 1 (power
  status), and does it need I2C commands to charge?

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
- **4-value output report:** exists (HID vendor report: heavy, weak, LT,
  RT); games and Steam don't use it.
- [ ] **Heavy / weak rumble feels weak in games** (trigger rumble is fine):
      likely the low PWM levels barely spin the motors. A minimum drive
      level per motor (non-zero starts where it spins), measured with the
      configurator's test sliders.
- [ ] **Tune trigger rumble / impulse-trigger feel:** level → PWM curve
  (lowest level that still spins, perceived steps), response to short
  pulses (impulse effects are brief clicks and ticks), and how the mixing
  and local effects above feel. Test with SDL trigger rumble and, later,
  [[#GIP dongle mode]] games.

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

## References

- Schematics: [[references/third-party/XB1 1914 TOP BOARD.pdf]],
  [[references/third-party/XB1_1914_BOTTOM_BOARD_SOME_VALUES.pdf]],
  [[references/third-party/XB1_1914_SOC_SOME_VALUES.pdf]]
- Pinouts: [[references/third-party/controller-pinouts.txt]],
  [[references/third-party/soc-pinout.txt]]
- Notes: [[docs/hardware|Hardware]], [[docs/pcal6416|PCAL6416]],
  [[docs/a3910|A3910]], [[docs/protocol|Protocols]], [[docs/gip|GIP]],
  [[docs/building|Building]], [[docs/flashing|Flashing]]
- Datasheets: [[references/third-party/A1304-Datasheet.pdf]] (trigger sensor,
  7.7 mA,
  t_PO 50–70 µs), [[references/third-party/A3910-datasheet.pdf]],
  [[references/third-party/PCAL6416A-datasheet.pdf]]
- Outline (stock bottom board, FreeCAD; DXF for KiCad `Edge.Cuts`):
  [[mech/controller-bottom-board/controller-bottom-board.FCStd]],
  [[mech/controller-bottom-board/controller-bottom-board-outline.dxf]];
  KiCad: `pcb/xbx-nrf/`

