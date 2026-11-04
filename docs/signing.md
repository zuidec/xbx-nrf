---
title: Firmware signing
created: 2026-09-29
updated: 2026-10-05
tags:
  - xbx-nrf
  - firmware
  - decision
aliases:
  - signing
  - MCUboot
---

# Firmware signing

**Not used.** Signed images (MCUboot, ECDSA P-256) were considered and
rejected: this is a hobby project, and unsigned images make it easier for
others to build their own boards and run their own firmware. Both boards use
the unsigned Adafruit nRF52 UF2 bootloader ([todo](../todo.md)).
