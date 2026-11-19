#!/usr/bin/env python3
"""contact.py out.png cols scale file.raw... : grid of raw frames (raw2png format) as one PNG."""
import sys, struct, zlib
out, cols, scale = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]); files = sys.argv[4:]
imgs = []
for f in files:
    d = open(f, 'rb').read(); nl = d.index(b'\n'); w, h = map(int, d[:nl].split()); px = d[nl+1:]
    imgs.append((w, h, px))
cw = max(w for w, h, p in imgs) // scale; ch = max(h for w, h, p in imgs) // scale
rows = (len(imgs) + cols - 1) // cols; W = cols * cw; H = rows * ch
canvas = bytearray(W * H * 3)
for i, (w, h, p) in enumerate(imgs):
    ox, oy = (i % cols) * cw, (i // cols) * ch
    for y in range(h // scale):
        for x in range(w // scale):
            s = ((y * scale) * w + x * scale) * 4; t = ((oy + y) * W + ox + x) * 3
            canvas[t:t + 3] = p[s:s + 3]
raw = b''.join(b'\x00' + bytes(canvas[y * W * 3:(y + 1) * W * 3]) for y in range(H))
def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
open(out, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))
