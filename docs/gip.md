---
title: GIP (Xbox One/Series USB protocol) notes
created: 2026-09-29
tags:
  - xbx-nrf
  - reference
  - usb
aliases:
  - GIP
  - MS-GIPUSB
---

# GIP: Gaming Input Protocol over USB

Microsoft's protocol for Xbox One/Series controllers. Only needed for an
optional future **GIP dongle mode** (impulse triggers on Windows; see
[[todo#Future / v2]]). Our own protocols: [[docs/protocol]].

Source: [[references/windows_protocols-ms-gipusb.pdf]] (MS-GIPUSB, 2024; §
numbers below). Reference implementation: GP2040-CE's Xbox One mode (MIT).

## Feasibility summary

- **Impulse triggers:** one simple message (`0x09`, below).
- **No authentication needed on PC:** list the opt-out interface GUID
  `7a34ce77-7de2-45c6-8ca4-0042c08bd94a` in the metadata; "the host succeeds
  the security exchange by default" (§5.1). Xbox consoles still require it.
- **Metadata** is the hard part: written as JSON, compiled to a binary blob with
  Microsoft's metadata compiler (in the "gipdocs" download, not in this spec).
  Windows requests it; Linux `xpad` doesn't.
- **Linux:** `xpad` supports GIP but sends trigger motor levels as 0, and Linux
  force feedback has only 2 rumble values. Trigger rumble would need SDL/Steam
  talking to the device directly. **Test with a genuine controller first**
  ([[todo#GIP dongle mode]]).
- **IDs:** the spec requires the maker's own VID; `xpad` only auto-binds known
  vendors (Microsoft's VID for a personal build, or bind manually).

## USB (§2.2.3–2.2.9)

| Item | Value |
|---|---|
| Device / interface class | `FF` / subclass `47` / protocol `D0` |
| Endpoints | Interface 0: 64-byte interrupt IN + OUT |
| Polling | "up to 4 ms / 250 Hz" (1 ms presumably allowed; verify) |
| Config attributes | `0xA0` (bus powered, remote wakeup) |
| Serial string | 32 hex digits = 64-bit Device ID |
| MS OS descriptor | String `MSFT100`, vendor code `0x90`; compatible ID `XGIP10` |

## Message header (§2.2.10)

| Byte | Field | Notes |
|---|---|---|
| 0 | Type | Bits 7:5 data class (000 command, 001 low latency, 011 audio), 4:0 message number |
| 1 | Flags | 7 fragment, 6 first fragment, 5 **system**, 4 **ACK requested**, 2:0 expansion index |
| 2 | Sequence | Wrapping, `0x00` reserved; global pool, except security / extended / audio / vendor messages (own pools) |
| 3 | Length | Payload bytes; bit 7 = length continues in the next byte |

MTU 64 bytes (commands, input). Larger messages (metadata) are fragmented with
ACKs (`0x01`) at least every 100 ms (§3.1.5.1–3.1.5.2).

## Startup (§1.3.1, §2.2.1–2.2.2)

1. Device sends **Hello** (`0x02`) every 500 ms until the host answers.
2. Host requests **metadata** (`0x04`, up to 4× at 500 ms; reply < 500 ms) or
   sends **Set Device State: Start** directly (metadata is cached by VID/PID
   + firmware major/minor).
3. **Start** → device sends **Status** (`0x03`) and a first **input report**
   reflecting the current state.
4. Host sends **Guide LED** (`0x0A`) and **motor** (`0x09`) commands.

## Messages we'd need

| Type | Dir | Message | Notes |
|---|---|---|---|
| `0x01` | ↕ | Protocol control (ACK) | Reliable / fragmented transfers |
| `0x02` | ↑ | Hello | 28 bytes: Device ID, VID, PID, FW major/minor/build/rev, HW version, RF/security/GIP versions (all `1.0`) |
| `0x03` | ↑ | Status | Extended format (4 bytes): on start, power off, charge/battery changes, every 1 s for 10 s then every 20 s |
| `0x04` | ↕ | Metadata request / response | Response fragmented, ends with Metadata Complete (flags `0xA0`) |
| `0x05` | ↓ | Set device state | `00` Start, `01` Stop, `04` Off, `05` Quiesce (clear motors), `07` Reset |
| `0x06` | ↕ | Security | Skipped via the opt-out GUID |
| `0x07` | ↑ | Guide button | 2-byte payload; after power-on, don't report the press until its first release |
| `0x09` | ↓ | **Direct motor** | Below |
| `0x0A` | ↓ | Guide LED | Pattern (`01` on, …) + intensity 0–47 % |
| `0x20` | ↑ | **Gamepad input** | Below; send only when something changed |

### Direct motor command (`0x09`, §3.1.5.6.1)

Header `09 00 <seq> 09`, then:

| Byte | Field | Notes |
|---|---|---|
| 4 | Command | `0x00` |
| 5 | Motor mask | bit 3 left impulse, 2 right impulse, 1 left grip, 0 right grip |
| 6 | Left impulse | 0–100 % |
| 7 | Right impulse | 0–100 % |
| 8 | Left grip (heavy) | 0–100 % |
| 9 | Right grip (light) | 0–100 % |
| 10 | Duration | ×10 ms; `0` = stop all |
| 11 | Delay | ×10 ms |
| 12 | Repeat | Count; `0` = play once |

Maps onto our `rumble[4]` (scale 0–100 → 0–255). Duration/delay/repeat could
be forwarded so the controller plays patterns itself.

### Gamepad input report (`0x20`, §3.1.5.6.1.1)

Header `20 00 <seq> 0E` (14-byte payload; `20` = 32 bytes with Share):

| Byte | Field |
|---|---|
| 4 | `0x04` Menu, `0x08` View, `0x10` A, `0x20` B, `0x40` X, `0x80` Y |
| 5 | `0x01` Up, `0x02` Down, `0x04` Left, `0x08` Right, `0x10` LB, `0x20` RB, `0x40` LS, `0x80` RS |
| 6–7 | Left trigger 0–1023 |
| 8–9 | Right trigger 0–1023 |
| 10–17 | LX, LY, RX, RY: −32768…32767 |
| 18 | *(Share variant)* Console Function Map: `0x01` while Share is held, else `0x00`; rest zero |

Guide isn't in this report (it's `0x07`). Bit order differs from our radio
`buttons` (XInput layout), so the dongle remaps.
