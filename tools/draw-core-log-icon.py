#!/usr/bin/env python3
"""Draws Export Core Log's menu icons into source/ui/qt/menu-icons: a page of
log lines with a bug on it (CoreLog.png), and the same with the green dot
MCP Server's icon has while its server runs (CoreLogOn.png), shown while the
log is kept. Drawn at 8x and scaled down, as tools/gen-icons.py draws.

    python3 tools/draw-core-log-icon.py [<folder>]
"""
import os
import sys
from PIL import Image, ImageDraw
here = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "source", "ui", "qt", "menu-icons")
K = 8
def px(v): return int(round(v * K))

def icon(dot):
    im = Image.new("RGBA", (16 * K, 16 * K), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # the page, its corner folded
    d.polygon([(px(4.5), px(0.5)), (px(12), px(0.5)), (px(15.5), px(4)), (px(15.5), px(15.5)), (px(4.5), px(15.5))],
              fill=(252, 252, 252, 255), outline=(110, 110, 110, 255), width=K)
    d.polygon([(px(12), px(0.5)), (px(12), px(4)), (px(15.5), px(4))], fill=(205, 205, 205, 255), outline=(110, 110, 110, 255), width=K)
    for y, x1 in [(3, 10.5), (5.5, 14), (8, 14), (10.5, 14), (13, 14)]:
        d.rectangle([px(6.5), px(y), px(x1), px(y + 1) - 1], fill=(70, 80, 95, 255))
    # the bug, as Chimera's Bug.png colours it
    leg = (25, 75, 80, 255)
    cx, cy = 5.2, 10.6
    for dy in (-1.6, 0.2, 2.0):
        d.line([px(cx - 2.2), px(cy + dy), px(cx - 4.4), px(cy + dy - 0.9)], fill=leg, width=K)
        d.line([px(cx + 2.2), px(cy + dy), px(cx + 4.4), px(cy + dy - 0.9)], fill=leg, width=K)
    d.line([px(cx - 0.8), px(cy - 4.0), px(cx - 2.2), px(cy - 5.6)], fill=leg, width=K)
    d.line([px(cx + 0.8), px(cy - 4.0), px(cx + 2.2), px(cy - 5.6)], fill=leg, width=K)
    d.ellipse([px(cx - 1.5), px(cy - 4.6), px(cx + 1.5), px(cy - 2.0)], fill=leg)
    d.ellipse([px(cx - 2.7), px(cy - 2.8), px(cx + 2.7), px(cy + 4.4)], fill=(150, 200, 120, 255), outline=leg, width=K)
    d.line([px(cx), px(cy - 2.2), px(cx), px(cy + 4.0)], fill=leg, width=K // 2)
    if dot:
        d.ellipse([px(10.2), px(10.2), px(15.8), px(15.8)], fill=(255, 255, 255, 255))
        d.ellipse([px(10.8), px(10.8), px(15.2), px(15.2)], fill=(60, 180, 90, 255))
    return im.resize((16, 16), Image.LANCZOS)

icon(False).save(out + "/CoreLog.png")
icon(True).save(out + "/CoreLogOn.png")
