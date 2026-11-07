#!/usr/bin/env python3
"""Wreck knob filmstrip, made from the user's own knob art (art_src/knob_with_pointer.png, art_src/knob_base.png).

Their pixels stay: the rim, the dark face, the tick marks and the pointer are theirs. Only colours change:
  * the gold rim and the pointer take the Wreck gold gradient (light yellow at the top, rich gold at the bottom),
    keeping their own light/dark shading;
  * the tick marks light up in the same gold from the start of the travel to the current value (the mock-up's value
    arc), the rest stay as given.
The pointer turns -135..+135 degrees about the centre of the face; the ticks and face stay still.
Output: art/knob_strip.png, FRAMES frames of SIZE x SIZE stacked downwards (minimum first)."""
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "art_src")
OUT = os.path.join(ROOT, "art")
SIZE = int(os.environ.get("KNOB_SIZE", "126"))   # the mock-up's knob is 176 px
FRAMES = int(os.environ.get("KNOB_FRAMES", "128"))
SS = 3                                            # supersample: build at SIZE*SS, then reduce

STOPS = [(0.00, (0xf7, 0xf1, 0x85)), (0.25, (0xf2, 0xe1, 0x60)), (0.45, (0xe8, 0xc9, 0x42)),
         (0.62, (0xe0, 0xb2, 0x27)), (0.82, (0xdd, 0xa8, 0x16)), (1.00, (0xe7, 0xb2, 0x1e))]


def gradient(t):
    """Wreck gold at vertical position t (0 top .. 1 bottom); t is an array."""
    t = np.clip(t, 0, 1)
    out = np.zeros(t.shape + (3,), np.float32)
    for c in range(3):
        out[..., c] = np.interp(t, [s[0] for s in STOPS], [s[1][c] for s in STOPS])
    return out


def gold_mask(a):
    r, g, b = [a[..., i].astype(np.int32) for i in range(3)]
    return (a[..., 3] > 40) & (r > 120) & (r - b > 45) & (g > 80)


def fit_disc(a):
    """Centre and outer radius of the gold rim (the pointer is inside it, so the bounding box is the rim's)."""
    m = gold_mask(a)
    ys, xs = np.nonzero(m)
    return (xs.min() + xs.max()) / 2.0, (ys.min() + ys.max()) / 2.0, (xs.max() - xs.min()) / 2.0, (ys.max() - ys.min()) / 2.0


def main():
    A = np.array(Image.open(os.path.join(SRC, "knob_with_pointer.png")).convert("RGBA"))
    B = np.array(Image.open(os.path.join(SRC, "knob_base.png")).convert("RGBA"))
    ax, ay, arx, ary = fit_disc(A)
    bx, by, brx, bry = fit_disc(B)
    ra, rb = (arx + ary) / 2, (brx + bry) / 2
    # reach of the artwork from the face centre (ticks), a square canvas around it
    ys, xs = np.nonzero(A[..., 3] > 40)
    reach = max(np.abs(xs - ax).max(), np.abs(ys - ay).max()) + 4
    N = int(round(2 * reach))
    # 1. their face + rim without the pointer, from the base image, scaled so the rims match
    sc = ra / rb
    Bi = Image.fromarray(B).resize((int(round(B.shape[1] * sc)), int(round(B.shape[0] * sc))), Image.LANCZOS)
    B2 = np.array(Bi)
    bx2, by2 = bx * sc, by * sc
    canvas = np.zeros((N, N, 4), np.uint8)
    cx = cy = N / 2.0
    yy, xx = np.mgrid[0:N, 0:N]
    dist = np.hypot(xx + 0.5 - cx, yy + 0.5 - cy)
    # tick marks from the pointer image (outside the rim), disc from the base image (inside)
    ox, oy = int(round(ax - cx)), int(round(ay - cy))
    def sample(img, ix, iy):
        out = np.zeros((N, N, 4), np.uint8)
        x0, y0 = max(0, -ix), max(0, -iy)
        x1, y1 = min(N, img.shape[1] - ix), min(N, img.shape[0] - iy)
        out[y0:y1, x0:x1] = img[y0 + iy:y1 + iy, x0 + ix:x1 + ix]
        return out
    Aw = sample(A, ox, oy)
    Bw = sample(B2, int(round(bx2 - cx)), int(round(by2 - cy)))
    inside = dist <= ra + 3
    canvas[~inside] = Aw[~inside]
    canvas[inside] = Bw[inside]
    # 2. the pointer, taken from the pointer image: gold pixels inside the rim ring
    pm = gold_mask(Aw) & (dist < ra * 0.86)
    pointer = np.zeros((N, N, 4), np.uint8)
    pointer[pm] = Aw[pm]
    # 3. tick components (outside the rim): label each by angle from 12 o'clock, clockwise
    tick = (canvas[..., 3] > 40) & (dist > ra + 6)
    from scipy import ndimage
    lab, n = ndimage.label(tick, structure=np.ones((3, 3)))
    idx = np.arange(1, n + 1)
    com = ndimage.center_of_mass(tick, lab, idx)
    angle = np.zeros(n + 1)
    for i, (my, mx) in zip(idx, com):
        angle[i] = math.degrees(math.atan2(mx + 0.5 - cx, -(my + 0.5 - cy)))   # 0 at the top, + clockwise
    tick_lab = lab

    ty = (yy + 0.5) / N   # vertical position of every pixel over the whole knob
    G = gradient(ty)
    # gold rim: recolour, keeping their shading
    rim = gold_mask(canvas) & inside
    lum = canvas[..., :3].astype(np.float32).mean(axis=2)
    ref = np.median(lum[rim])
    f = np.clip(lum / ref, 0.55, 1.2)[..., None]
    base = canvas.copy()
    base[rim, :3] = np.clip(G[rim] * f[rim], 0, 255).astype(np.uint8)
    # pointer colours: same rule
    plum = pointer[..., :3].astype(np.float32).mean(axis=2)
    pref = np.median(plum[pm]) if pm.any() else 1
    pf = np.clip(plum / pref, 0.55, 1.2)[..., None]
    pointer[pm, :3] = np.clip(G[pm] * pf[pm], 0, 255).astype(np.uint8)

    # tick lighting texture: their tick shading relative to the tick median
    tl = canvas[..., :3].astype(np.float32).mean(axis=2)
    tref = np.median(tl[tick]) if tick.any() else 1
    tf = np.clip(tl / max(tref, 1), 0.8, 1.35)[..., None]
    lit_col = np.clip(G * tf, 0, 255)

    frames = []
    pil_pointer = Image.fromarray(pointer)
    for k in range(FRAMES):
        v = k / (FRAMES - 1)
        ang = -135.0 + 270.0 * v
        fr = base.copy()
        lit_ids = [i for i in idx if angle[i] <= ang + 1.0 and -140 <= angle[i]]
        if lit_ids:
            lm = np.isin(tick_lab, lit_ids) & tick
            fr[lm, :3] = lit_col[lm].astype(np.uint8)
        pr = pil_pointer.rotate(-ang, resample=Image.BICUBIC, center=(cx, cy))   # PIL rotates anticlockwise
        im = Image.fromarray(fr)
        im.alpha_composite(pr)
        frames.append(im.resize((SIZE, SIZE), Image.LANCZOS))
    os.makedirs(OUT, exist_ok=True)
    strip = Image.new("RGBA", (SIZE, SIZE * FRAMES), (0, 0, 0, 0))
    for k, im in enumerate(frames):
        strip.paste(im, (0, k * SIZE))
    strip.save(os.path.join(OUT, "knob_strip.png"))
    frames[FRAMES // 2].save(os.path.join(OUT, "knob_mid.png"))
    # contact sheet to look at (0, 25, 50, 75, 100 %)
    sheet = Image.new("RGBA", (SIZE * 5 + 40, SIZE + 20), (0, 0, 0, 255))
    for j, k in enumerate([0, FRAMES // 4, FRAMES // 2, 3 * FRAMES // 4, FRAMES - 1]):
        sheet.alpha_composite(frames[k], (10 + j * (SIZE + 5), 10))
    sheet.convert("RGB").save(os.path.join(OUT, "knob_sheet.png"))
    print("face centre", (round(ax), round(ay)), "radius", round(ra), "canvas", N, "ticks", n, "strip", strip.size)


if __name__ == "__main__":
    main()
