---
title: xbx-nrf protocols (radio + USB HID)
created: 2026-09-29
tags:
  - xbx-nrf
  - firmware
  - reference
aliases:
  - protocol
  - radio protocol
  - HID report
---

# xbx-nrf protocols

Two links: **radio** (controller ↔ dongle, implemented) and **USB HID** (dongle
↔ PC, planned). XInput and GIP modes reuse the radio data; see [[docs/gip]] for
GIP. Tasks: [[todo#Link]], [[todo#Dongle]].

## Radio (controller ↔ dongle)

Source of truth: `firmware/common/include/protocol.h`. **Version 1**
(`XBX_PROTOCOL_VERSION`); both boards must run the same version.

### Link

| Setting | Value |
|---|---|
| Protocol | Nordic ESB, dynamic payload length, selective auto-ACK |
| Bit rate | 2 Mbps, fast ramp-up |
| Channel | 76 (2476 MHz), fixed for now (`XBX_RF_CHANNEL`) |
| Address | base 0 `58 42 58 31` ("XBX1"), prefix `E7`, pipe 0 (fixed until pairing) |
| TX power | +8 dBm, both ends (`XBX_TX_POWER_DBM`) |
| Roles | Controller = PTX, dongle = PRX |
| Report rate | 1000 Hz from hardware TIMER3 (`XBX_REPORT_PERIOD_US`) |
| Retries | 1, 450 µs apart (ESB minimum 435 µs); stale reports are flushed, never resent late |
| Max payload | 32 bytes (`CONFIG_ESB_MAX_PAYLOAD_LENGTH`) |

Every 1 ms the controller sends an **input report**. The dongle's ESB ACK
carries the **output report** queued for it (one is always kept queued), so
rumble/LED data costs no extra transmissions. All fields are little-endian.

### Input report (controller → dongle), 23 bytes

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `type` | `0x01` (`XBX_MSG_INPUT`) |
| 1 | 2 | `seq` | +1 per report actually sent; gaps = lost over the air. Gap ≥ 0x8000 = controller restart |
| 3 | 2 | `buttons` | Bitmap below, 1 = pressed |
| 5 | 2 | `lx` | Left stick X, −32768…32767 |
| 7 | 2 | `ly` | Left stick Y |
| 9 | 2 | `rx` | Right stick X |
| 11 | 2 | `ry` | Right stick Y |
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

### Output report (dongle → controller, ACK payload), 8 bytes

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `type` | `0x02` (`XBX_MSG_OUTPUT`) |
| 1 | 1 | `seq` | +1 per report queued |
| 2 | 4 | `rumble[4]` | Heavy, light, LT, RT; 0…255 |
| 6 | 1 | `led` | Guide LED brightness, 0…255 |
| 7 | 1 | `flags` | Reserved, 0 |

### Changing the protocol
- Bump `XBX_PROTOCOL_VERSION` and the `_Static_assert` sizes; update this page.
- Mismatched versions show up as `bad` packets on the dongle (length/type
  check).
- Planned for v2: pairing, multiple controllers, time slots
  ([[#Pairing & multiple controllers (planned, protocol v2)]]).

| Version | Change |
|---|---|
| 0 | Initial: 22-byte input report, 8-bit `seq` |
| 1 | 16-bit `seq` (23-byte input report) |

## Pairing & multiple controllers (planned, protocol v2)

> [!warning] Draft
> Design agreed 2026-09-29, not implemented. Numbers marked *(tune)* are
> starting values to adjust on hardware.

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
- **Channel:** each dongle picks the quietest channel (RSSI sampling) from a
  fixed **candidate list**, at first use: **24, 49, 74, 76, 78** *(tune)*,
  i.e. 2424–2478 MHz in the gaps between Wi-Fi channels 1/6/11, clear of BLE
  advertising. Channel hopping later follows a per-dongle sequence.
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
      no ACK → next channel (~0.6 ms each)
      ACK    → stay, repeat every ~2 ms
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
- **Proximity:** RSSI ≥ **−50 dBm** *(tune)*, i.e. held close; otherwise
  ignored.
- **Version:** `proto_ver` must match; otherwise `PAIR_OFFER` with status
  "version mismatch" and both show the error blink.
- **Ambiguity:** a second, different `ctrl_id` in the same window → abort, error
  blink.
- **Pipe:** known `ctrl_id` → same pipe; else a free pipe; else the **least
  recently connected** entry's pipe.

**Controller checks on `PAIR_OFFER`:**
- `nonce` must echo its own (not someone else's offer).
- Offers from two different `dongle_id`s in one window (two dongles pairing
  nearby) → abort, error blink.

**Commit order:** the dongle saves when it receives `PAIR_CONFIRM` on the new
pipe (proof the controller has the offer and switched); the controller saves
when it receives `PAIR_DONE`. Flash writes run from a work queue, never in the
radio interrupt.

### Pairing messages
Payloads (little-endian, sizes to finalize):

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
- Factory reset: e.g. Pair + View held at power-on *(tune)*; the dongle's
  button held ~10 s *(tune)*.

### Time slots (TDMA)
One radio can't receive two controllers at once, so the dongle runs a
repeating **frame** and gives each connected controller its own **slot**:

| Connected | Frame | Slots | Rate per controller |
|---|---|---|---|
| 1–2 | 1 ms | 2 × 500 µs | 1000 Hz |
| 3–4 | 2 ms | 4 × 500 µs | 500 Hz |

- A transaction (report + ACK) takes ~200–300 µs, so 500 µs slots leave margin.
- **Sync:** each ACK carries a timing correction (report arrival vs. slot
  start, measured by the dongle). The controller trims its TIMER3 period.
  Crystal drift is ~20 ns/ms, so per-frame correction is plenty.
- **No retries** within a frame: a lost report is replaced by the next one.
- **Frame changes** (2nd → 3rd controller and back): the dongle announces the
  new frame length and each slot in ACK payloads, effective from a given frame
  number, so all controllers switch together.
- Pairing traffic on pipe 0 is rare and short; it may cost an occasional report
  in an active slot.

### Connecting and disconnecting
- **Join:** a paired, unsynced controller sends reports on its pipe every
  ~10 ms with random jitter until an ACK assigns it a slot. Rare collisions with
  active slots cost at most one report.
- **Full (4 connected):** the dongle answers with a "full" status; the
  controller shows it and retries slowly, then powers off after a timeout.
- **Drop:** no report from a slot for ~100 ms *(tune)* → slot freed, USB reports
  a disconnect.
- **Player number** = slot (XInput receiver slot; optionally shown on the
  Guide LED). HID mode is single-player.

### Message changes (v2)
| Type | Message | Direction |
|---|---|---|
| `0x01` | Input report (unchanged) | controller → dongle |
| `0x02` | Output report + sync fields (below) | dongle → controller |
| `0x10`–`0x13` | Pairing ([[#Pairing messages]]) | both |

Output report additions (sizes to finalize): slot index, frame length, timing
correction (signed µs), status (`ok` / `full` / `frame change at N`), and link
quality (RSSI, for [[todo#Dynamic TX power]]).

### Later
- **Encryption / authentication:** ESB has none; a key from pairing (ECDH) +
  AES-CCM per packet would stop spoofed input. The nRF52840 has hardware for
  both. Not planned for v1.

## USB modes (dongle ↔ PC)

| Mode | Players | Hot-plug | Use |
|---|---|---|---|
| **HID gamepad** | 1 | — | Bring-up, own tools, non-XInput systems |
| **XInput** (Xbox 360 Wireless Receiver emulation) | 1–4 | Yes, in-band connect/disconnect (`xpad`, Steam, Windows) | Everyday / multiplayer |

Plain HID has no way to add or remove a gamepad without re-enumerating the
whole device, hence single-player HID.

## USB HID mode (planned, M2)

> [!warning] Draft
> Design agreed 2026-09-29, not implemented. Finalize the descriptor bytes when
> building it and update this page.

### Device
| Item | Value |
|---|---|
| VID / PID | `0x1209` (pid.codes) / **`0x0001`** test PID during development |
| Interfaces | 1 HID gamepad; + CDC ACM console in development builds (composite) |
| HID endpoints | Interrupt IN 1 ms; interrupt OUT ~4 ms *(tune)* |
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
| 3 | 1 | Hat switch (4 bits, 0–7, 8 = centred) + 4 bits padding | Hat switch | `ABS_HAT0X/Y` |
| 4 | 2 | Left stick X | X, −32768…32767 | `ABS_X` |
| 6 | 2 | Left stick Y (**inverted**: HID down = positive) | Y | `ABS_Y` |
| 8 | 2 | Right stick X | Rx | `ABS_RX` |
| 10 | 2 | Right stick Y (**inverted**) | Ry | `ABS_RY` |
| 12 | 2 | Left trigger, 0…1023 | Z | `ABS_Z` |
| 14 | 2 | Right trigger, 0…1023 | Rz | `ABS_RZ` |

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
| 1 | 4 | Rumble heavy, light, LT, RT (0…255) | Vendor page `0xFF00` |
| 5 | 1 | Guide LED (0…255) | Vendor page `0xFF00` |

Maps 1:1 onto the radio output report. Games won't use it (no standard HID
rumble); it's for our own tools and possibly Steam later.

### Behaviour
- **Latest wins:** each radio report is converted and submitted at once; if the
  host hasn't collected the previous one, it's replaced and sent on the
  "report done" callback. Radio → USB delay < ~1 ms.
- **Link loss:** no radio report for ~100 ms *(tune)* → neutral report (sticks
  centred, nothing pressed) and rumble off.
