---
title: Firmware signing
created: 2026-09-29
updated: 2026-09-29
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

Release builds: **MCUboot**, **ECDSA P-256**. MCUboot only boots or installs
images signed with the device's key. Related: [[docs/building]],
[[docs/flashing|Flashing & debugging]], [[todo#Firmware signing]].

## Keys

| Device | App | Key file |
|---|---|---|
| Controller | `firmware/xbx-nrf` | `~/.config/xbx-nrf/keys/controller-p256.pem` |
| Dongle | `firmware/dongle` | `~/.config/xbx-nrf/keys/dongle-p256.pem` |

- **One key per device type**, so MCUboot rejects dongle firmware on the
  controller and vice versa (it only checks the signer, not the image's
  purpose).
- Outside the repo (folder `700`, files `600`). `.gitignore` also blocks
  `*.pem`, `*.key`, `*.p12`, `*.pfx`, `*.der`, `keys/`.
- `XBX_KEY_DIR` is exported by `~/bin/ncs-shell` only.

> [!danger] Back up both keys offline
> A lost key means those devices can only be updated over SWD.

> [!warning] Never sign with the default key
> Without `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`, the SDK silently signs with
> MCUboot's **public demo key** (`root-ec-p256.pem`) or a generated
> `GENERATED_NON_SECURE_SIGN_KEY_PRIVATE.pem`. `build-signed.sh` refuses to run
> without our key.

### Generating a key (done 2026-09-29)
```sh
umask 077; mkdir -p ~/.config/xbx-nrf/keys
python3 ~/ncs/v3.4.1/bootloader/mcuboot/scripts/imgtool.py keygen \
    -k ~/.config/xbx-nrf/keys/controller-p256.pem -t ecdsa-p256
```

## Building a signed release

```sh
ncs-shell
cd ~/Projects/xbx-nrf
firmware/build-signed.sh dongle nrf52840dongle/nrf52840/bare
firmware/build-signed.sh xbx-nrf <custom-board>   # once it exists
```

The script picks the app's key from `$XBX_KEY_DIR`, builds with
`sysbuild-signed.conf` into `<app>/build-signed/`, and passes the key path on
the command line (nothing committed). Development builds and the Pro Micro
prototypes don't use MCUboot.

### Outputs (`<app>/build-signed/`)

| File | Use |
|---|---|
| `<app>/zephyr/zephyr.signed.bin` | Update image (serial recovery / mcumgr) |
| `<app>/zephyr/zephyr.signed.hex` | Signed application, hex |
| `dfu_application.zip` | Update package (nRF Connect tools) |

First install (MCUboot + app) over SWD: `west flash -d <app>/build-signed`.

### Verifying a signature
```sh
python3 ~/ncs/v3.4.1/bootloader/mcuboot/scripts/imgtool.py verify \
    -k ~/.config/xbx-nrf/keys/dongle-p256.pem \
    firmware/dongle/build-signed/dongle/zephyr/zephyr.signed.bin
```
"Image was correctly validated" = signed with that key. Tested 2026-09-29:
accepted by the dongle key, rejected by the controller and demo keys.

## Installing updates (to decide)

- **MCUboot serial recovery over USB** (planned): hold a button at power-up,
  upload with `mcumgr` / `smpmgr`. Needs MCUboot config (serial recovery, CDC
  ACM, entry button).
- Over the radio: possible later, custom work.

## Limits

> [!important] Useless unless SWD is locked
> With the debug port open, anyone can replace MCUboot. Release devices need
> **APPROTECT**; development boards stay unlocked
> ([[docs/flashing#Locked chip (APPROTECT)]]).

- Signed, not encrypted.
- Image version comes from the app's `VERSION` file (`0.1.0+0`;
  [[docs/building#Versions]]). Downgrade protection isn't enabled yet.
