#!/usr/bin/env python3
"""Draws the icons Chimera owns, replacing art inherited from BizHawk that
depicted copyrighted Nintendo game objects (a plumber sprite, the fire and
ice flowers, the question block).

    python3 tools/gen-icons.py [--preview <file.png>]

What it writes, into source/ui/qt/images:

  TAStudio.png    the Chimera eye in TAStudio's own colours
  Freeze.png      a snowflake (freeze an address; also cheats)
  Unfreeze.png    the same flake, thawed

The eye is drawn from the same 8x8 cell grid as images/chimera.png, so the
mark stays the mark and only the palette changes. Everything else is drawn
at 8x and downsampled, which is what gives the small sizes their soft edges
(the neighbouring toolbar icons look the same way).
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.join(HERE, "..", "source", "ui", "qt", "images")

# the brand palette, read off images/chimera.png
TEAL = (45, 179, 166)
PURPLE = (138, 99, 232)
SLATE = (74, 85, 104)
AMBER = (246, 168, 33)
IRIS = (59, 130, 246)

# TAStudio's eye: the same mark in the tool's own colours. A triadic shift of
# the brand hues - unmistakably the same eye, never mistaken for the app icon
# at toolbar size.
T_RING_A = (206, 74, 168)   # was teal
T_RING_B = (232, 116, 63)   # was purple
T_RING_C = (86, 196, 118)   # was amber
T_SLATE = (74, 85, 104)     # unchanged: the neutral quarter
T_IRIS = (236, 84, 122)     # was blue

def draw_eye(size, source="chimera.png"):
    """The Chimera mark itself, recoloured.

    Reads the mark and substitutes each brand colour for TAStudio's, keeping
    every pixel's shading and alpha - so the shape, the ring gaps and the
    pupil are the artwork's, not an approximation of it. The 16px icon comes
    from the hand-tuned ChimeraSmall.png rather than a downsample of the big
    one, because at that size every pixel is a decision someone made.
    """
    src = Image.open(os.path.join(os.path.normpath(IMAGES), source)).convert("RGBA")
    swap = {TEAL: T_RING_A, PURPLE: T_RING_B, AMBER: T_RING_C, SLATE: T_SLATE, IRIS: T_IRIS}
    brands = list(swap)
    out = Image.new("RGBA", src.size, (0, 0, 0, 0))
    sp, op = src.load(), out.load()
    for y in range(src.height):
        for x in range(src.width):
            r, g, b, a = sp[x, y]
            if a == 0:
                continue
            # nearest brand colour decides the substitution; the pixel's own
            # brightness against that colour carries the anti-aliasing over
            near = min(brands, key=lambda c: (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2)
            tr, tg, tb = swap[near]
            scale = (sum((r, g, b)) + 1) / (sum(near) + 1)
            op[x, y] = (min(255, int(tr * scale)), min(255, int(tg * scale)),
                        min(255, int(tb * scale)), a)
    return out if out.size == (size, size) else out.resize((size, size), Image.LANCZOS)


def draw_flake(size, thawed=False):
    """A six-armed snowflake: freeze, and its thawed twin."""
    S = 256
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    import math

    # thawed keeps every arm and loses its colour: the pair has to read as
    # one state and its opposite, not as a flake and a broken flake
    # the thawed flake keeps a slate-blue cast rather than going pale grey:
    # a light toolbar swallows pale grey at 16px
    body = (104, 120, 138) if thawed else (72, 178, 236)
    core = (150, 165, 182) if thawed else (206, 236, 252)
    cx, cy = S / 2, S / 2 - (S * 0.03 if thawed else 0)
    arm = S * 0.44 * (0.88 if thawed else 1.0)
    width = int(S * 0.045)

    for i in range(6):
        ang = math.radians(i * 60 + 90)
        ex, ey = cx + math.cos(ang) * arm, cy + math.sin(ang) * arm
        d.line([cx, cy, ex, ey], fill=body + (255,), width=width)
        for frac, blen in ((0.50, 0.30), (0.80, 0.20)):
            bx, by = cx + math.cos(ang) * arm * frac, cy + math.sin(ang) * arm * frac
            for side in (-1, 1):
                bang = ang + side * math.radians(45)
                d.line([bx, by,
                        bx + math.cos(bang) * arm * blen,
                        by + math.sin(bang) * arm * blen],
                       fill=body + (255,), width=max(2, int(width * 0.75)))

    r = S * 0.07
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=core + (255,))

    if thawed:
        # one drop coming off it: frozen, and no longer frozen
        drop = (72, 178, 236, 255)
        dx, dy = cx + arm * 0.70, cy + arm * 0.95
        dr = S * 0.085
        d.ellipse([dx - dr, dy - dr * 0.8, dx + dr, dy + dr * 1.2], fill=drop)
        d.polygon([(dx, dy - dr * 2.2), (dx - dr * 0.8, dy + dr * 0.1),
                   (dx + dr * 0.8, dy + dr * 0.1)], fill=drop)

    return img.resize((size, size), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also write a scaled-up sheet here")
    args = ap.parse_args()

    out = os.path.normpath(IMAGES)
    if not os.path.isdir(out):
        sys.exit(f"no images directory at {out}")

    tas = draw_eye(16, source="ChimeraSmall.png")
    tas.save(os.path.join(out, "TAStudio.png"))

    freeze = draw_flake(16)
    freeze.save(os.path.join(out, "Freeze.png"))

    unfreeze = draw_flake(16, thawed=True)
    unfreeze.save(os.path.join(out, "Unfreeze.png"))

    print("wrote TAStudio.png, Freeze.png, Unfreeze.png")

    if args.preview:
        tiles = [("TAStudio", tas), ("Freeze", freeze), ("Unfreeze", unfreeze)]
        tw = 96
        sheet = Image.new("RGBA", (len(tiles) * tw, tw + 30), (32, 32, 32, 255))
        d = ImageDraw.Draw(sheet)
        for i, (name, im) in enumerate(tiles):
            big = im.resize((80, 80), Image.NEAREST)
            sheet.paste(big, (i * tw + 8, 8), big)
            d.text((i * tw + 8, 92), name, fill=(230, 230, 230, 255))
        sheet.convert("RGB").save(args.preview)
        print("preview ->", args.preview)


if __name__ == "__main__":
    main()
