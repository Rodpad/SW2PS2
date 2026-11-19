#!/usr/bin/env python3
"""Generate the SW2PS2 icon (256x256 PNG; the build converts it to sw2ps2.jpg)."""
import struct, zlib, sys
from pathlib import Path

W = H = 256
px = [[(26, 29, 40)] * W for _ in range(H)]
OY = 0

def rect(x0, y0, x1, y1, c, r=0):
    y0 += OY; y1 += OY
    for y in range(max(0, y0), min(H, y1)):
        for x in range(max(0, x0), min(W, x1)):
            if r:
                cx = min(max(x, x0 + r), x1 - 1 - r); cy = min(max(y, y0 + r), y1 - 1 - r)
                if (x - cx) ** 2 + (y - cy) ** 2 > r * r: continue
            px[y][x] = c

def circle(cx, cy, r, c):
    rect(cx - r, cy - r, cx + r + 1, cy + r + 1, c, r)

# Artwork spans y 28 (title) to 206 (accent band), centred in 256.
OY = (256 - (206 - 28)) // 2 - 28
rect(18, 196, 238, 206, (52, 92, 196))
LIFT = 6  # the whole controller sits this far above its first layout
OY -= LIFT
# DualShock 2: wide top, long grips angled down and out, sticks low in the middle,
# plain coloured face buttons.
def capsule(x0, y0, x1, y1, r, c):  # a thick line with round ends
    for y in range(int(min(y0, y1) - r), int(max(y0, y1) + r) + 1):
        for x in range(int(min(x0, x1) - r), int(max(x0, x1) + r) + 1):
            dx, dy = x1 - x0, y1 - y0; L = dx * dx + dy * dy
            t = 0 if L == 0 else max(0, min(1, ((x - x0) * dx + (y - y0) * dy) / L))
            if (x - x0 - t * dx) ** 2 + (y - y0 - t * dy) ** 2 <= r * r:
                yy = y + OY
                if 0 <= yy < H and 0 <= x < W: px[yy][x] = c
def poly(pts, c):  # filled polygon (even-odd)
    ys = [p[1] for p in pts]
    for y in range(int(min(ys)), int(max(ys)) + 1):
        xs = []
        for k in range(len(pts)):
            (ax, ay), (bx, by) = pts[k], pts[(k + 1) % len(pts)]
            if (ay <= y < by) or (by <= y < ay): xs.append(ax + (y - ay) * (bx - ax) / (by - ay))
        xs.sort()
        for a, b in zip(xs[0::2], xs[1::2]):
            for x in range(int(round(a)), int(round(b))):
                yy = y + OY
                if 0 <= yy < H and 0 <= x < W: px[yy][x] = c
def ring(cx, cy, r, w, c):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            if r - w <= d <= r and 0 <= y + OY < H: px[y + OY][x] = c

BODY = (58, 60, 70); WELL = (40, 42, 50); KEY = (150, 153, 165); BTN = (30, 31, 37)
# Two round lobes (D-pad, face buttons); each grip runs down from its lobe's widest, outer edge
circle(64, 112, 42, BODY)                  # left lobe: outer edge at x 22
circle(192, 112, 42, BODY)                 # right lobe: outer edge at x 234
# Grips: the inner edge runs from the edge round the sticks, slanting out to a round end
import math
def hull_fill(points, c):  # the convex hull of the points, filled: straight edges run into the curves
    pts = sorted(set(points))
    def cross(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, hi = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0: lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(hi) >= 2 and cross(hi[-2], hi[-1], p) <= 0: hi.pop()
        hi.append(p)
    poly(lo[:-1] + hi[:-1], c)
def disc(cx, cy, r): return [(cx + r * math.cos(t / 36 * 2 * math.pi), cy + r * math.sin(t / 36 * 2 * math.pi)) for t in range(36)]
hull_fill([(22, 112), (96, 112), (80, 146)] + disc(40, 174, 19), BODY)     # left: inner edge from the sticks' edge, straight down into the end's curve
hull_fill([(234, 112), (160, 112), (176, 146)] + disc(216, 174, 19), BODY)  # right
rect(64, 80, 193, 140, BODY)               # the centre between the lobes
circle(100, 146, 22, BODY); circle(156, 146, 22, BODY)  # bulges round the sticks, below the centre
RAISE = 5  # the D-pad and face buttons sit this far above the rest
OY -= RAISE
# D-pad: four arms in a round well, each pointing in to the centre
circle(64, 117, 27, WELL)
poly([(58, 96), (70, 96), (70, 107), (64, 113), (58, 107)], KEY)    # up
poly([(58, 138), (70, 138), (70, 127), (64, 121), (58, 127)], KEY)  # down
poly([(43, 111), (43, 123), (54, 123), (60, 117), (54, 111)], KEY)  # left
poly([(85, 111), (85, 123), (74, 123), (68, 117), (74, 111)], KEY)  # right
OY += RAISE
# Select (bar) and Start (arrow), the red Analog light between them
rect(104, 108, 118, 114, (120, 122, 134), 2)
poly([(140, 105), (140, 117), (151, 111)], (120, 122, 134))
circle(128, 124, 3, (214, 60, 60))
OY -= RAISE
# Face buttons: plain coloured buttons (green top, red right, blue bottom, pink left)
circle(192, 117, 30, WELL)
circle(192, 98, 9, (64, 178, 128))
circle(211, 117, 9, (214, 72, 72))
circle(192, 136, 9, (92, 130, 220))
circle(173, 117, 9, (212, 130, 190))
OY += RAISE - 4  # the sticks a little higher too
# Analog sticks, low and close together
circle(100, 150, 15, WELL); circle(100, 150, 10, (98, 100, 112)); ring(100, 150, 10, 2, (84, 86, 98))
circle(156, 150, 15, WELL); circle(156, 150, 10, (98, 100, 112)); ring(156, 150, 10, 2, (84, 86, 98))

OY += LIFT
font = {"N": ["10001", "11001", "10101", "10011", "10001"], "E": ["11111", "10000", "11110", "10000", "11111"],
        "S": ["01111", "10000", "01110", "00001", "11110"], "W": ["10001", "10001", "10101", "11011", "10001"],
        "P": ["11110", "10001", "11110", "10000", "10000"],
        "2": ["01110", "10001", "00110", "01000", "11111"], "T": ["11111", "00100", "00100", "00100", "00100"]}
def text(s, x, y, scale, c):
    for ch in s:
        g = font[ch]
        for gy, rowbits in enumerate(g):
            for gx, bit in enumerate(rowbits):
                if bit == "1": rect(x + gx * scale, y + gy * scale, x + (gx + 1) * scale, y + (gy + 1) * scale, c)
        x += (len(g[0]) + 1) * scale
OY = 0  # the title sits where SW2Transfer's does (y 28), above the centred artwork
text("SW2", 34, 28, 5, (154, 163, 181))
text("PS2", 132, 28, 5, (238, 241, 247))

# --test: a red "TEST" banner across the bottom, for builds that are not the one to play
TEST = "--test" in sys.argv
if TEST:
    sys.argv.remove("--test")
    OY = 0
    rect(0, 186, 256, 256, (200, 30, 40))
    text("TEST", 128 - (4 * 6 * 9 - 9) // 2, 199, 9, (255, 255, 255))

raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in px)
def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "icon.png"
out.write_bytes(png)
print(out, len(png), "bytes")
