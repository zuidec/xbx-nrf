"""Command line: the app's functions for development and tests.
No arguments starts the GUI (see __main__.py)."""

import argparse
import select
import sys
import time

from . import device as dev
from . import files, transport, uf2
from . import protocol as p

BUTTON_NAMES = {v: k for k, v in p.BUTTONS.items()}


def _open(args):
    return dev.open_first(args.device)


def cmd_list(args):
    found = dev.find()
    if not found:
        print("nothing found")
    for f in found:
        try:
            d = dev.Device(f)
        except transport.AccessError:
            print(f"{f.path}: {f.kind}, no access (run: xbx-config install-udev)")
            continue
        i = d.info
        print(f"{f.path}: {i.device} {i.firmware_str}, {i.usb_mode}, {i.board}, "
              f"serial {i.serial}")
        d.close()


def cmd_info(args):
    d = _open(args)
    i = d.info
    print(f"device:   {i.device}\nfirmware: {i.firmware_str}\nchannel:  {i.channel[0]}."
          f"{i.channel[1]}\nUSB mode: {i.usb_mode}\nboard:    {i.board}\nserial:   {i.serial}")
    d.close()


def _binding_name(v):
    return BUTTON_NAMES.get(v, "none" if v == 0 else f"0x{v:04x}")


def cmd_paddles(args):
    d = _open(args)
    if args.state is not None:
        d.set_paddles_enabled(args.state == "on")
    if args.bind:
        values = []
        for name in args.bind:
            if name.lower() == "none":
                values.append(0)
                continue
            match = [v for k, v in p.BUTTONS.items() if k.lower() == name.lower()]
            if not match:
                sys.exit(f"unknown button {name!r}: {', '.join(p.BUTTONS)}, none")
            values.append(match[0])
        if len(values) != 4:
            sys.exit("--bind takes 4 buttons (P1 P2 P3 P4)")
        d.set_bindings(values)
    on, binds = d.paddles()
    print(f"paddles {'on' if on else 'off'}: " +
          ", ".join(f"{n} → {_binding_name(v)}" for n, v in zip(p.PADDLES, binds)))
    d.close()


def cmd_triggers(args):
    d = _open(args)
    if args.pct:
        d.set_trigger_full(*args.pct)
    lt, rt = d.trigger_full()
    print(f"full pull threshold: LT {lt} %, RT {rt} % of the calibrated travel")
    d.close()


def cmd_led(args):
    d = _open(args)
    if d.info.device == "dongle":
        if args.level is not None:
            sys.exit("the dongle takes --pair, --player, --power")
        leds = d.dongle_leds()
        for name in ("pair", "player", "power"):
            v = getattr(args, name)
            if v in ("on", "off"):
                if name == "pair":
                    sys.exit("the pair LED can't be turned off")
                leds[name + "_on"] = v == "on"
            elif v is not None:
                leds[name] = led_level(v)
        if any(getattr(args, n) is not None for n in ("pair", "player", "power")):
            d.set_dongle_leds(leds)
        leds = d.dongle_leds()
        print(f"pair {leds['pair']}, " + ", ".join(
            f"{n} {leds[n]}{'' if leds[n + '_on'] else ' (off)'}" for n in ("player", "power")))
    else:
        if any(getattr(args, n) is not None for n in ("pair", "player", "power")):
            sys.exit("the controller takes one level (the Guide LED)")
        if args.level is not None:
            d.set_led(args.level)
        print(f"Guide LED: {d.led()} / 255")
    d.close()


def cmd_stream(args):
    d = _open(args)
    d.stream(args.hz)
    try:
        while True:
            s = d.latest_stream
            if s:
                pressed = [k for k, v in p.BUTTONS.items() if s.buttons & v]
                pressed += [k for k, v in p.BUTTONS_EXT.items() if s.buttons_ext & v]
                print(f"\rL {s.lx:6} {s.ly:6}  R {s.rx:6} {s.ry:6}  T {s.lt:4} {s.rt:4}  "
                      f"{s.battery_mv} mV  {' '.join(pressed):40}", end="", flush=True)
            time.sleep(1 / 20)
    except KeyboardInterrupt:
        print()
    finally:
        d.stream(0)
        d.close()


TARGETS = {"all": 0, "sticks": p.CAL_STICKS, "triggers": p.CAL_TRIGGERS}


def _cal_line(st):
    axes = []
    for i, name in enumerate(p.AXES):
        r = st.ranges[i]
        mark = "!" if st.refused & (1 << i) else ("*" if st.stored & (1 << i) else " ")
        axes.append(f"{name}{mark}{r.min:5}..{r.center:5}..{r.max:5}")
    return f"{p.CAL_STATES[st.state]:6} " + "  ".join(axes)


def cmd_calibrate(args):
    d = _open(args)
    if args.step == "status":
        st = d.calibrate()
        print(_cal_line(st), f" (* stored; full from LT {st.full_pct[0]} %, "
              f"RT {st.full_pct[1]} %)")
    elif args.step == "erase":
        d.calibrate(p.CAL_ERASE)
        print("erased: auto-ranging until calibrated")
    else:
        _guided(d, TARGETS[args.targets])
    d.close()


def _guided(d, targets):
    what = {0: "sticks and triggers", 1: "sticks", 2: "triggers"}[targets]
    d.calibrate(p.CAL_START, targets)
    print(f"1. Leave the {what} alone: measuring rest...")
    told = False
    while True:
        st = d.calibrate()
        print("\r" + _cal_line(st) + "   ", end="", flush=True)
        if st.state == 2 and not told:
            told = True
            print(f"\n2. Move the sticks around their full range and pull the triggers fully, "
                  f"a few times.\n   Enter saves, c + Enter cancels (! = refused, keep going).")
        if st.state == 2 and select.select([sys.stdin], [], [], 0)[0]:
            line = sys.stdin.readline().strip()
            d.calibrate(p.CAL_CANCEL if line == "c" else p.CAL_SAVE)
        if st.state == 3:
            print("\nSaved.")
            return
        if st.state == 0 and told:
            print("\nCancelled.")
            return
        time.sleep(0.1)


def cmd_rumble(args):
    d = _open(args)
    d.rumble([args.heavy, args.weak, args.lt, args.rt], args.ms)
    d.close()


def cmd_reset(args):
    scope = ((p.RESET_SETTINGS if args.settings else 0)
             | (p.RESET_CALIBRATION if args.calibration else 0)
             | (p.RESET_PAIRING if args.pairing else 0))
    if not scope:
        sys.exit("name at least one of --settings, --calibration, --pairing")
    d = _open(args)
    d.factory_reset(scope)
    print("reset; restarting")
    d.close()


def cmd_update(args):
    image = uf2.load(args.file)
    print(f"{args.file}: {image.device} firmware {image.version_str}")
    d = _open(args)
    print(f"device: {d.info.device} firmware {d.info.firmware_str}")
    uf2.update(d, image)


def cmd_bootloader(args):
    d = _open(args)
    d.bootloader()
    d.close()
    print(f"restarting into the bootloader (drive {uf2.DRIVES[d.info.device]})")


def cmd_reboot(args):
    d = _open(args)
    d.reboot()
    d.close()


def cmd_install_udev(args):
    files.install_udev_rule()
    print("installed; unplug and replug the device")


def cmd_uninstall(args):
    paths = files.installed()
    if paths:
        files.uninstall()
        print("removed: " + ", ".join(paths))
    else:
        print("nothing installed on this system")
    app = files.app_file()
    print(f"to remove the app itself, delete {app}" if app else
          "running from source: nothing else to remove")


def led_level(s):
    v = int(s)
    if not 1 <= v <= 255:
        raise argparse.ArgumentTypeError("LED levels are 1-255")
    return v


def level(s):
    v = int(s)
    if not 0 <= v <= 255:
        raise argparse.ArgumentTypeError("levels are 0-255")
    return v


def main(argv=None):
    ap = argparse.ArgumentParser(prog="xbx-config", description="xbx-nrf configurator "
                                 "(no command: the window)")
    ap.add_argument("-d", "--device", choices=("controller", "dongle"),
                    help="which device (default: the first found)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list", help="devices found").set_defaults(fn=cmd_list)
    sub.add_parser("info").set_defaults(fn=cmd_info)
    s = sub.add_parser("paddles", help="show or set paddles (controller)")
    s.add_argument("state", nargs="?", choices=("on", "off"))
    s.add_argument("--bind", nargs=4, metavar="BTN", help="XInput button for P1..P4 or none")
    s.set_defaults(fn=cmd_paddles)
    s = sub.add_parser("triggers", help="show or set the full pull threshold (controller)")
    s.add_argument("pct", nargs="*", type=int, metavar="PCT",
                   help=f"LT RT, {p.TRG_FULL_MIN}-100 %% of the calibrated travel")
    s.set_defaults(fn=cmd_triggers)
    s = sub.add_parser("led", help="show or set the LED brightness")
    s.add_argument("level", nargs="?", type=led_level, help="controller: Guide LED, 1-255")
    s.add_argument("--pair", metavar="LEVEL", help="dongle: pair LED, 1-255")
    s.add_argument("--player", metavar="LEVEL", help="dongle: player LEDs, 1-255, on or off")
    s.add_argument("--power", metavar="LEVEL", help="dongle: power LED, 1-255, on or off")
    s.set_defaults(fn=cmd_led)
    s = sub.add_parser("stream", help="live input (controller)")
    s.add_argument("hz", nargs="?", type=int, default=50)
    s.set_defaults(fn=cmd_stream)
    s = sub.add_parser("calibrate", help="guided calibration (controller)")
    s.add_argument("step", nargs="?", choices=("guided", "status", "erase"), default="guided")
    s.add_argument("targets", nargs="?", choices=tuple(TARGETS), default="all")
    s.set_defaults(fn=cmd_calibrate)
    s = sub.add_parser("rumble", help="motor test (controller)")
    for name in ("heavy", "weak", "lt", "rt"):
        s.add_argument(name, type=level, help=f"{name} motor level 0-255")
    s.add_argument("ms", nargs="?", type=int, default=1000, help="duration, ≤ 2000 ms")
    s.set_defaults(fn=cmd_rumble)
    s = sub.add_parser("reset", help="factory reset, then restart")
    s.add_argument("--settings", action="store_true")
    s.add_argument("--calibration", action="store_true")
    s.add_argument("--pairing", action="store_true")
    s.set_defaults(fn=cmd_reset)
    s = sub.add_parser("update", help="firmware update from a .uf2 file")
    s.add_argument("file")
    s.set_defaults(fn=cmd_update)
    sub.add_parser("bootloader", help="restart into the UF2 bootloader").set_defaults(
        fn=cmd_bootloader)
    sub.add_parser("reboot").set_defaults(fn=cmd_reboot)
    sub.add_parser("install-udev", help="Linux: give the user access (admin prompt)"
                   ).set_defaults(fn=cmd_install_udev)
    sub.add_parser("uninstall", help="remove what the app installed (Linux: the udev "
                   "rule; admin prompt)").set_defaults(fn=cmd_uninstall)
    args = ap.parse_args(argv)
    if getattr(args, "pct", None) and (len(args.pct) != 2 or not all(
            p.TRG_FULL_MIN <= v <= 100 for v in args.pct)):
        ap.error(f"triggers takes LT RT, each {p.TRG_FULL_MIN}-100")
    try:
        args.fn(args)
    except transport.AccessError as e:
        sys.exit(f"{e}: run 'xbx-config install-udev'" if transport.LINUX else str(e))
    except (p.ProtocolError, uf2.Uf2Error, OSError) as e:
        sys.exit(f"error: {e}")
