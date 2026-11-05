#!/usr/bin/env python3
"""Globo single-board layout: fit check against the real enclosure + KiCad outline.

Everything here is in ENCLOSURE coordinates, as exported in
../enclosure/PRINT_01_Enclosure_USBV.stl (mm):

    X  across the triangle (left face = speaker, right face = display)
    Y  along the prism     (Y=-5 knob end ... Y=80 USB-C end)
    Z  up                  (Z=0 bottom / lid)

Run:  python3 globo_board.py           (needs trimesh, scipy, matplotlib)
Out:  fitcheck.png         sections of the case with the board + parts drawn in
      globo-board.kicad_pcb  Edge.Cuts for the spine + encoder tab, F.Fab zones

All numbers below were measured from the STL by slicing it. They are the
single source of truth: change a number, rerun, and both outputs follow.
"""
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
STL = os.path.join(HERE, "..", "enclosure", "PRINT_01_Enclosure_USBV.stl")

# ── Measured enclosure (from slicing the STL) ─────────────────────────────
CX = 113.83                 # centre plane of the prism; knob hole + USB slot sit on it
INNER_Y = (-2.0, 78.0)      # inner faces of the two end walls
FLOOR_Z = 2.35              # inner floor (top of lid plate)
LID_OPENING_Y = (3.15, 74.5)  # bottom opening length today: 71.35 mm
LID_OPENING_X = (103.6, 124.1)  # 20.5 mm wide
KNOB_AXIS_Z = 17.73         # 7 mm through-hole, 20.4 mm knob recess outside
USB_SLOT = dict(x=(112.09, 115.59), z=(12.5, 21.5))   # 3.5 x 9 mm, upright
WINDOW = dict(y=(15.0, 60.0), z_outer=(10.75, 33.3))  # display window, right face
GRILLE = dict(y=(38.2, 53.8), z=(16.5, 31.0))         # speaker slots, left face
TAN30 = math.tan(math.radians(30))   # faces are 60 deg: x moves 0.577 per mm of z


def x_left(z):   # inner face of the left (speaker) wall
    return 95.94 + TAN30 * (z - 12.0)


def x_right(z):  # inner face of the right (display) wall
    return 2 * CX - x_left(z)


# ── Board stack-up and parts (design choices) ─────────────────────────────
PCB_T = 1.6
SPINE_X = (CX - PCB_T / 2, CX + PCB_T / 2)   # the spine stands on the centre plane
BOARD_BOTTOM_Z = 2.6

# Encoder: snap-in (no bushing) EC11, shaft trimmed; the knob gets a stem that
# reaches through the 7 mm hole and grips the shaft tip from outside.
ENC_SHAFT_TIP_Y = 0.0        # 2 mm inside the inner wall face
ENC_SHAFT_FROM_BODY = 7.0    # trimmed shaft length above the body face
ENC_BODY = 12.4              # body square
ENC_BODY_H = 6.5             # body height (face to PCB)
ENC_BODY_FACE_Y = ENC_SHAFT_TIP_Y + ENC_SHAFT_FROM_BODY
TAB_Y = (ENC_BODY_FACE_Y + ENC_BODY_H, ENC_BODY_FACE_Y + ENC_BODY_H + PCB_T)
TAB_HALF_W = 9.5             # 19 mm: must pass the 20.5 mm lid opening

# USB-C: mid-mount receptacle in a notch at the spine's far edge, shell
# poking 1.5 mm into the existing 3.5 x 9 slot (shell is 8.94 x 3.26).
USB_FACE_Y = 79.5
USB_LEN = 7.35
USB_Z = (17.0 - 4.6, 17.0 + 4.6)
SPINE_Y = (TAB_Y[1], 77.7)
SPINE_TOP_Z = 27.0
ANT_NOTCH_TOP_Z = 24.0       # board cut back under the ESP32 antenna

# Display: 1.9" 170x320 ST7789 bare panel, 25.8 x 49.72 x 1.43, 8080 FPC.
LCD = dict(len=49.72, wid=25.8, t=1.43)
LCD_Y = (37.5 - LCD["len"] / 2, 37.5 + LCD["len"] / 2)
LCD_ZC = 22.0                # centre of the window on the slope

# Speaker: ASSUMED 35 x 25 x 4.5 (the "3525" in INVENTORY.md), against the
# left face, centred on the grille. Swap in the real one when known.
SPK = dict(len=35.0, wid=25.0, t=4.5, yc=46.0, zc=23.5)

# Placement zones on the spine (Y range, Z range, side, label)
ZONES = [
    ((15.6, 33.6), (5.0, 30.5), "R", "ESP32-S3-WROOM-1-N16R8\n(antenna up)"),
    ((50.0, 62.0), (3.0, 8.0), "R", "LCD FPC\nconnector"),
    ((38.0, 50.0), (3.2, 11.0), "L", "MAX98357A"),
    ((58.0, 72.0), (3.2, 12.0), "L", "charger + LDO\n+ power path"),
    ((64.0, 71.5), (21.5, 26.0), "L", "batt JST"),
    ((18.0, 30.0), (2.6, 6.0), "L", "BOOT / RST\n(side-push, via lid)"),
]


def spine_outline():
    """Spine board polygon in (Y, Z), counter-clockwise, with the USB notch,
    the T-joint tongue that plugs through the encoder tab, and the antenna
    cut-back."""
    y0, y1 = SPINE_Y
    ant_y1 = ZONES[0][0][1]
    nb = 3.0, 9.0  # tongue z range (below the encoder body)
    ch = 1.5       # bottom chamfers clear the floor fillets
    uz0, uz1 = USB_Z
    notch_y = USB_FACE_Y - USB_LEN + 1.6
    return [
        (y0, BOARD_BOTTOM_Z + ch), (y0, nb[0]),
        (TAB_Y[0], nb[0]), (TAB_Y[0], nb[1]), (y0, nb[1]),   # tongue
        (y0, ANT_NOTCH_TOP_Z), (ant_y1, ANT_NOTCH_TOP_Z),
        (ant_y1, SPINE_TOP_Z), (y1, SPINE_TOP_Z),
        (y1, uz1), (notch_y, uz1), (notch_y, uz0), (y1, uz0),  # USB notch
        (y1, BOARD_BOTTOM_Z + ch), (y1 - ch, BOARD_BOTTOM_Z),
        (y0 + ch, BOARD_BOTTOM_Z),
    ]


def tab_outline():
    """Encoder tab polygon in (X, Z): 19 mm wide house shape that follows the
    60-degree walls with 1 mm clearance; a slot takes the spine's tongue."""
    hw = TAB_HALF_W
    z_shoulder = 12.0 + (17.89 - 1.0 - hw) / TAN30
    z_top = 31.0
    top_hw = 17.89 - 1.0 - TAN30 * (z_top - 12.0)
    return [
        (CX - hw, BOARD_BOTTOM_Z), (CX + hw, BOARD_BOTTOM_Z),
        (CX + hw, z_shoulder), (CX + top_hw, z_top),
        (CX - top_hw, z_top), (CX - hw, z_shoulder),
    ]


TAB_SLOT = ((SPINE_X[0] - 0.1, SPINE_X[1] + 0.1), (3.0, 9.0))


# ── Checks ────────────────────────────────────────────────────────────────
def checks():
    out = []
    ok = lambda c, msg: out.append(("OK  " if c else "FAIL") + "  " + msg)
    hw = TAB_HALF_W * 2
    ok(hw < LID_OPENING_X[1] - LID_OPENING_X[0],
       f"encoder tab {hw:.1f} mm wide passes the {LID_OPENING_X[1]-LID_OPENING_X[0]:.1f} mm lid opening")
    total = USB_FACE_Y - ENC_SHAFT_TIP_Y
    ok(total - 1.5 <= INNER_Y[1] - INNER_Y[0] - 0.5,
       f"assembly {total:.1f} mm long fits the {INNER_Y[1]-INNER_Y[0]:.0f} mm cavity "
       f"(dropped in 1.5 mm short of the USB wall, then slid into the slot)")
    lid = LID_OPENING_Y[1] - LID_OPENING_Y[0]
    ok(total <= lid,
       f"assembly {total:.1f} mm drops through the CURRENT {lid:.1f} mm lid opening"
       + ("" if total <= lid else "  -> lengthen the opening to the full 80 mm"))
    # display back vs ESP32 module (right side)
    (my, mz, _, _) = ZONES[0]
    mod_face = SPINE_X[1] + 3.1
    worst = min(x_right(z) - LCD["t"] / math.cos(math.radians(30)) - mod_face
                for z in (mz[0], mz[1]))
    ok(worst > 0.3, f"ESP32 module clears the display back by {worst:.1f} mm at worst")
    # speaker back vs spine (left side)
    zs = (SPK["zc"] - SPK["wid"] / 2 * math.sin(math.radians(60)),
          SPK["zc"] + SPK["wid"] / 2 * math.sin(math.radians(60)))
    # above the spine's top edge the speaker may reach past the centre plane
    zs = (zs[0], min(zs[1], SPINE_TOP_Z))
    gap = min(SPINE_X[0] - (x_left(z) + SPK["t"] / math.cos(math.radians(30))) for z in zs)
    ok(gap > 1.0, f"spine left face clears the (assumed) speaker back by {gap:.1f} mm "
                  f"(below the spine's top edge)")
    # encoder body inside the triangle at the tab
    top = KNOB_AXIS_Z + ENC_BODY / 2
    room = 17.89 - TAN30 * (top - 12.0)
    ok(room > ENC_BODY / 2, f"encoder body corners clear the walls by {room-ENC_BODY/2:.1f} mm")
    usb_ok = (USB_SLOT["z"][0] <= USB_Z[0] + 0.1 and USB_Z[1] - 0.1 <= USB_SLOT["z"][1])
    ok(usb_ok, "USB-C shell (8.94 x 3.26) lines up with the existing 3.5 x 9 slot")
    return out


# ── Fit-check render ──────────────────────────────────────────────────────
def render(path):
    import numpy as np
    import trimesh
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.patches import Polygon, Rectangle

    m = trimesh.load(STL)

    def section(ax, axis, val, cols):
        o = [0, 0, 0]; n = [0, 0, 0]; o[axis] = val; n[axis] = 1
        s = m.section(plane_origin=o, plane_normal=n)
        if s is None:
            return
        for e in s.discrete:
            ax.plot(e[:, cols[0]], e[:, cols[1]], color="#444", lw=0.9)

    def slanted_rect(ax, side, zc, wid, t, **kw):
        """A flat part lying against a 60-degree inner face."""
        s = math.sin(math.radians(60))
        z0, z1 = zc - wid / 2 * s, zc + wid / 2 * s
        f = x_right if side == "R" else x_left
        # inward normal of the wall: perpendicular to its direction
        d = np.array([f(z1) - f(z0), z1 - z0]); d /= np.linalg.norm(d)
        nrm = np.array([d[1], -d[0]])
        if (nrm[0] > 0) != (side == "L"):
            nrm = -nrm
        p = [(f(z0), z0), (f(z1), z1),
             (f(z1) + nrm[0] * t, z1 + nrm[1] * t), (f(z0) + nrm[0] * t, z0 + nrm[1] * t)]
        ax.add_patch(Polygon(p, closed=True, **kw))

    fig = plt.figure(figsize=(16, 10.5))
    gs = fig.add_gridspec(2, 3, height_ratios=[1, 1.1])
    spine_kw = dict(fc="#2e7d32", ec="#1b5e20", alpha=0.85)

    # --- three cross-sections
    for i, (yv, title) in enumerate([(10.5, "Y=10.5  encoder end"),
                                      (25.0, "Y=25  ESP32 + display"),
                                      (46.0, "Y=46  speaker + display")]):
        ax = fig.add_subplot(gs[0, i])
        section(ax, 1, yv, (0, 2))
        if SPINE_Y[0] <= yv <= SPINE_Y[1]:
            ax.add_patch(Rectangle((SPINE_X[0], BOARD_BOTTOM_Z), PCB_T,
                                   SPINE_TOP_Z - BOARD_BOTTOM_Z, **spine_kw))
        if LCD_Y[0] <= yv <= LCD_Y[1]:
            slanted_rect(ax, "R", LCD_ZC, LCD["wid"], LCD["t"], fc="#1565c0", alpha=0.8)
        if abs(yv - SPK["yc"]) <= SPK["len"] / 2:
            slanted_rect(ax, "L", SPK["zc"], SPK["wid"], SPK["t"], fc="#ef6c00", alpha=0.75)
        for (zy, zz, side, lab) in ZONES:
            if zy[0] <= yv <= zy[1]:
                h = 3.1 if "ESP32" in lab else 2.0
                x = SPINE_X[1] if side == "R" else SPINE_X[0] - h
                ax.add_patch(Rectangle((x, zz[0]), h, zz[1] - zz[0], fc="#9e9e9e", ec="k", lw=0.5))
        if yv < TAB_Y[1] + 0.5:
            ax.add_patch(Polygon(tab_outline(), closed=True, fc="#2e7d32", alpha=0.35, ec="#1b5e20"))
            ax.add_patch(Rectangle((CX - ENC_BODY / 2, KNOB_AXIS_Z - ENC_BODY / 2), ENC_BODY, ENC_BODY,
                                   fc="none", ec="#6a1b9a", lw=1.5, ls="--"))
        ax.axvline(CX, color="#bbb", lw=0.5, ls=":")
        ax.set_aspect("equal"); ax.set_title(title, fontsize=10)
        ax.set_xlim(90, 138); ax.set_ylim(-2, 42); ax.grid(lw=0.3)

    # --- side view of the spine
    ax = fig.add_subplot(gs[1, :])
    section(ax, 0, CX, (1, 2))
    ax.add_patch(Polygon(spine_outline(), closed=True, **spine_kw, label="spine PCB"))
    ax.add_patch(Rectangle((TAB_Y[0], BOARD_BOTTOM_Z), PCB_T, 28.4, fc="#2e7d32", alpha=0.45,
                           label="encoder tab (same PCB, soldered at 90°)"))
    ax.add_patch(Rectangle((ENC_BODY_FACE_Y, KNOB_AXIS_Z - ENC_BODY / 2), ENC_BODY_H, ENC_BODY,
                           fc="#ce93d8", ec="#6a1b9a", label="EC11 body"))
    ax.plot([ENC_SHAFT_TIP_Y, ENC_BODY_FACE_Y], [KNOB_AXIS_Z] * 2, color="#6a1b9a", lw=4)
    ax.add_patch(Rectangle((USB_FACE_Y - USB_LEN, USB_Z[0] + 0.07), USB_LEN, 8.94 - 0.14,
                           fc="#b0bec5", ec="k", label="USB-C (mid-mount)"))
    ax.add_patch(Rectangle((LCD_Y[0], LCD_ZC - LCD["wid"] / 2 * 0.866), LCD["len"], LCD["wid"] * 0.866,
                           fc="none", ec="#1565c0", lw=1.5, ls="--", label="display (right face, projected)"))
    ax.add_patch(Rectangle((SPK["yc"] - SPK["len"] / 2, SPK["zc"] - SPK["wid"] / 2 * 0.866), SPK["len"],
                           SPK["wid"] * 0.866, fc="none", ec="#ef6c00", lw=1.5, ls="--",
                           label="speaker (left face, projected, ASSUMED 35x25)"))
    for (zy, zz, side, lab) in ZONES:
        ax.add_patch(Rectangle((zy[0], zz[0]), zy[1] - zy[0], zz[1] - zz[0],
                               fc="#e0e0e0" if side == "L" else "#ffffff", ec="k", lw=0.6, alpha=0.9))
        ax.text((zy[0] + zy[1]) / 2, (zz[0] + zz[1]) / 2, f"{lab}\n[{side}]", ha="center",
                va="center", fontsize=6.5)
    ax.axvspan(*LID_OPENING_Y, ymin=0, ymax=0.04, color="#c62828", alpha=0.5)
    ax.text(LID_OPENING_Y[0], -1.4, "current lid opening (71 mm)", color="#c62828", fontsize=7)
    ax.set_aspect("equal"); ax.set_xlim(-7, 82); ax.set_ylim(-2.5, 41); ax.grid(lw=0.3)
    ax.set_title("Side view at the centre plane (looking from the speaker side).  "
                 "[R] = part on the display side of the spine, [L] = speaker side", fontsize=10)
    ax.legend(loc="upper right", fontsize=7, ncol=2)
    fig.suptitle("Globo — one board in the triangle (fit check against PRINT_01_Enclosure_USBV)", fontsize=13)
    fig.tight_layout()
    fig.savefig(path, dpi=110)


# ── KiCad export ──────────────────────────────────────────────────────────
LAYERS = """  (layers
    (0 "F.Cu" signal) (31 "B.Cu" signal)
    (32 "B.Adhes" user "B.Adhesive") (33 "F.Adhes" user "F.Adhesive")
    (34 "B.Paste" user) (35 "F.Paste" user)
    (36 "B.SilkS" user "B.Silkscreen") (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user) (39 "F.Mask" user)
    (40 "Dwgs.User" user "User.Drawings") (41 "Cmts.User" user "User.Comments")
    (44 "Edge.Cuts" user) (45 "Margin" user)
    (46 "B.CrtYd" user "B.Courtyard") (47 "F.CrtYd" user "F.Courtyard")
    (48 "B.Fab" user) (49 "F.Fab" user)
  )"""


def kicad(path):
    items = []

    def poly(pts, layer, w=0.1):
        for a, b in zip(pts, pts[1:] + pts[:1]):
            items.append(f'  (gr_line (start {a[0]:.3f} {a[1]:.3f}) (end {b[0]:.3f} {b[1]:.3f}) '
                         f'(stroke (width {w}) (type solid)) (layer "{layer}"))')

    def text(s, x, y, layer, size=1.0):
        s = s.replace("\n", " ").replace('"', "'")
        items.append(f'  (gr_text "{s}" (at {x:.3f} {y:.3f}) (layer "{layer}") '
                     f'(effects (font (size {size} {size}) (thickness 0.15))))')

    # spine: board x = Y + 20, board y = 60 - Z   (viewed from the display side [R])
    S = lambda y, z: (y + 20.0, 60.0 - z)
    poly([S(*p) for p in spine_outline()], "Edge.Cuts", 0.1)
    for (zy, zz, side, lab) in ZONES:
        layer = "F.Fab" if side == "R" else "B.Fab"
        poly([S(zy[0], zz[0]), S(zy[1], zz[0]), S(zy[1], zz[1]), S(zy[0], zz[1])], layer)
        text(lab, *S((zy[0] + zy[1]) / 2, (zz[0] + zz[1]) / 2), layer, 0.8)
    text("GLOBO spine - F = display side, B = speaker side", *S(46, 29.5), "Cmts.User", 1.2)

    # encoder tab, drawn next to the spine: x = X - CX + 125, y = 60 - Z
    T = lambda x, z: (x - CX + 125.0, 60.0 - z)
    poly([T(*p) for p in tab_outline()], "Edge.Cuts", 0.1)
    (sx, sz) = TAB_SLOT
    poly([T(sx[0], sz[0]), T(sx[1], sz[0]), T(sx[1], sz[1]), T(sx[0], sz[1])], "Edge.Cuts", 0.1)
    e = ENC_BODY / 2
    poly([T(CX - e, KNOB_AXIS_Z - e), T(CX + e, KNOB_AXIS_Z - e),
          T(CX + e, KNOB_AXIS_Z + e), T(CX - e, KNOB_AXIS_Z + e)], "F.Fab")
    text("EC11 (snap-in)", *T(CX, KNOB_AXIS_Z), "F.Fab", 0.8)
    text("encoder tab - solder 90deg to spine tongue", *T(CX, 33.5), "Cmts.User", 1.0)

    with open(path, "w") as f:
        f.write('(kicad_pcb (version 20221018) (generator "globo_board.py")\n')
        f.write('  (general (thickness 1.6))\n  (paper "A4")\n')
        f.write(LAYERS + "\n  (setup (pad_to_mask_clearance 0))\n  (net 0 \"\")\n")
        f.write("\n".join(items) + "\n)\n")


if __name__ == "__main__":
    for line in checks():
        print(line)
    kicad(os.path.join(HERE, "globo-board.kicad_pcb"))
    render(os.path.join(HERE, "fitcheck.png"))
    print("wrote globo-board.kicad_pcb, fitcheck.png")
