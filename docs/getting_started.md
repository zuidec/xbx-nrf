---
title: Getting started
created: 2026-11-03
updated: 2026-11-03
tags:
  - xbx-nrf
  - howto
aliases:
  - getting started
  - quick start
---

# Getting started

This is for using the pre-compiled binaries. If you'd like to build yourself, 
have a look at [building from source](building.md).

**Prerequisite:** Make sure you have the [required 
hardware](flashing.md#hardware)

## Downloads

From the [releases page](https://github.com/zuidec/xbx-nrf/releases), the
newest of each:

| File | For |
|---|---|
| `xbx-config-<version>-<platform>` | Configurator|
| `xbx-dongle-<version>.uf2` | Dongle firmware |
| `xbx-controller-<version>.uf2` | Controller firmware |
| `xbx_controller_bootloader-<version>_s140_6.1.1.hex` | Controller bootloader |
| `xbx_dongle_bootloader-<version>_s140_6.1.1.hex` | Dongle bootloader |

`SHA256SUMS` lists the checksums.


## Preparing to flash the Bootloader

> [!IMPORTANT]
> The nRF modules do not ship with a bootloader.
> You will need a debugger to program the bootloader
> the first time over SWD.

> [!WARNING]
> **SWD flashing erases the entire chip**
>
> This includes firmware, pairing, and calibration settings!

1. Download the latest bootloader: [bootloader 
   releases](https://github.com/zuidec/xbx-nrf/releases?q=bootloader&expanded=true)
2. Make sure `openocd` is installed.
    - On linux, this should be available through your package manager
        - **Debian / Ubuntu:**
        `sudo apt install openocd`
        - **Fedora:**
        `sudo dnf install openocd`
        - **SUSE:**
        `sudo zypper install openocd`
    - On windows:
        - Download from 
        [Github](https://github.com/openocd-org/openocd/releases)
        - Using MSYS2: `pacman -S mingw-w64-x86_64-openocd`
3. Connect the debugger to the SWD port. Any CMSIS-DAP probe should work, but 
   the Raspberry Pi Debug Probe connects directly with its included cable
4. Ensure the board is powered over USB

## Flashing the Bootloader

The commands to flash are very similar, but there are minor differences, so make 
sure to pay attention to which commands you are using.

### Linux

Run the commands below, using the path to the downloaded bootloader. If you 
installed with a package manager, the `openocd` command should be available 
system-wide

**Controller:**
```sh
openocd -f interface/cmsis-dap.cfg -c 'transport select swd' \
  -f target/nrf52.cfg -c init -c 'reset halt' -c 'nrf5 mass_erase' \
  -c 'program /path/to/xbx_controller_bootloader-0.11.0-xbx.1_s140_6.1.1.hex verify reset exit'
```

**Dongle:**
```sh
openocd -f interface/cmsis-dap.cfg -c 'transport select swd' \
  -f target/nrf52.cfg -c init -c 'reset halt' -c 'nrf5 mass_erase' \
  -c 'program /path/to/xbx_dongle_bootloader-0.11.0-xbx.1_s140_6.1.1.hex verify reset exit'
```

### Windows

Either put OpenOCD on your `PATH` or run these commands from the directory where 
the OpenOCD binary is using PowerShell. If OpenOCD is not on `PATH`, add `.\` 
before the commands below to run from the current directory.

**Controller:**
```powershell
openocd.exe -f interface/cmsis-dap.cfg -c 'transport select swd' `
  -f target/nrf52.cfg -c init -c 'reset halt' -c 'nrf5 mass_erase' `
  -c 'program {C:/Users/you/Downloads/xbx_controller_bootloader-0.11.0-xbx.1_s140_6.1.1.hex} verify reset exit'
```

**Dongle:**
```powershell
openocd.exe -f interface/cmsis-dap.cfg -c 'transport select swd' `
  -f target/nrf52.cfg -c init -c 'reset halt' -c 'nrf5 mass_erase' `
  -c 'program {C:/Users/you/Downloads/xbx_dongle_bootloader-0.11.0-xbx.1_s140_6.1.1.hex} verify reset exit'
```

**Congratulations!** The boards are now ready to be loaded with the app firmware

## Flashing the App

After flashing each board with the bootloader, they should automatically appear 
as a drive when connected via USB.

> [!IMPORTANT]
> **Boards with apps already installed**
>
> Entering the bootloader of a previously flashed board can be accomplished
> by performing the following:
> - **Controller:** Hold **LS+RS** while plugging the controller into USB
> - **Dongle:** Hold **PAIR** while plugging into USB
> - **Both:** double-tap the reset button (USB must be plugged in)

1. Download the latest release:
    - [Controller 
    Releases](https://github.com/zuidec/xbx-nrf/releases?q=controller&expanded=true)
    - [Dongle 
    Releases](https://github.com/zuidec/xbx-nrf/releases?q=dongle&expanded=true)
2. Open the USB drive that appears, either `XBXDONGLE` or `XBXCTRL`
3. Copy the corresponding UF2 file to the USB drive. It should disconnect 
   automatically when it completes

**Ta-da!** If this is their first flash, your boards will start flashing their 
LEDs after programming to indicate they are in pairing mode.

## Pairing

1. The dongle should be in pairing mode. If not, press the pair button on the 
   dongle. The blue pair light will start flashing.
2. Make sure the controller is unplugged from USB.
3. Turn on the controller with the guide button. The guide LED should flash. If 
   not, press and hold the pair button on the controller until it does (*only 
   ~3s, 10s is a factory reset*)
4. Once both devices are in pairing mode, they should pair almost 
   immediately. You should see three quick LED flashes followed by solid 
   indicator lights on both devices.

> [!TIP]
> In practice, the devices can pair so quickly that you may miss the flashing 
> stage. If you see a solid guide LED and a lit green LED player indicator, the 
> pairing was likely successful.

## Setting up the configurator

The configurator install process differs slightly between operating systems.

### Linux

Runs on Debian 12 or newer (glibc 2.36).

1. **Configurator:** make the downloaded file executable and start it:
   ```sh
   chmod +x xbx-config-*-linux-x86_64
   ./xbx-config-*-linux-x86_64
   ```
2. **Install files:** File → Install files, tick both:
   - **udev rule** (admin password): access to the devices without root,
     and the dongle and wired controller can wake the PC.
   - **SDL mapping:** Map the share button and paddles in Steam and SDL games, 
     from the next login.

> [!TIP]
> Log out and back in for the SDL mapping to take effect.

### Windows

**Planned.** Will be performing more testing and updating this section when 
complete!

### macOS

Not tested or planned, I don't own a Mac. In theory, it should just build with 
the proper environment setup. If you'd like to try it on a Mac, open an issue on 
[GitHub](https://github.com/zuidec/xbx-nrf/issues).

## Calibration

The controller will perform coarse calibration ranging automatically. However, 
for the best performance, you should calibrate it manually with one of the 
following methods:

1. Built-in calibration:
    1. Hold **View + Menu** for 3s while leaving sticks and triggers in neutral 
       position
    2. The Guide LED will begin blinking. Move both sticks in full circles a few 
       times and fully depress each trigger a few times.
    3. Press **View + Menu** again to save. If the firmware has not detected 
       enough range, the LED will blink quickly to indicate you need to continue 
       moving each axis through its full range and try again.
    4. Cancel calibration at any time by pressing **B**. Calibration cancels 
       automatically after 60 seconds.
2. Configurator
    1. Plug the controller into your PC via USB. Configurator does not work 
       wirelessly over the dongle.
    2. Navigate to the `Calibration` section of the `Controller` tab.
    3. Select which axes to calibrate and click **Start**. Make sure not to move 
       the sticks or triggers yet.
    4. Move the selected axes through their full range of motion a few 
       times. You should see your inputs live on the configurator.
    5. Once you are satisfied, click **Save**.
    6. Click **Cancel** at any time to cancel. Calibration cancels automatically 
       after 60 seconds.

## Next

Power, LEDs, wired and wireless, trigger effects: [controller](controller.md).
Configurator settings: [configurator](configurator.md).
