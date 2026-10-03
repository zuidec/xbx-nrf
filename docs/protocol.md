---
title: xbx-nrf protocols (radio + USB)
created: 2026-09-29
tags:
  - xbx-nrf
  - firmware
  - reference
aliases:
  - protocol
  - radio protocol
  - HID report
  - XInput
---

# xbx-nrf protocols

Two links: **radio** (controller ↔ dongle) and **USB** (dongle ↔ PC: HID and
XInput modes, implemented). Both USB modes reuse the radio data, as would the
planned GIP mode ([[docs/gip]]). The controller can also be a USB gamepad
itself ([[#Controller wired USB mode]]). Tasks: [[todo#Link]],
[[todo#Dongle]], [[todo#Controller]].

## Radio (controller ↔ dongle)

Source of truth: `firmware/common/include/protocol.h`. **Version 3**
(`XBX_PROTOCOL_VERSION`); both boards must run the same version.

### Link

| Setting | Value |
|---|---|
| Protocol | Nordic ESB, dynamic payload length, selective auto-ACK |
| Bit rate | 2 Mbps, fast ramp-up |
| Channel | Per dongle: the quietest of 24, 49, 74, 76, 78 (2400 + n MHz) at first use (`XBX_RF_CHANNELS`, [[#Addresses and channels]]) |
| Address | Pipe 0: pairing address (base 0 "XBXP", prefix `E7`), open only while pairing. Pipes 1–7: one per paired controller, on the dongle's random base address 1 and prefixes ([[#Addresses and channels]]) |
| TX power | +8 dBm, both ends (`XBX_TX_POWER_DBM`) |
| Roles | Controller = PTX, dongle = PRX |
| Report rate | 1000 Hz from hardware TIMER3 (`XBX_REPORT_PERIOD_US`) |
| Retries | None: a retry (ESB minimum 435 µs later) would land in the next controller's slot. Stale reports are flushed, never resent late |
| Max payload | 32 bytes (`CONFIG_ESB_MAX_PAYLOAD_LENGTH`) |

Every 1 ms the controller sends an **input report**. The dongle's ESB ACK
carries the **output report** queued for that controller (one is always kept
queued per pipe), so rumble/LED data costs no extra transmissions. All fields
are little-endian.

Each controller keeps to its own time slot, steered by the timing error in
the output report ([[#Time slots (TDMA)]]). Unsynchronised controllers don't
share a channel: their 1 ms periods don't drift apart, so one that starts
overlapping another stays starved (measured: ~40 of 1000 reports/s).

### Input report (controller → dongle), 23 bytes

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `type` | `0x01` (`XBX_MSG_INPUT`) |
| 1 | 2 | `seq` | +1 per report actually sent; gaps = lost over the air. Gap ≥ 0x8000 = controller restart |
| 3 | 2 | `buttons` | Bitmap below, 1 = pressed |
| 5 | 2 | `lx` | Left stick X, −32768…32767 |
| 7 | 2 | `ly` | Left stick Y, **up = positive** (XInput convention) |
| 9 | 2 | `rx` | Right stick X |
| 11 | 2 | `ry` | Right stick Y, up = positive |
| 13 | 2 | `lt` | Left trigger, 0…1023 |
| 15 | 2 | `rt` | Right trigger, 0…1023 |
| 17 | 1 | `buttons_ext` | bit 0 Share, bit 1 Pair |
| 18 | 1 | `battery` | 0…100 %, `0xFF` = unknown |
| 19 | 4 | `timestamp_us` | Controller uptime at send (latency tests) |

`buttons` uses the **XInput `wButtons` layout**, so XInput mode passes it
through:

| Bit | Button | Bit | Button |
|---|---|---|---|
| 0 | D-pad up | 8 | LB |
| 1 | D-pad down | 9 | RB |
| 2 | D-pad left | 10 | Guide |
| 3 | D-pad right | 11 | *(unused)* |
| 4 | Menu (XInput START) | 12 | A |
| 5 | View (XInput BACK) | 13 | B |
| 6 | LS (stick click) | 14 | X |
| 7 | RS | 15 | Y |

### Output report (dongle → controller, ACK payload), 14 bytes

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `type` | `0x02` (`XBX_MSG_OUTPUT`) |
| 1 | 1 | `seq` | +1 per report queued |
| 2 | 4 | `rumble[4]` | Heavy, light, LT, RT; 0…255 |
| 6 | 1 | `led` | Guide LED brightness, 0…255 |
| 7 | 1 | `flags` | Bit 0: dongle full (no slot free, retry slowly) |
| 8 | 1 | `slot` | Time slot, 0…; `0xFF` = none yet |
| 9 | 1 | `slots` | Slots per frame; frame = `slots` × 500 µs |
| 10 | 2 | `sync_seq` | Input report `seq` the timing was measured on |
| 12 | 2 | `sync_err_us` | Its arrival minus the slot start, signed µs (> 0 = late) |

### Changing the protocol
- Bump `XBX_PROTOCOL_VERSION` and the `_Static_assert` sizes; update this page.
- Mismatched versions show up as `bad` packets on the dongle (length/type
  check).

| Version | Change |
|---|---|
| 0 | Initial: 22-byte input report, 8-bit `seq` |
| 1 | 16-bit `seq` (23-byte input report) |
| 2 | Time slots: `slot`, `slots`, `sync_seq`, `sync_err_us` (14-byte output report); no retries |
| 3 | Pairing: per-dongle random addresses, pairing messages `0x10`–`0x13` |

## Pairing & multiple controllers

> [!note] Status
> Designed 2026-09-29, implemented: addresses, channel choice, pairing mode,
> the exchange and its checks, stored data, factory reset, time slots,
> join/full/drop. Later: channel hopping, encryption. Numbers marked *(tune)*
> are starting values to adjust on hardware.

### Requirements
- A controller only ever talks to the dongle it's paired with.
- Several dongle/controller sets work side by side.
- One dongle: **up to 7 paired** controllers, **up to 4 connected**.
- Report rate: **1 kHz** with 1–2 connected, **500 Hz** with 3–4.
- Pairing needs a deliberate action on **both** sides, and doesn't interrupt
  controllers already playing on that dongle.

### Addresses and channels
- ESB gives a receiver 8 **pipes**: pipe 0 on base address 0, pipes 1–7 on base
  address 1, each pipe with its own 1-byte prefix.

| Pipe | Address | Use |
|---|---|---|
| 0 | Fixed **pairing address** `"XBXP"` + prefix `E7`, same on every dongle | Pairing only; enabled only while the dongle is in pairing mode |
| 1–7 | Dongle's **random base address 1** + 7 distinct random prefixes | One per paired controller |

- The radio drops packets for addresses it isn't listening on (plus CRC), so a
  controller can't reach another dongle, and nobody reaches the pairing pipe
  unless that dongle is in pairing mode.
- Random addresses are chosen once, at first use; patterns resembling the
  preamble (mostly `0x55`/`0xAA`) are rejected.
- **Channel:** each dongle picks the quietest channel from a fixed
  **candidate list** at first use: **24, 49, 74, 76, 78** *(tune)*, i.e.
  2424–2478 MHz in the gaps between Wi-Fi channels 1/6/11, clear of BLE
  advertising. It samples RSSI 400 times per channel (100 ms each) and takes
  the one with the fewest samples above −85 dBm (then the weakest average);
  stored as `pair/chan`, chosen again after a factory reset. A dongle paired
  before channel choice existed keeps 76. Channel hopping later follows a
  per-dongle sequence.
- **Pairing scan:** a pairing controller tries each candidate in turn (one
  request per attempt, sparse as when joining); an ACK means a dongle in
  pairing mode listens there, so it stays (moving on after 3 misses). Two
  dongles pairing on different channels at once aren't detected: the
  controller pairs with the one it finds first.
- **IDs:** each nRF52840's factory-unique 64-bit `DEVICEID` (FICR).

### ESB constraint: answers take two transmissions
The receiver ACKs ~150 µs after a packet arrives, before firmware has read it,
so an ACK can only carry what was queued **before** the packet came in. Every
request is therefore repeated: the first copy gets a plain ACK ("heard you"),
the dongle then queues its answer, and a later copy gets the answer in its ACK.
The controller simply repeats each message every ~2 ms until the answer
arrives.

### Pairing mode
| Side | Enter | While active | Timeout |
|---|---|---|---|
| Dongle | Press its button; automatic at plug-in if never paired | Enables pipe 0 on its own channel; time slots keep running; LED fast blink | 30 s *(tune)* |
| Controller | Hold **Pair** ~3 s, or power on with Pair held | Drops its current link, scans; Guide LED fast blink | 30 s *(tune)* |

Both windows just need to overlap; press order doesn't matter.

### Exchange

```
controller                                   dongle (own channel)
  scan candidate channels:
  PAIR_REQ on pipe 0 (pairing address)  ─▶   (pipe 0 open only in pairing mode)
      no ACK → next channel
      ACK    → stay, repeat (every 5–15 periods)
                                              check RSSI, version, ambiguity;
                                              pick pipe; queue PAIR_OFFER
  PAIR_REQ (repeat)                     ─▶
                                        ◀─   ACK: PAIR_OFFER
  check nonce; switch to assigned pipe
  PAIR_CONFIRM on its pipe (repeat)     ─▶
                                              save entry to flash;
                                              queue PAIR_DONE on that pipe
  PAIR_CONFIRM (repeat)                 ─▶
                                        ◀─   ACK: PAIR_DONE
  save to flash; both leave pairing mode (LEDs solid), dongle closes pipe 0
  controller joins normally (below)
```

Once both sides are in pairing mode, this takes well under a second.

**Dongle checks on `PAIR_REQ`** (step "check" above):
- **Proximity:** RSSI ≥ **−50 dBm** *(tune: `CONFIG_XBX_PAIR_RSSI_MIN`)*,
  i.e. held close; otherwise ignored.
- **Version:** `proto_ver` must match; otherwise `PAIR_OFFER` with status
  "version mismatch" and both show the error blink.
- **Ambiguity:** a second, different `ctrl_id` in the same window → abort,
  error blink. For 2 s the dongle answers every request with status
  "aborted" (so both controllers learn it), then closes pipe 0.
- **Pipe:** known `ctrl_id` → same pipe; else a free pipe; else the **least
  recently connected** entry's pipe.

**Controller checks on `PAIR_OFFER`:**
- `nonce` must echo its own (not someone else's offer).
- Offers from two different `dongle_id`s in one window (two dongles pairing
  nearby) → abort, error blink. To give a second dongle the chance to answer,
  it collects **3 offers** before it confirms (~30 ms).

**Commit order:** the dongle saves when it receives `PAIR_CONFIRM` on the new
pipe (proof the controller has the offer and switched); the controller saves
when it receives `PAIR_DONE`. Flash writes run from a work queue, never in the
radio interrupt.

### Pairing messages
Payloads (little-endian; structs in `protocol.h`):

| Type | Message | Pipe | Fields | Size |
|---|---|---|---|---|
| `0x10` | PAIR_REQ | 0 | `ctrl_id`[8], `proto_ver`, `nonce`[4] | 14 |
| `0x11` | PAIR_OFFER | 0 (ACK) | `status`, `dongle_id`[8], `nonce`[4], `base_addr1`[4], `prefix`, `pipe` (1–7), `channel` | 21 |
| `0x12` | PAIR_CONFIRM | assigned | `ctrl_id`[8], `dongle_id`[8] | 17 |
| `0x13` | PAIR_DONE | assigned (ACK) | `status` | 2 |

Byte 0 of each payload is the type. `status`: `0` ok, `1` version mismatch, `2`
aborted (ambiguity).

### Failure handling
| Situation | Result |
|---|---|
| Only one side in pairing mode | Controller keeps scanning; both time out; nothing changes |
| Controller too far away | Ignored; timeout |
| Two controllers at one dongle | Dongle aborts, error blink |
| Two dongles pairing nearby | Controller aborts, error blink |
| Version mismatch | Error blink on both |
| `PAIR_DONE` lost | Controller repeats `PAIR_CONFIRM`; dongle re-sends `PAIR_DONE` |
| Controller never gets `PAIR_DONE` | Controller saves nothing; the dongle's entry is reused on re-pair or replaced later |
| Re-pairing a known controller | Same pipe, entry updated |

### Stored data (Zephyr settings, flash)
- **Dongle:** `base_addr1`, 7 prefixes, channel; 7 entries of `ctrl_id`, pipe,
  last-connected counter.
- **Controller:** `dongle_id`, `base_addr1`, prefix, pipe, channel.
- **Factory reset:** Pair held **10 s**, on either side (pairing mode starts at
  3 s on the way). The controller forgets its dongle and restarts unpaired
  (it then pairs at boot); the dongle forgets all controllers and its address
  (new random one) and restarts. The LED goes solid for 1 s first.

**LEDs** (dongle: pairing LED; controller: the indicator, on-board on the Pro
Micro): fast blink (5 Hz) = pairing mode; three slow blinks = error (refused,
aborted, version mismatch, dongle full); solid = factory reset.

### Time slots (TDMA)
One radio can't receive two controllers at once, so the dongle runs a
repeating **frame** and gives each connected controller its own **slot**:

| Connected | Frame | Slots | Rate per controller |
|---|---|---|---|
| 1–2 | 1 ms | 2 × 500 µs | 1000 Hz |
| 3–4 | 2 ms | 4 × 500 µs | 500 Hz |

- A transaction (report + ACK) takes ~200–300 µs, so 500 µs slots leave margin.
- **Slot:** the dongle gives a controller the first free slot with its first
  report and frees it on link loss.
- **Sync:** the dongle's frame runs on TIMER3. It times each report's arrival
  against the slot start and returns the error (`sync_seq`, `sync_err_us`).
  The controller makes its next TIMER3 period 1000 µs − error, then 1000 µs
  again. ACK payloads lag a report or two, so it ignores measurements of
  reports sent before its last shift (no double correction). Crystal drift is
  ~20 ns/ms, so this also keeps it in place. Errors within ±15 µs are left
  alone: mostly the controller's thread jitter.
- **No retries** within a frame: a lost report is replaced by the next one.
- **Frame changes:** when more controllers connect than the 1 ms frame serves
  (2; `CONFIG_XBX_FAST_FRAME_MAX`, 1 for testing with two boards), the dongle
  stretches its frame to 2 ms; when they leave, the rest move into slots 0–1
  and it goes back to 1 ms. Controllers follow `slots` and `slot` in their next
  ACK: the report period becomes the frame length and older measurements are
  ignored. Expect a few colliding reports during a switch.
- Pairing traffic on pipe 0 is rare and short; it may cost an occasional report
  in an active slot.

### Connecting and disconnecting
- **Join:** a controller without a slot (just started, or no ACK for 100 ms)
  sends one report every 5–15 periods, each at a random phase, until one lands
  in a gap and its ACK assigns a slot. Collisions with active slots cost them
  at most one report per attempt.
- **Full (no slot free):** the ACK has `flags` bit 0 set; the controller
  retries once a second. Later: show it, power off after a timeout.
- **Drop:** no report from a slot for **1000 ms** → slot freed, USB reports a
  disconnect. Long enough to ride out brief radio dropouts mid-game.
- **Player number:** the first free XInput receiver slot when the controller
  connects (has a time slot); not the pipe or the time slot. HID mode is
  single-player.
- **Replaced entry:** when a full table gives a pipe to a new controller, that
  pipe gets a new random prefix, so the old controller can't use it anymore.
- **Sparse pairing traffic:** a pairing controller sends its messages the way
  a joining one does (every 5–15 periods, random phase), so it doesn't starve
  active slots.

### Messages
| Type | Message | Direction |
|---|---|---|
| `0x01` | Input report | controller → dongle |
| `0x02` | Output report, with the time slot fields | dongle → controller |
| `0x10`–`0x13` | Pairing ([[#Pairing messages]]) | both |

Later in the output report: link quality (RSSI, for
[[todo#Dynamic TX power]]).

### Later
- **Encryption / authentication:** ESB has none; a key from pairing (ECDH) +
  AES-CCM per packet would stop spoofed input. The nRF52840 has hardware for
  both. Not planned for v1.

## USB modes (dongle ↔ PC)

| Mode | Players | Hot-plug | Use |
|---|---|---|---|
| **HID gamepad** | 1 | — | Bring-up, own tools, non-XInput systems |
| **XInput** (Xbox 360 Wireless Receiver emulation) | 1–4 | Yes, in-band connect/disconnect (`xpad`; Windows untested) | Everyday / multiplayer, two-motor rumble |

Plain HID has no way to add or remove a gamepad without re-enumerating the
whole device, hence single-player HID.

**Mode selection:** read once at boot, before USB starts (`usb_mode_get()` in
`usb.c`); changing it needs a reset. Pro Micro: jumper P0.06 to GND = XInput,
open = HID (`mode-gpios` in the board overlay). Dongle PCB: a switch
([[todo#Dongle]]). Each mode has its own PID, so hosts never mix up the two
descriptor sets.

## USB HID mode

Descriptor: `report_desc` in `firmware/common/src/hid_pad.c`, one Gamepad
application collection. Tested with `evtest`, SDL and Steam on Linux.

### Device
| Item | Value |
|---|---|
| VID / PID | `0x1209` (pid.codes) / **`0x0001`** test PID during development |
| Interfaces | 1 HID gamepad; + CDC ACM console in development builds (composite) |
| Reports | Input ID 1, 16 bytes (= `in-report-size` in `usb.overlay`); output ID 2, 6 bytes; PID `0x11`–`0x23` ([[#Force feedback (PID)]]) |
| HID endpoints | Interrupt IN 1 ms; interrupt OUT 1 ms (PID uploads send several reports in a row), max packet 64 (must exceed every output report, or reports merge) |
| Serial number | From the nRF52840 `DEVICEID` |
| Console | `CONFIG_XBX_USB_CONSOLE`: on in `build-unsigned.sh`, off in `build-signed.sh` |

pid.codes rules: test PIDs `0x0001`–`0x000F` are for development only. Our own
PID requires a public repo with an open-source license and a `LICENSE` file
(apply via pull request to pid.codes).

### Input report (dongle → PC)

Layout chosen so Linux's generic HID driver emits the same event codes as
`xpad`; SDL/Steam then map it without a custom mapping.

| Offset | Size | Field | HID usage | Linux event |
|---|---|---|---|---|
| 0 | 1 | Report ID `0x01` | | |
| 1 | 2 | Buttons 1–16 | Button page (see below) | `BTN_*` |
| 3 | 1 | Hat switch (4 bits, 0–7, 8 = centred) + 4 bits padding | Hat switch: logical 0–7 = 0–315° (unit degrees), Null State | `ABS_HAT0X/Y` |
| 4 | 2 | Left stick X | X, logical −32768…32767 | `ABS_X` |
| 6 | 2 | Left stick Y (**inverted**: HID down = positive) | Y | `ABS_Y` |
| 8 | 2 | Right stick X | Rx | `ABS_RX` |
| 10 | 2 | Right stick Y (**inverted**) | Ry | `ABS_RY` |
| 12 | 2 | Left trigger | Z, logical 0…1023 | `ABS_Z` |
| 14 | 2 | Right trigger | Rz, logical 0…1023 | `ABS_RZ` |

| HID button | Control | Linux event |
|---|---|---|
| 1 | A | `BTN_SOUTH` |
| 2 | B | `BTN_EAST` |
| 3 | *(unused)* | `BTN_C` |
| 4 | X | `BTN_NORTH` |
| 5 | Y | `BTN_WEST` |
| 6 | *(unused)* | `BTN_Z` |
| 7 | LB | `BTN_TL` |
| 8 | RB | `BTN_TR` |
| 9, 10 | *(unused)* | `BTN_TL2`, `BTN_TR2` |
| 11 | View | `BTN_SELECT` |
| 12 | Menu | `BTN_START` |
| 13 | Guide | `BTN_MODE` |
| 14 | LS | `BTN_THUMBL` |
| 15 | RS | `BTN_THUMBR` |
| 16 | Share | extra button |

The radio `buttons` bits (XInput layout) are remapped to this order; the D-pad
bits become the hat value.

### Output report (PC → dongle)

| Offset | Size | Field | HID usage |
|---|---|---|---|
| 0 | 1 | Report ID `0x02` | |
| 1 | 4 | Rumble heavy, light, LT, RT (0…255) | Vendor page `0xFF00`, usage `0x01` |
| 5 | 1 | Guide LED (0…255) | Vendor page `0xFF00`, usage `0x01` |

Maps 1:1 onto the radio output report: the dongle forwards the values in the
next ACK payloads (live within ~2 ms). Rumble stays on until changed; it's
reset to 0 when the USB interface goes down or the link is lost.

This vendor report is for testing and our own tools; games and Steam don't
use it (no evdev force feedback). Test: `tools/test-rumble.sh <hr|lr|all>
<0-255>` (1 s pulse via `/dev/hidrawN`). Game rumble uses
[[#Force feedback (PID)]].

### Force feedback (PID)

HID PID reports (usage page `0x0F`) inside the gamepad collection; Linux's
`hid-pidff` binds to them without quirks. Code: `hid_pid.c` (descriptor,
effect table, engine), `bridge.c` (mixing).

**Sine only.** The input core emulates `FF_RUMBLE` on any device with periodic
effects: each rumble becomes a sine with magnitude ⅔ strong + ⅓ weak (50 ms
period). SDL, Steam and Proton use `FF_RUMBLE`, so games rumble, but with one
blended strength: heavy/light separation needs XInput mode. Other effect types
(constant, ramp, conditions) only serve DirectInput wheel/joystick games.
`hid-pidff` needs Set Envelope for any periodic effect, so it's included.

Output reports (after the ID; levels 0…255, times u16 LE in ms):

| ID | Report | Fields |
|---|---|---|
| `0x11` | Set Effect | block, type (1 = sine), duration (`0xFFFF` = infinite), trigger repeat, start delay, gain, trigger button, direction enable (bit 0), direction |
| `0x12` | Set Envelope | block, attack level, fade level, attack time, fade time |
| `0x13` | Set Periodic | block, magnitude, offset (s8), phase, period |
| `0x14` | Effect Operation | block, op (1 start, 2 start solo, 3 stop), loop count |
| `0x15` | Block Free | block |
| `0x16` | Device Control | 1 enable / 2 disable actuators, 3 stop all, 4 reset, 5 pause, 6 continue |
| `0x17` | Device Gain | gain |

Feature reports:

| ID | Report | Direction | Fields |
|---|---|---|---|
| `0x21` | Create New Effect | set | type (1 = sine) |
| `0x22` | Block Load | get | block (0 on failure), status (1 success, 2 full, 3 error), RAM pool available (u16) |
| `0x23` | Pool | get | RAM pool size (u16), simultaneous max (16), bit 0 device-managed pool |

Block indexes are 1–16. Upload: Create New Effect → Block Load returns the
lowest free block → Set Effect / Set Periodic / Set Envelope for that block →
Effect Operation starts it. Direction, trigger and phase are accepted and
ignored.

**Engine** (`hid_pid_strength()`, every radio report, i.e. 1 kHz while
linked): for each playing effect, after the start delay, magnitude with the
envelope applied × effect gain × device gain; effects end after duration ×
loop count. Playing effects add up, capped at 255; 0 while actuators are
disabled or paused.

**Mixing:** heavy and light each take the larger of the PID strength and the
vendor report; LT, RT and the LED are vendor-only. Trigger rumble mixing is
controller-side ([[todo#Trigger rumble (controller-side)]]).

**Resets:**
- Device Control Reset frees all effects but keeps the device gain:
  `hid-pidff` sends it before the first upload, after setting the gain.
- USB interface down: effects freed, gain back to maximum, rumble off.
- Link loss: all effects stopped (a reconnect doesn't resume old rumble).

Tests: `fftest /dev/input/eventN` (only the sine and the two rumbles upload),
SDL `SDL_JoystickRumble`; watch `rumble[…]` on the controller.

### Behaviour
- **Latest wins:** each radio report is converted and submitted at once; if the
  host hasn't collected the previous one, it's replaced and sent on the
  "report done" callback. Radio → USB delay < ~1 ms.
- **Link loss:** no radio report for **1000 ms** (`LINK_TIMEOUT_MS` in
  `bridge.c`) → neutral report (sticks centred, nothing pressed), rumble off,
  PID effects stopped.

## USB XInput mode

The dongle poses as an Xbox 360 Wireless Receiver as Linux's `xpad` driver
knows it: `xpad` binds any `0x1209` interface of the receiver type, so no
Microsoft IDs are needed. Tested on Linux (Steam included); whether Windows'
driver binds the receiver type with our IDs is untested ([[todo#Dongle]]).
Code: `xinput.c` (USB class, packets), `bridge.c` (slot 0, output).

### Device
| Item | Value |
|---|---|
| VID / PID | `0x1209` / **`0x0002`** test PID |
| Interfaces | 4 × vendor class `0xFF`, subclass `0x5D`, protocol `0x81` (one per player slot); + CDC ACM console in development builds |
| Endpoints | Per interface: interrupt IN and OUT, 32 bytes, 1 ms (`xpad` requires exactly these two) |
| Gamepad | Created by `xpad` when a slot reports a controller: "Generic X-Box pad", product ID `0x02a1` |

Each connected controller takes the first free slot
([[#Connecting and disconnecting]]); free slots report none.

### Input packets (dongle → PC)

Presence, 2 bytes: `08 80` = controller connected, `08 00` = none. `xpad` adds
or removes the slot's gamepad on each change.

Pad data, 29 bytes:

| Offset | Size | Field |
|---|---|---|
| 0–5 | 6 | `00 01 00 F0 00 13` (pad data valid; wired-360 report header) |
| 6 | 2 | Buttons, XInput `wButtons` layout (= radio `buttons`, passed through) |
| 8 | 1 | Left trigger 0…255 (radio value >> 2) |
| 9 | 1 | Right trigger 0…255 |
| 10 | 8 | LX, LY, RX, RY, s16 LE, up = positive (as on the radio) |
| 18 | 11 | 0 |

Share and Pair have no XInput bit and aren't sent.

### Output packets (PC → dongle)

The commands `xpad` sends; others are ignored.

| Bytes | Command | Dongle action |
|---|---|---|
| `00 01 0F C0 00 <strong> <weak> …` | Rumble | Heavy = strong, light = weak |
| `00 00 08 4<n> …` | LED pattern `n` (0–15) | Guide LED: 0 = off, anything else = on |
| `00 00 08 C0 …` | Power off (Guide held 5 s) | Logged ([[todo#Controller]]) |
| `08 00 0F C0 …` | Presence query (at bind) | Repeat the presence packet |

### Behaviour
- **Connect/disconnect:** link up → presence connected, then pad data from
  every radio report; no radio report for **1000 ms** → presence
  disconnected, rumble off. The gamepad disappears from the PC.
- **Newest wins:** one IN transfer in flight per slot; a newer report replaces
  one not yet sent. Presence packets go first.
- **Rumble:** native `FF_RUMBLE` (`xpad` via the kernel's memoryless FF helper),
  heavy and light separate. The helper also emulates periodic effects on both
  motors. Off when the interface goes down or the link is lost; LT/RT motors
  stay 0 (XInput has only 2).
- **LED:** `xpad` sets the player pattern when it adds the gamepad (again
  after a reconnect).

Tests: `evtest`, `fftest /dev/input/eventN` (rumbles: one motor each),
SDL `SDL_JoystickRumble` with low only, then high only; watch `rumble[…] led`
on the controller.

## Controller wired USB mode

The controller as a USB gamepad over its own USB port (J3 D+/D−; Pro Micro:
its USB-C port), no dongle. Code: `firmware/xbx-nrf/src/usb.c` (device, mode),
`xinput_wired.c` (wired 360 class), `main.c` (switch, rumble).

### When it's wired
- **Wired** = a PC has the USB device configured and the bus isn't suspended
  (`usb_host_active()`). A charger never configures it: the controller stays
  wireless.
- While wired: reports go to USB, the radio pauses (the dongle sees link loss
  after 1000 ms), rumble comes from the PC. Unconfigured, suspended or
  unplugged → radio again.
- `CONFIG_XBX_WIRED` (default on): off → never wired, for wireless tests with
  the USB console attached.

### Mode (8BitDo-style)

| Held at boot | Mode | VID / PID | Rumble |
|---|---|---|---|
| X | Wired XInput (Xbox 360 pad) | `0x1209` / `0x0004` | Heavy / light via `xpad` |
| B | HID gamepad + PID | `0x1209` / `0x0003` | Blended strength ([[#Force feedback (PID)]]) |
| Neither | Stored mode (settings key `usb/mode`), default HID | | |

The button must be held through ~20 ms of samples right after the buttons are
set up; flash is written only when the mode changes. Read at boot only for now
(plugged in while running: [[todo#Controller]]).

**HID mode** is the dongle's HID mode ([[#USB HID mode]]): same shared code
and reports, device name "XBX-NRF Gamepad". Heavy and light each take the
larger of the vendor report and the PID strength.

### Wired XInput

| Item | Value |
|---|---|
| Interface | Vendor class `0xFF`, subclass `0x5D`, protocol `0x01` (`xpad`: wired 360 for any `0x1209` device), plus the 17-byte class-specific descriptor (type `0x21`) real pads carry; its layout is undocumented, bytes copied |
| Endpoints | Interrupt IN 32 bytes, 1 ms; interrupt OUT 32 bytes, 8 ms (as real pads) |
| Device class | `0xFF/0xFF/0xFF` as real pads; Misc / IAD when the console is included |
| Gamepad | "Generic X-Box pad" (`xpad`'s name for unlisted devices) |

Input report, 20 bytes: `00 14`, buttons (XInput `wButtons`, = radio
`buttons`), LT, RT (0…255), LX, LY, RX, RY (s16 LE, up = positive), 6 × `00`.

| Bytes (PC → pad) | Command | Controller action |
|---|---|---|
| `00 08 00 <strong> <weak> …` | Rumble | Heavy = strong, light = weak |
| `01 03 <n>` | LED pattern | Logged (Guide LED later) |
| Vendor IN request `0x01` (to the interface) | `xpad`'s start-up request | Answered with 20 zero bytes |

Windows untested ([[todo#Controller]]).

Tests: `lsusb` (PID), `evtest`, `fftest` and SDL rumble as in the dongle's
modes; the controller's stats line shows `wired` / `radio` and the motor
values.
