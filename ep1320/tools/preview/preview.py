#!/usr/bin/env python3
"""Draw mock.c's output as a PNG at 2x (the drawing is 320 x 160 units; the OP-1's window shows y 2-146, marked).
   ./mock ARGS | python3 preview.py out.png "caption"
A plain font stands in for the OP-1's own, so text is close, not exact."""
import sys
from PIL import Image, ImageDraw, ImageFont

S = 3
COL = {14: (235, 235, 235), 12: (110, 110, 110), 4: (55, 55, 55), 1: (0, 0, 0), 2: (230, 60, 60),
       -10: (80, 140, 255), -11: (60, 210, 120), -12: (235, 235, 235), -13: (255, 150, 40)}
img = Image.new('RGB', (320 * S, 160 * S), (0, 0, 0))
d = ImageDraw.Draw(img)
try:
    FONT = lambda sz: ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', int(sz * S * 0.95))
    FONT(10)
except OSError:
    FONT = lambda sz: ImageFont.load_default()
for ln in sys.stdin:
    p = ln.split()
    if not p:
        continue
    k = p[0]
    if k == 'text':
        _, slot, al, x, y, sz, c = p[:7]
        t = ln.split(None, 7)[7].rstrip('\n')
        x, y, sz, c = int(x), int(y), int(sz), COL.get(int(c), (255, 0, 255))
        f = FONT(sz)
        w = d.textlength(t, font=f)
        X = x * S - (w if al == 'R' else 0)
        d.text((X, y * S - sz * S * 0.15), t, font=f, fill=c)
    elif k == 'fill':
        x0, y0, x1, y1, c = map(int, p[1:])
        d.rectangle([x0 * S, y0 * S, x1 * S + S - 1, y1 * S + S - 1], fill=COL[c])
    elif k == 'bar':
        x0, y0, x1, y1, v, m, c = map(int, p[1:])
        d.rectangle([x0 * S, y0 * S, x1 * S + S - 1, y1 * S + S - 1], fill=COL[4])
        xe = x0 + (x1 - x0) * v // max(m, 1)
        d.rectangle([x0 * S, y0 * S, xe * S + S - 1, y1 * S + S - 1], fill=COL[c])
    elif k == 'line':
        x0, y0, x1, y1, c = map(int, p[1:])
        d.line([x0 * S, y0 * S, x1 * S, y1 * S], fill=COL[c], width=S)
    elif k == 'dot':
        x, y, r, f, c = map(int, p[1:])
        box = [(x - r) * S, (y - r) * S, (x + r) * S, (y + r) * S]
        if f:
            d.ellipse(box, fill=COL[c])
        else:
            d.ellipse(box, outline=COL[c], width=max(1, S // 2 + 1))
d.line([0, 147 * S, 320 * S, 147 * S], fill=(40, 0, 40), width=1)   # the window's lower edge
cap = sys.argv[2] if len(sys.argv) > 2 else ''
out = Image.new('RGB', (img.width, img.height + 30), (30, 30, 34))
out.paste(img, (0, 30))
ImageDraw.Draw(out).text((8, 6), cap, font=ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 18)
                         if FONT(10) else None, fill=(200, 200, 200))
out.save(sys.argv[1])
