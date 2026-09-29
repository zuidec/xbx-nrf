---
title: Firmware signing
created: 2026-09-29
tags:
  - xbx-nrf
  - firmware
  - security
  - howto
aliases:
  - signing
  - MCUboot
---

# Firmware signing

Release builds use **MCUboot** with **ECDSA P-256** signatures. MCUboot only boots or installs an image signed with the device's private key. Related: [[docs/flashing|Flashing & debugging]], [[todo#Firmware signing]].

## Keys

| Device | App | Key file |
|---|---|---|
| Controller | `firmware/xbx-nrf` | `~/.config/xbx-nrf/keys/controller-p256.pem` |
| Dongle | `firmware/dongle` | `~/.config/xbx-nrf/keys/dongle-p256.pem` |

- **One key per device type.** Both boards are nRF52840s, and MCUboot only checks *who* signed an image, not what it's for. Separate keys mean MCUboot **rejects the dongle firmware on the controller** (and vice versa) instead of installing it.
- Keys are **outside the repo** (folder `700`, files `600`). The `.gitignore` also ignores `*.pem`, `*.key`, `*.p12`, `*.pfx`, `*.der` and `keys/` as a safety net.
- `~/bin/ncs-shell` exports `XBX_KEY_DIR` **only inside the NCS shell**; it isn't in `~/.bashrc`.

> [!danger] Back up both keys offline
> Put them in a password manager or on an offline USB stick. If a key is lost, devices with that key can only be updated by re-flashing over SWD (which also replaces MCUboot).

> [!warning] Never sign with the default key
> If `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` isn't set, the SDK signs with **MCUboot's public demo key** (`root-ec-p256.pem`) or a generated `GENERATED_NON_SECURE_SIGN_KEY_PRIVATE.pem`, and the build still succeeds. Always build releases with `build-signed.sh`, which refuses to run without our key.

### Generating a key (already done 2026-09-29)
In the NCS shell:
```sh
umask 077; mkdir -p ~/.config/xbx-nrf/keys
python3 ~/ncs/v3.4.1/bootloader/mcuboot/scripts/imgtool.py keygen \
    -k ~/.config/xbx-nrf/keys/controller-p256.pem -t ecdsa-p256
```

## Building a signed release

```sh
ncs-shell
cd ~/Projects/xbx-nrf
firmware/build-signed.sh dongle  nrf52840dongle/nrf52840/bare
firmware/build-signed.sh xbx-nrf <custom-board>          # once the board definition exists
```

The script:
1. picks the key for the app (`controller-p256.pem` / `dongle-p256.pem`) from `$XBX_KEY_DIR` and stops if it's missing
2. builds with `sysbuild-signed.conf` (MCUboot + ECDSA P-256) into `<app>/build-signed/`
3. passes the key on the command line (`-DSB_CONFIG_BOOT_SIGNATURE_KEY_FILE=...`), so no path is committed

Normal development builds (`west build` in the app folder) are **unchanged**: no MCUboot, no signing. The **Pro Micro** prototypes keep their UF2 bootloader and are never built signed.

### Outputs (`<app>/build-signed/`)

| File | Use |
|---|---|
| `<app>/zephyr/zephyr.signed.bin` | Update image for MCUboot (serial recovery / mcumgr) |
| `<app>/zephyr/zephyr.signed.hex` | Signed application as hex |
| `dfu_application.zip` | Update package with metadata (nRF Connect tools) |

For the **first install** (MCUboot + app) over SWD: `west flash -d <app>/build-signed` flashes every image in the build (see [[docs/flashing]]).

### Verifying a signature
```sh
python3 ~/ncs/v3.4.1/bootloader/mcuboot/scripts/imgtool.py verify \
    -k ~/.config/xbx-nrf/keys/dongle-p256.pem firmware/dongle/build-signed/dongle/zephyr/zephyr.signed.bin
```
"Image was correctly validated" = signed with that key. Tested 2026-09-29: the dongle image validates with the dongle key and is rejected by the controller key and the MCUboot demo key.

## Installing updates (to decide)

- **MCUboot serial recovery over USB** (planned): both devices have native USB (the controller through the top board's USB-C). Hold a button at power-up → MCUboot exposes a USB serial port → upload with `mcumgr` / `smpmgr`. Needs MCUboot image config (serial recovery, CDC ACM, entry button).
- Over the radio (dongle → controller): possible later, custom work.

## Limits

> [!important] Signing only protects anything if SWD is locked
> With the debug port open, anyone with physical access can replace MCUboot itself. Release builds must **lock the debug port (APPROTECT)**, and the build prints a reminder about keeping secure boot intact. During development, keep it unlocked (see [[docs/flashing#Locked chip (APPROTECT)]]).

- Signing proves *who built* the image. It doesn't encrypt it. MCUboot image encryption exists, but isn't planned.
- Image version is `0.0.0+0` until versioning is set up (e.g. a `VERSION` file per app); MCUboot can then refuse downgrades.
