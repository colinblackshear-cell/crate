# Draws the Kaleidobox app icon (kaleidobox-icon.png, 1024 x 1024) with Pillow and numpy:
# a twelve-fold kaleidoscope in the Rig's panel colours, seen through a brass eyepiece on a
# dark panel. Run: python3 make_icon.py
import numpy as np
from PIL import Image, ImageFilter
import os

S = 2048                      # drawn at twice the size, then scaled down for smooth edges
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "kaleidobox-icon.png")

def hexc(h):
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float32) / 255.0

INK    = hexc("15131a")
CREAM  = hexc("f1e7d1")
AMBER  = hexc("ffb54a")
ORANGE = hexc("f39b33")
RED    = hexc("e8432f")
YELLOW = hexc("efcb45")
VIOLET = hexc("8b5cf6")
TEAL   = hexc("2bb3a3")
MAGENTA= hexc("d9468f")
BRASS  = hexc("c9a24a")

y, x = np.mgrid[0:S, 0:S].astype(np.float32)
cx = cy = S / 2
dx, dy = (x - cx) / (S / 2), (y - cy) / (S / 2)
r = np.sqrt(dx * dx + dy * dy)
a = np.arctan2(dy, dx)

img = np.zeros((S, S, 3), np.float32)
alpha = np.zeros((S, S), np.float32)

def soft(d, w=0.004):
    """1 inside (d < 0), 0 outside, with a soft edge of width w."""
    return np.clip(0.5 - d / w, 0.0, 1.0)

def paint(mask, colour):
    global img
    img = img * (1 - mask[..., None]) + colour * mask[..., None]

# The macOS icon shape: a rounded square (superellipse) about 80% of the canvas, with a shadow.
half = 0.805
n = 5.0
sq = (np.abs(dx) / half) ** n + (np.abs(dy) / half) ** n
body = soft(sq - 1.0, 0.02)
shadow = soft(((np.abs(dx) / half) ** n + (np.abs((dy - 0.025)) / half) ** n) - 1.0, 0.12)
alpha = np.maximum(body, shadow * 0.45)

# The panel: dark, with a faint violet nebula and an amber glow, like the Guide's sky.
panel = INK[None, None, :] * np.ones((S, S, 1), np.float32)
neb = np.exp(-(((dx - 0.35) ** 2 + (dy + 0.4) ** 2) / 0.25))[..., None]
glow = np.exp(-(((dx + 0.45) ** 2 + (dy - 0.5) ** 2) / 0.18))[..., None]
panel = panel + VIOLET * neb * 0.22 + AMBER * glow * 0.10
img = panel * 1.0
# A few stars.
rng = np.random.default_rng(42)
for _ in range(70):
    sx, sy = rng.uniform(-0.75, 0.75, 2)
    if np.hypot(sx, sy) < 0.7:
        continue
    rad = rng.uniform(0.003, 0.008)
    m = soft(np.hypot(dx - sx, dy - sy) - rad, 0.004) * rng.uniform(0.35, 0.9)
    paint(m, CREAM)

# The kaleidoscope: fold the angle into a 30-degree mirrored wedge (twelve-fold symmetry).
N = 12
wedge = 2 * np.pi / N
t = np.mod(a, wedge)
t = np.minimum(t, wedge - t) / (wedge / 2)      # 0 on a mirror line, 1 mid-wedge
R = 0.60                                       # the eyepiece's inner radius
inside = soft(r - R, 0.006)
k = img.copy()

def layer(mask, colour):
    global k
    k = k * (1 - mask[..., None]) + colour * mask[..., None]

base = np.clip(r / R, 0, 1)
layer(np.ones_like(r), INK * 0.6 + VIOLET * 0.25)
# Outer violet petals (point outward on the mirror lines).
layer(soft(r - (R * (0.98 - 0.30 * t))), VIOLET)
# Magenta inner petals, between them.
layer(soft(r - (R * (0.86 - 0.36 * (1 - t)))), MAGENTA)
# A ring of teal diamonds.
dm = np.abs(r - R * 0.70) / (R * 0.10) + t * 1.4
layer(soft(dm - 1.0, 0.03), TEAL)
# Orange flames.
layer(soft(r - (R * (0.62 - 0.24 * t ** 0.7))), ORANGE)
# Red star.
layer(soft(r - (R * (0.48 - 0.20 * (1 - t) ** 1.5))), RED)
# Yellow and amber core rings.
layer(soft(r - R * (0.36 - 0.05 * np.cos(np.pi * t))), YELLOW)
layer(soft(r - R * 0.26), AMBER)
# Cream petals in the centre.
layer(soft(r - (R * (0.22 - 0.12 * t))), CREAM)
layer(soft(r - R * 0.075), INK)
layer(soft(r - R * 0.045), AMBER)
# Little cream jewels on the mirror lines.
for rr, size in ((0.80, 0.035), (0.55, 0.03)):
    jx = R * rr
    # distance to the point on the mirror line at radius rr (in folded coordinates)
    fx = r * np.cos(t * wedge / 2)
    fy = r * np.sin(t * wedge / 2)
    layer(soft(np.hypot(fx - jx, fy) - size, 0.006), CREAM)
# Glass shading: lighter top-left, darker toward the rim.
shade = 1.0 + 0.12 * (-dx - dy) / R - 0.35 * np.clip((r / R) - 0.75, 0, 1) / 0.25
k = np.clip(k * shade[..., None], 0, 1)
img = img * (1 - inside[..., None]) + k * inside[..., None]

# The brass eyepiece: a bevelled ring with knurled ticks, like a Rig knob.
ring_in, ring_out = R, R + 0.11
ring = soft(r - ring_out, 0.006) * (1 - soft(r - ring_in, 0.006))
light = 0.75 + 0.35 * (-(dx + dy) / np.maximum(r, 1e-3)) / 1.4142
bev = np.abs((r - (ring_in + ring_out) / 2) / ((ring_out - ring_in) / 2))
brass = BRASS * (light * (1.05 - 0.35 * bev ** 2))[..., None]
ticks = (np.cos(a * 72) > 0.6) & (r > ring_out - 0.035)
brass = np.where(ticks[..., None], brass * 0.72, brass)
img = img * (1 - ring[..., None]) + np.clip(brass, 0, 1) * ring[..., None]
# Dark grooves at the ring's edges.
for rr in (ring_in, ring_out - 0.035):
    g = soft(np.abs(r - rr) - 0.004, 0.004)
    img = img * (1 - 0.6 * g[..., None])
# A glint on the glass.
gl = soft(np.hypot((dx + 0.22) / 1.6, dy + 0.28) - 0.07, 0.08) * 0.22
img = img + gl[..., None] * (1 - img) * inside[..., None]

# Clip to the icon shape (the shadow outside is black).
img = img * body[..., None]
rgba = np.dstack([np.clip(img, 0, 1), alpha])
im = Image.fromarray((rgba * 255).astype(np.uint8), "RGBA").resize((1024, 1024), Image.LANCZOS)
im.save(out)
print("wrote", out)
