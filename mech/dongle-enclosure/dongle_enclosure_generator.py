# Dongle enclosure generator (FreeCAD 1.x).
#
#   FreeCADCmd dongle_enclosure.py
#
# Builds the enclosure around pcb/xbx-nrf-dongle Rev1.1 and writes, next to
# this script:
#   dongle-enclosure.FCStd   all parts in assembly position, plus the PCB
#   dongle-enclosure.step    all parts in assembly position
#   stl/*.stl                one per part, in print orientation
#
# Parts are built in KiCad board coordinates (mm, Y negated: the frame of
# kicad-cli's board STEP), then everything, PCB included, is moved so the
# enclosure is centred on the origin in X and Y; Z = 0 stays the PCB underside.
# Component positions below are from the Rev1.1 layout; update them if the
# board changes.

import os
import subprocess

import FreeCAD as App
import Import
import MeshPart
import Part

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
PCB_FILE = os.path.join(REPO, "pcb", "xbx-nrf-dongle", "xbx-nrf-dongle.kicad_pcb")
PCB_STEP = os.path.join(HERE, "dongle-pcb.step")

V = App.Vector

# --- Board (Rev1.1) ---------------------------------------------------------

PCB_X0, PCB_X1 = 75.4, 110.4
PCB_Y0, PCB_Y1 = 36.17, 86.17
PCB_R = 2.5
PCB_T = 1.6

MOUNT_HOLES = [(91.41, 40.7), (91.41, 81.7)]   # H1, H2 (M2)
BUTTONS = {"pair": (87.41, 44.47), "rst": (87.41, 74.47)}   # SW2, SW1 (B3U)
BUTTON_TOP = 3.2                                # B3U actuator top
LED_TOP = 2.7
LED_BLUE = (87.13, 49.47)                       # D1, pairing
LED_RED = (87.68, 69.47)                        # D6, power
LED_GREEN_Y = 78.21                             # D2-D5, player
LED_GREEN_X = [96.34, 99.8, 102.96, 106.23]

# Rear (left) edge: actuator range covers both switch positions (2 mm travel).
SW_ACT_Y = (46.67, 50.17)                       # SW3
SW_ACT_Z = (3.31, 4.82)
USB_Y, USB_Z = 60.15, 3.275                     # J2 opening centre
DEBUG_PORTS = [(68.07, 73.07), (75.80, 80.80)]  # J3 UART, J1 SWD (y range)
DEBUG_TOP = 4.56

# Top-side parts near the bosses and under the caps: (x0, x1, y0, y1, z top).
TOP_PARTS = [
    (92.67, 93.92, 44.20, 46.20, 2.84),         # C1
    (95.87, 97.47, 45.75, 46.55, 2.40),         # C3
    (93.17, 96.07, 47.92, 50.72, 3.15),         # U1
    (82.18, 83.78, 41.27, 42.07, 2.04),         # R1
    (95.94, 96.74, 81.31, 82.91, 2.04),         # R7
]
BOTTOM_PARTS = [
    (77.71, 79.31, 58.45, 59.25),               # R2
    (77.76, 79.36, 61.37, 62.17),               # R5
]

# Kyocera FPC antenna, stuck to the inside of the lid (User.Comments hatch).
ANTENNA = (122.4, 130.4, 41.19, 81.19)

# --- Enclosure parameters ---------------------------------------------------

WALL = 1.6          # 4 lines at 0.4 mm
LID = 1.6
FLOOR = 1.6
GAP = 0.3           # PCB edge to wall
UNDER = 2.0         # PCB bottom to floor (room for the screw counterbore)
CEILING = 6.0       # PCB top to lid underside (u.FL plug + cable)
OUT_R = 5.0         # outer corner radius

OUT_X0 = PCB_X0 - GAP - WALL
OUT_X1 = ANTENNA[1] + GAP + WALL
OUT_Y0, OUT_Y1 = 33.7, 88.7

Z_BOT = -UNDER - FLOOR
Z_FLOOR = -UNDER
Z_SPLIT = PCB_T                     # shells meet at the PCB top surface
Z_LID = PCB_T + CEILING
Z_TOP = Z_LID + LID

# Screws from below into heat-set inserts in the top shell (ruthex M2).
BOSS_D = 6.0
INSERT_HOLE_D = 3.2                 # ruthex M2 x 4: Ø3.2 hole, 4 mm long
INSERT_HOLE_DEPTH = 5.5
SCREW_CLEAR_D = 2.4
SCREW_HEAD_D = 4.2                  # M2 socket head Ø3.8
SCREW_HEAD_DEPTH = 2.1
EXTRA_BOSSES = [(116.4, 38.9), (116.4, 83.5)]   # beside the PCB, no PCB hole

USB_WINDOW = (12.8, 7.0, 2.5)       # width, height, radius: fits plug overmolds
SW_SLOT_CLEAR = 0.3
SW_RECESS = (6.0, 3.3, 0.8)         # finger recess round the slot: w, h, depth
DEBUG_WINDOWS = False               # open J1/J3 through the rear wall
PORT_CLEAR = 0.3

CAP_D, CAP_HOLE_D = 4.0, 4.4
CAP_FLANGE_D, CAP_FLANGE_T = 5.6, 0.8
CAP_STEM_D = 2.0
CAP_PLAY = 0.3                      # flange to lid underside
CAP_GAP = 0.1                       # stem to actuator
CAP_PROUD = {"pair": 0.6, "rst": -0.4}   # top above (+) or below (-) the lid

PIPE_D, PIPE_HOLE_D = 2.4, 2.6      # D1, D6
GREEN_D, GREEN_HOLE_D = 2.0, 2.2    # D2-D5 (3.2 mm pitch)
PIPE_FLANGE_D, PIPE_FLANGE_T = 3.6, 0.8
PIPE_GAP = 0.2                      # pipe end above the LED

ANT_POCKET_DEPTH = 0.3
ANT_CLEAR = 0.3

# Labels engraved into the lid, on the free side of each button. Cap height
# 2.6 mm gives ~0.5 mm strokes in bold: printable with a 0.4 mm nozzle.
LABEL_FONT = "/usr/share/fonts/truetype/LiberationSans-Bold.ttf"
LABEL_CAP = 2.6
LABEL_DEPTH = 0.6
LABEL_GAP = 1.0                     # cap hole edge to text
LABELS = {"pair": ("PAIR", -1), "rst": ("RESET", +1)}  # -1 above, +1 below

# --- Helpers (KiCad x/y in, FreeCAD solids out) -----------------------------


def kbox(x0, x1, y0, y1, z0, z1):
    return Part.makeBox(x1 - x0, y1 - y0, z1 - z0, V(x0, -y1, z0))


def rrect(x0, x1, y0, y1, z0, z1, r):
    """Box with its vertical edges rounded."""
    b = kbox(x0, x1, y0, y1, z0, z1)
    if r <= 0:
        return b
    edges = [e for e in b.Edges
             if abs(e.Vertexes[0].Point.z - e.Vertexes[1].Point.z) > 1e-6]
    return b.makeFillet(r, edges)


def xslot(x0, x1, y0, y1, z0, z1, r):
    """Box with the edges along X rounded (a window through the rear wall)."""
    b = kbox(x0, x1, y0, y1, z0, z1)
    if r <= 0:
        return b
    edges = [e for e in b.Edges
             if abs(e.Vertexes[0].Point.x - e.Vertexes[1].Point.x) > 1e-6]
    return b.makeFillet(r, edges)


def cyl(x, y, d, z0, z1):
    return Part.makeCylinder(d / 2, z1 - z0, V(x, -y, z0))


def fuse(shapes):
    out = shapes[0]
    for s in shapes[1:]:
        out = out.fuse(s)
    return out.removeSplitter()


def ring(x0, x1, y0, y1, r, inset, z0, z1):
    outer = rrect(x0, x1, y0, y1, z0, z1, r)
    inner = rrect(x0 + inset, x1 - inset, y0 + inset, y1 - inset, z0 - 1,
                  z1 + 1, max(r - inset, 0))
    return outer.cut(inner)


# --- Shells -----------------------------------------------------------------


def shell():
    outer = rrect(OUT_X0, OUT_X1, OUT_Y0, OUT_Y1, Z_BOT, Z_TOP, OUT_R)
    cavity = rrect(OUT_X0 + WALL, OUT_X1 - WALL, OUT_Y0 + WALL, OUT_Y1 - WALL,
                   Z_FLOOR, Z_LID, OUT_R - WALL)
    s = outer.cut(cavity)

    # Rear wall: cutters reach from outside the wall to the PCB edge.
    x0, x1 = OUT_X0 - 1, PCB_X0
    w, h, r = USB_WINDOW
    cuts = [xslot(x0, x1, USB_Y - w / 2, USB_Y + w / 2, USB_Z - h / 2,
                  USB_Z + h / 2, r)]
    c = SW_SLOT_CLEAR
    cuts.append(kbox(x0, x1, SW_ACT_Y[0] - c, SW_ACT_Y[1] + c,
                     SW_ACT_Z[0] - c, SW_ACT_Z[1] + c))
    rw, rh, rd = SW_RECESS
    ym, zm = sum(SW_ACT_Y) / 2, sum(SW_ACT_Z) / 2
    cuts.append(xslot(x0, OUT_X0 + rd, ym - rw / 2, ym + rw / 2, zm - rh / 2,
                      zm + rh / 2, 1.0))
    if DEBUG_WINDOWS:
        for ya, yb in DEBUG_PORTS:
            cuts.append(kbox(x0, x1, ya - PORT_CLEAR, yb + PORT_CLEAR,
                             Z_SPLIT, DEBUG_TOP + PORT_CLEAR))

    # Lid: button caps, light pipes, antenna pocket.
    for x, y in BUTTONS.values():
        cuts.append(cyl(x, y, CAP_HOLE_D, Z_LID - 1, Z_TOP + 1))
    for x, y in (LED_BLUE, LED_RED):
        cuts.append(cyl(x, y, PIPE_HOLE_D, Z_LID - 1, Z_TOP + 1))
    for x in LED_GREEN_X:
        cuts.append(cyl(x, LED_GREEN_Y, GREEN_HOLE_D, Z_LID - 1, Z_TOP + 1))
    ax0, ax1, ay0, ay1 = ANTENNA
    a = ANT_CLEAR
    cuts.append(rrect(ax0 - a, min(ax1 + a, OUT_X1 - WALL), ay0 - a, ay1 + a,
                      Z_LID - ANT_POCKET_DEPTH, Z_LID + 0.01, 0.5))
    cuts += [label(name) for name in LABELS]
    return s.cut(fuse(cuts))


def label(name):
    """Engraving cutter for a button label, centred over or under the cap."""
    text, side = LABELS[name]
    x, y = BUTTONS[name]
    chars = Part.makeWireString(text, LABEL_FONT, 10.0, 0)
    faces = [Part.makeFace(w, "Part::FaceMakerBullseye") for w in chars if w]
    t = Part.Compound(faces)
    t.scale(LABEL_CAP / t.BoundBox.YLength)     # capitals only: bbox = cap
    b = t.BoundBox
    ty = y + side * (CAP_HOLE_D / 2 + LABEL_GAP + LABEL_CAP / 2)
    t.translate(V(x - (b.XMin + b.XMax) / 2, -ty - (b.YMin + b.YMax) / 2,
                  Z_TOP - LABEL_DEPTH))
    return t.extrude(V(0, 0, LABEL_DEPTH + 1))


def keepouts_top():
    m = 0.4
    ks = [kbox(x0 - m, x1 + m, y0 - m, y1 + m, Z_SPLIT - 0.1, zt + m)
          for x0, x1, y0, y1, zt in TOP_PARTS]
    # Room for the cap flanges to move.
    for x, y in BUTTONS.values():
        ks.append(cyl(x, y, CAP_FLANGE_D + 0.6, Z_LID - CAP_PLAY
                      - CAP_FLANGE_T - 0.3, Z_LID + 0.01))
    return fuse(ks)


def top_shell(s):
    top = s.common(kbox(OUT_X0 - 1, OUT_X1 + 1, OUT_Y0 - 1, OUT_Y1 + 1,
                        Z_SPLIT, Z_TOP + 1))
    bosses = [cyl(x, y, BOSS_D, Z_SPLIT, Z_LID + 0.01)
              for x, y in MOUNT_HOLES + EXTRA_BOSSES]
    top = top.fuse(fuse(bosses).cut(keepouts_top()))
    holes = [cyl(x, y, INSERT_HOLE_D, Z_SPLIT - 0.1,
                 Z_SPLIT + INSERT_HOLE_DEPTH)
             for x, y in MOUNT_HOLES + EXTRA_BOSSES]
    return top.cut(fuse(holes)).removeSplitter()


def bottom_shell(s):
    bot = s.common(kbox(OUT_X0 - 1, OUT_X1 + 1, OUT_Y0 - 1, OUT_Y1 + 1,
                        Z_BOT - 1, Z_SPLIT))
    # Ledge under the PCB edge, clear of the bottom-side resistors.
    ledge = ring(PCB_X0 - GAP, PCB_X1 + GAP, PCB_Y0 - GAP, PCB_Y1 + GAP,
                 PCB_R + GAP, GAP + 0.8, Z_FLOOR - 0.01, 0)
    m = 0.4
    ledge = ledge.cut(fuse([kbox(x0 - m, x1 + m, y0 - m, y1 + m, -2, 0.1)
                            for x0, x1, y0, y1 in BOTTOM_PARTS]))
    bosses = [cyl(x, y, BOSS_D, Z_FLOOR - 0.01, 0) for x, y in MOUNT_HOLES]
    bosses += [cyl(x, y, BOSS_D, Z_FLOOR - 0.01, Z_SPLIT)
               for x, y in EXTRA_BOSSES]
    bot = bot.fuse(fuse([ledge] + bosses))
    holes = []
    for x, y in MOUNT_HOLES + EXTRA_BOSSES:
        holes.append(cyl(x, y, SCREW_CLEAR_D, Z_BOT - 1, Z_SPLIT + 1))
        holes.append(cyl(x, y, SCREW_HEAD_D, Z_BOT - 1,
                         Z_BOT + SCREW_HEAD_DEPTH))
    return bot.cut(fuse(holes)).removeSplitter()


# --- Small parts ------------------------------------------------------------


def button_cap(name):
    x, y = BUTTONS[name]
    flange_top = Z_LID - CAP_PLAY
    flange_bot = flange_top - CAP_FLANGE_T
    top = Z_TOP + CAP_PROUD[name]
    return fuse([
        cyl(x, y, CAP_D, flange_top - 0.01, top),
        cyl(x, y, CAP_FLANGE_D, flange_bot, flange_top),
        cyl(x, y, CAP_STEM_D, BUTTON_TOP + CAP_GAP, flange_bot + 0.01),
    ])


def light_pipe(x, y):
    return fuse([
        cyl(x, y, PIPE_D, LED_TOP + PIPE_GAP, Z_TOP),
        cyl(x, y, PIPE_FLANGE_D, Z_LID - PIPE_FLANGE_T, Z_LID),
    ])


def light_bar():
    xs = LED_GREEN_X
    y = LED_GREEN_Y
    half = PIPE_FLANGE_D / 2
    plate = rrect(xs[0] - half, xs[-1] + half, y - half, y + half,
                  Z_LID - PIPE_FLANGE_T, Z_LID, 1.0)
    pipes = [cyl(x, y, GREEN_D, LED_TOP + PIPE_GAP, Z_TOP) for x in xs]
    return fuse([plate] + pipes)


# --- Build ------------------------------------------------------------------


def print_pose(shape, flip):
    s = shape.copy()
    if flip:
        s.rotate(V(0, 0, 0), V(1, 0, 0), 180)
    b = s.BoundBox
    s.translate(V(-(b.XMin + b.XMax) / 2, -(b.YMin + b.YMax) / 2, -b.ZMin))
    return s


def export_pcb_step():
    if os.path.exists(PCB_STEP) and \
            os.path.getmtime(PCB_STEP) >= os.path.getmtime(PCB_FILE):
        return True
    try:
        subprocess.run(["kicad-cli", "pcb", "export", "step", "--subst-models",
                        "--no-dnp", "-f", "-o", PCB_STEP, PCB_FILE],
                       check=True, capture_output=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        print("kicad-cli failed: no PCB reference or fit check")
        return False


def main():
    s = shell()
    parts = {
        "top_shell": (top_shell(s), True),
        "bottom_shell": (bottom_shell(s), False),
        "cap_pair": (button_cap("pair"), True),
        "cap_rst": (button_cap("rst"), True),
        "pipe_blue": (light_pipe(*LED_BLUE), True),
        "pipe_red": (light_pipe(*LED_RED), True),
        "pipe_green": (light_bar(), True),
    }
    centre = V(-(OUT_X0 + OUT_X1) / 2, (OUT_Y0 + OUT_Y1) / 2, 0)
    for shape, _ in parts.values():
        shape.translate(centre)

    doc = App.newDocument("DongleEnclosure")
    objs = []
    for name, (shape, _) in parts.items():
        o = doc.addObject("Part::Feature", name)
        o.Shape = shape
        objs.append(o)

    have_pcb = export_pcb_step()
    if have_pcb:
        Import.insert(PCB_STEP, doc.Name)
        for o in doc.Objects:
            if o not in objs and not o.InList and hasattr(o, "Placement"):
                o.Placement.Base = o.Placement.Base + centre
        pcb = [o for o in doc.Objects
               if o.TypeId == "Part::Feature" and o not in objs
               and o.Shape.Volume > 0]
        print("Fit check (overlap with PCB and parts, mm^3):")
        for o in objs:
            worst = 0.0
            for p in pcb:
                ps = p.Shape.copy()
                ps.Placement = p.getGlobalPlacement()
                if not ps.BoundBox.intersect(o.Shape.BoundBox):
                    continue
                v = o.Shape.common(ps).Volume
                if v > 1e-3:
                    worst = max(worst, v)
                    print(f"  {o.Name} hits {p.Label}: {v:.3f}")
            if worst == 0:
                print(f"  {o.Name}: clear")

    doc.recompute()
    doc.saveAs(os.path.join(HERE, "dongle-enclosure.FCStd"))
    Import.export(objs, os.path.join(HERE, "dongle-enclosure.step"))

    os.makedirs(os.path.join(HERE, "stl"), exist_ok=True)
    for name, (shape, flip) in parts.items():
        mesh = MeshPart.meshFromShape(Shape=print_pose(shape, flip),
                                      LinearDeflection=0.01,
                                      AngularDeflection=0.1, Relative=False)
        mesh.write(os.path.join(HERE, "stl", name + ".stl"))

    print(f"Outside: {OUT_X1 - OUT_X0:.1f} x {OUT_Y1 - OUT_Y0:.1f} x "
          f"{Z_TOP - Z_BOT:.1f} mm")
    print(f"Screws: M2 x 6 socket head ({len(MOUNT_HOLES + EXTRA_BOSSES)}), "
          f"ruthex M2 inserts ({len(MOUNT_HOLES + EXTRA_BOSSES)})")


main()
