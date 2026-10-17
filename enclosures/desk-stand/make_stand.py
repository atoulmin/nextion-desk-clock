"""Desk stand for the Open Nextion ONX2424G013 knob.

A flat square base plate with a neck rising from the middle that holds the knob in a
tilted cup, face angled towards the user. The cup is the same as the wall plate's
(housing in a bore, front ring 1 mm proud), continued backwards as a tube long enough
for a straight USB-C plug. The neck is hollow and open underneath; the cable drops out
of the end of the plug space and leaves through a groove under the base to the back.

The USB-C socket is 10.8 mm off centre, right next to one of the three M1.6 nuts, so
only the other two nuts get a screw post (that one would foul the plug). The knob sits
turned 90 degrees from the wall plate so both screws are on the low side of the cup:
turn the stand over and a driver reaches them straight up the neck. That means the
display needs rotating 90 degrees in the firmware compared with the wall plate.

Coordinates: mm, z = 0 is the desk, +z up, -y towards the user, x to the user's right.
Run: pip install -r ../requirements.txt, then python make_stand.py
"""
import math

import numpy as np
import trimesh
from manifold3d import CrossSection, JoinType, Manifold, set_circular_segments

set_circular_segments(128)

# Stand
TILT = 45.0                   # knob axis elevation above horizontal (face 45 deg back from vertical)
RIM_HEIGHT = 55.0             # height of the cup mouth centre above the desk
BASE_W = 95.0                 # square base plate
BASE_Y0 = -32.0               # front edge of the base (cup mouth centre is at y = 0)
BASE_H = 7.0
BASE_CORNER_R = 8.0
NECK_W = 52.0                 # neck footprint on top of the base
NECK_Y0, NECK_Y1 = -16.0, 52.0
NECK_CORNER_R = 14.0
WALL = 2.6
CABLE_GROOVE_W, CABLE_GROOVE_H = 7.0, 5.0   # under the base, from the neck to the back edge
FOOT_D, FOOT_RECESS, FOOT_INSET = 10.5, 1.0, 10.0   # stick-on rubber bumpers, locating recess
DRIVER_D = 4.0                # screwdriver shaft that must reach the screws from underneath

# Knob (from ONX2424G013-Dimension.pdf), same as the wall plate
HOUSING_D = 43.27
RING_D = 48.0
RING_T = 17.0
HOUSING_DEPTH = 25.4 - 17.0
CONNECTOR_DEPTH = 28.0 - 25.4
RING_GAP = 1.0
FIT = 0.7
# Positions as seen from the front in the drawing's orientation (back view mirrored left-right)
NUTS_USED = [(-9.11, 14.72), (-9.11, -14.42)]
USB_C = (10.8, 0.0)           # socket centre, long side vertical
PLUG_W, PLUG_T = 18.0, 10.0   # clearance for the plug overmould (along the socket, across it)
PLUG_LEN = 45.0               # straight plug + strain relief behind the socket before the cable bends
KNOB_ROT = 90.0               # knob turned anticlockwise (seen from the front) from the drawing


def knob_xy(x, y):
    a = math.radians(KNOB_ROT)
    return x * math.cos(a) - y * math.sin(a), x * math.sin(a) + y * math.cos(a)


# Cup (local frame: knob axis = +z, z = 0 at the cup mouth, +y = up the face)
CUP_WALL = 2.0
FLOOR_T = 2.0
POST_D = 5.0
POST_H = CONNECTOR_DEPTH + 0.4
NUT_SCREW_D = 1.8
NUT_HEAD_D, NUT_HEAD_DEPTH = 3.4, 1.2
FLOOR_OPENING_R = 13.0

housing_back_z = RING_GAP - HOUSING_DEPTH
post_bottom_z = housing_back_z - POST_H
floor_bottom_z = post_bottom_z - FLOOR_T
plug_end_z = housing_back_z - CONNECTOR_DEPTH - PLUG_LEN
tube_end_z = plug_end_z - 6.0

cup_in_d = HOUSING_D + FIT
cup_out_d = cup_in_d + 2 * CUP_WALL
nuts = [knob_xy(x, y) for x, y in NUTS_USED]
usb = knob_xy(*USB_C)

e = math.radians(TILT)
AXIS = np.array([0.0, -math.cos(e), math.sin(e)])
UP = np.array([0.0, math.sin(e), math.cos(e)])
ORIGIN = np.array([0.0, 0.0, RIM_HEIGHT])


def place(m):
    """Local knob frame -> world."""
    return m.rotate([90.0 - TILT, 0, 0]).translate(ORIGIN.tolist())


def world(x, y, z):
    return ORIGIN + x * np.array([1.0, 0, 0]) + y * UP + z * AXIS


def rounded_rect(w, h, r):
    return CrossSection.square([w - 2 * r, h - 2 * r], center=True).offset(r, JoinType.Round)


def cylinder(d, z0, z1, x=0.0, y=0.0):
    return Manifold.cylinder(z1 - z0, d / 2, d / 2).translate([x, y, z0])


def cone(d0, d1, z0, z1, x=0.0, y=0.0):
    return Manifold.cylinder(z1 - z0, d0 / 2, d1 / 2).translate([x, y, z0])


base_cy = BASE_Y0 + BASE_W / 2
neck_cy = (NECK_Y0 + NECK_Y1) / 2
desk_side = Manifold.cube([400, 400, 400]).translate([-200, -200, 0])


def neck_foot(inset, z0, z1):
    r = rounded_rect(NECK_W - 2 * inset, NECK_Y1 - NECK_Y0 - 2 * inset, NECK_CORNER_R - inset)
    return r.extrude(z1 - z0).translate([0, neck_cy, z0])


outer_tube = place(cylinder(cup_out_d, tube_end_z, 0.0))
inner_tube = place(cylinder(cup_out_d - 2 * WALL, tube_end_z + WALL, floor_bottom_z))

# Base plate + neck (hull of the tilted tube and the footprint), hollowed and open underneath
base = rounded_rect(BASE_W, BASE_W, BASE_CORNER_R).extrude(BASE_H).translate([0, base_cy, 0])
neck = Manifold.batch_hull([outer_tube, neck_foot(0, 0, BASE_H)])
cavity = Manifold.batch_hull([inner_tube, neck_foot(WALL, -1.0, BASE_H)])
body = ((base + neck) ^ desk_side) - cavity

# Cup bore, floor opening (round middle plus a slot for the off-centre plug)
cup = cylinder(cup_in_d, post_bottom_z, 50.0)
cup += cone(cup_in_d, cup_in_d + 1.6, -0.8, 0.001)
floor_hole = cylinder(FLOOR_OPENING_R * 2, floor_bottom_z - 1, post_bottom_z + 0.01)
plug_slot = rounded_rect(PLUG_T, PLUG_W, 2.0).rotate(KNOB_ROT).extrude(PLUG_LEN + 10).translate(
    [usb[0], usb[1], plug_end_z - 5])
body -= place(cup + floor_hole + plug_slot)

# Screw posts on the cup floor, then screw holes and counterbores from behind
for x, y in nuts:
    body += place(cylinder(POST_D, post_bottom_z - 0.01, housing_back_z, x, y))
for x, y in nuts:
    body -= place(cylinder(NUT_SCREW_D, floor_bottom_z - 1, housing_back_z + 1, x, y))
    body -= place(cylinder(NUT_HEAD_D, floor_bottom_z - 1, floor_bottom_z + NUT_HEAD_DEPTH, x, y))

# Rubber feet recesses and the cable groove under the base
c = BASE_W / 2 - FOOT_INSET
for fx in (-c, c):
    for fy in (-c, c):
        body -= cylinder(FOOT_D, -1, FOOT_RECESS, fx, base_cy + fy)
groove = rounded_rect(CABLE_GROOVE_W, CABLE_GROOVE_H * 2, CABLE_GROOVE_W / 2 - 0.01).extrude(BASE_W)
body -= groove.rotate([90, 0, 0]).translate([0, base_cy + BASE_W, 0])

body = body ^ desk_side

mesh = body.to_mesh()
tm = trimesh.Trimesh(vertices=np.asarray(mesh.vert_properties)[:, :3], faces=np.asarray(mesh.tri_verts))
assert tm.is_watertight
tm.export("onx2424g013_desk_stand.stl")

# Checks
face_centre = world(0, 0, RING_GAP + RING_T)
plug_end = min((world(usb[0] + dx, usb[1] + dy, plug_end_z) for dx in (-PLUG_W / 2, PLUG_W / 2)
                for dy in (-PLUG_W / 2, PLUG_W / 2)), key=lambda p: p[2])
ring_front = world(0, -RING_D / 2, RING_GAP + RING_T)
press_line_hits_desk_y = face_centre[1] + face_centre[2] / math.tan(e)
print(f"base {BASE_W} x {BASE_W} x {BASE_H} mm, y {BASE_Y0:.0f}..{BASE_Y0 + BASE_W:.0f}; "
      f"stand height {tm.bounds[1][2]:.1f} mm")
print(f"display centre {face_centre[2]:.1f} mm above the desk, face {90 - TILT:.0f} deg from horizontal")
print(f"ring front edge at y {ring_front[1]:.1f} (base front {BASE_Y0})")
print(f"pressing the knob pushes along a line that meets the desk at y {press_line_hits_desk_y:.1f} "
      f"(base back {BASE_Y0 + BASE_W})")
print(f"lowest point of the plug end {plug_end[2]:.1f} mm above the desk")
for x, y in nuts:
    shaft = place(cylinder(DRIVER_D, -300, floor_bottom_z - 0.3, x, y)) ^ desk_side
    clear = (shaft ^ body).volume() < 0.01
    print(f"screw at ({x:.1f}, {y:.1f}): driver reaches it straight from underneath: {'yes' if clear else 'NO'}")
print(f"M1.6 screws ~{FLOOR_T + POST_H - NUT_HEAD_DEPTH + 2:.0f} mm long")
print(f"bounds {tm.bounds.round(1).tolist()}, volume {tm.volume / 1000:.1f} cm3")

# Thinnest neck wall: the cavity is convex, so grow it by t (hull of shifted copies) and see if it pokes out
dirs = [np.array(v, float) for v in np.ndindex(3, 3, 3) if v != (1, 1, 1)]
dirs = [(v - 1) / np.linalg.norm(v - 1) for v in dirs]
above = Manifold.cube([400, 400, 400]).translate([-200, -200, BASE_H])
for t in (1.5, 2.0, 2.5):
    grown = Manifold.batch_hull([cavity.translate((d * t).tolist()) for d in dirs])
    poke = ((grown ^ above) - neck).volume()
    print(f"neck wall at least {t} mm thick: {'yes' if poke < 1 else f'no ({poke:.0f} mm3 pokes out)'}")

# Preview renders
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

views = [(25, -60, "front"), (20, 120, "back"), (-60, -60, "underneath"), (0, 0, "side")]
fig = plt.figure(figsize=(16, 4.5), facecolor="black")
for i, (elev, azim, title) in enumerate(views):
    ax = fig.add_subplot(1, 4, i + 1, projection="3d", facecolor="black")
    tris = tm.vertices[tm.faces]
    cam = np.array([math.cos(math.radians(elev)) * math.cos(math.radians(azim)),
                    math.cos(math.radians(elev)) * math.sin(math.radians(azim)), math.sin(math.radians(elev))])
    light = cam + np.array([0.3, 0.2, 0.5])
    shade = np.clip(tm.face_normals @ (light / np.linalg.norm(light)), 0.15, 1)
    ax.add_collection3d(Poly3DCollection(tris, facecolors=np.c_[shade, shade * 0.6, shade * 0.1, np.ones_like(shade)],
                                         linewidths=0))
    ax.set_xlim(-50, 50); ax.set_ylim(-35, 65); ax.set_zlim(0, 100)
    ax.set_box_aspect((1, 1, 1))
    ax.view_init(elev, azim)
    ax.set_axis_off()
    ax.set_title(title, color="white")
plt.tight_layout()
plt.savefig("preview.png", dpi=100, facecolor="black")
