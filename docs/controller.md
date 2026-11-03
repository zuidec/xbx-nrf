# Using the controller

How to pair, power, connect and calibrate the controller and the dongle.
Settings beyond these: the configurator ([[configurator]]).

## Batteries

AAs only: alkaline, NiMH, or regulated 1.5 V Li-ion AAs. **Never a bare
Li-ion cell** ([[hardware]]). On USB the AAs are disconnected; the
controller doesn't charge them.

Low battery: no rumble below 25 %; the controller turns itself off when the
pack runs flat.

## Power

| Action | How |
|---|---|
| On | Press **Guide** |
| Off | Hold **Guide** 6 s |
| Auto off (AAs) | 15 min without input, or 2 min without the dongle |
| PC goes to sleep | Controllers turn off |
| Wake the PC | Press **Guide** (wireless or wired; needs wake allowed, below) |

On USB the controller can't cut its own power: "off" is a deep sleep that
Guide wakes.

## Pairing

1. Dongle: press its button (automatic at plug-in if never paired).
2. Controller: hold **Pair** ~3 s, or power on with Pair held (automatic if
   never paired).

Both blink fast while pairing and stop after 30 s. One dongle keeps up to 7
controllers paired and 4 connected. Hold **Pair** 10 s (either device) for a
factory reset: pairing and settings erased.

## Guide LED

| LED | Means |
|---|---|
| Solid | Connected (dongle or wired) |
| Slow blink, 1 Hz | Searching for the dongle |
| Fast blink, 5 Hz | Pairing |
| Three quick blinks | Paired |
| Three slow blinks | Error: pairing refused or failed, dongle full |
| Off (on USB) | The PC is asleep: Guide wakes it |

## Wired and wireless

Plugged into a PC, the controller works wired and its radio pauses; a
charger alone keeps it wireless. Unplugged, it goes back to the dongle.

The wired mode is picked at start-up and remembered:

| Held at start-up | Mode |
|---|---|
| **X** | XInput (Xbox 360 pad; rumble per motor) |
| **B** | HID gamepad (Share and paddles with the SDL mapping) |

The dongle's mode switch picks HID (1 player) or XInput (up to 4 players).
Share and the paddles reach games only in HID mode; on Linux they need the
SDL mapping (configurator: File → Install files).

## Calibration

| Step | How |
|---|---|
| Start | Hold **View + Menu** 3 s; leave sticks and triggers alone (LED solid) |
| Sweep | LED blinks 1 Hz: move both sticks around their full range, pull both triggers fully, a few times |
| Save | Press **View + Menu** again; a fast blink means not enough range yet: keep going, save again |
| Cancel | **B** (or 60 s) |
| Erase | Hold **View + Menu + LS** 3 s: back to auto-ranging |

The configurator does the same with live views, and sets where a trigger
reports full pull (5–100 % of its travel).

## Trigger effects

Made on the controller, so they work in every game, wired or wireless:

- **Click:** a short pulse when a trigger reaches full pull (disabled when the
  aux trigger buttons are detected as the input source).
- **Trigger rumble:** while a trigger is pulled, part of the game's rumble
  in that trigger.

Strength, length and shares: configurator, Rumble panel.

## Waking the PC

Needs, once per PC:

- **Linux:** the udev rule (configurator: File → Install files), then
  replug.
- **BIOS:** wake from USB allowed (usually on).

This wakes from sleep, not from a full shutdown.
