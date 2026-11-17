# Dongle logger

Logs the dongle's USB console to a file, unattended, for chasing problems
that only show up on someone else's PC. Linux, systemd, Python 3 (standard
library only). Needs a development build of the dongle: release builds have
no console.

## Install

```
sudo ./install.sh        # install, start, and start at every boot
sudo ./install.sh -u     # remove (keeps the logs)
```

It runs as a system service, so it needs no login or SSH session.

## Logs

`/var/log/xbx-dongle/xbx-dongle-YYYY-MM.log`, one file per month, the newest
6 kept. Each line has the PC's wall-clock time. Lines starting with `---` are
the logger's own:

| Line | Means |
|---|---|
| `console open` / `console gone` | Dongle plugged in or out, or USB reset |
| `waiting for the dongle console` | No dongle with a console found |
| `PC was suspended for N s` | Suspend and resume |

USB-level events (disconnects, resets) are in the kernel log:
`journalctl -k`.

## Reading the dongle's lines

| Line | Means |
|---|---|
| `10 s P1 rx …` | Averages over 10 s, with the radio channel (`ch`) |
| `second P1 rx …` | A second with 25+ lost reports or under 98 % of the rate |
| `P1 gap …` | Late reports (see below) |
| `P1 controller restarted` | The controller's uptime started over |
| `pipe 1: link lost` | No reports for 1 s |

A gap with `dongle missed` above 0: reports never arrived. Only `rejoining`:
the reports arrived but the controller missed the dongle's replies for
100 ms, lost its slot and blinked; the replies stopped about 100 ms before
the gap's start time.
