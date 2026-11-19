#!/usr/bin/env python3
"""hostpcs.py log.txt sw2ps2.elf : resolve the console profiler's host pc offsets to functions."""
import re, sys, subprocess, bisect, collections
log, elf = sys.argv[1], sys.argv[2]
line = [l for l in open(log, errors='replace') if l.startswith('host pcs')][-1]
m = re.match(r'host pcs \((\d+) samples, outside the image (\d+)%\)', line)
total, outside = int(m.group(1)), int(m.group(2))
pairs = [(int(a, 16), int(c)) for a, c in re.findall(r' ([0-9a-f]+):(\d+)', line.split('):', 1)[1])]
nm = subprocess.run(['/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm', '-C', '--defined-only', elf], capture_output=True, text=True).stdout
syms = []
for l in nm.split('\n'):
    p = l.split(' ', 2)
    if len(p) == 3 and p[1].lower() in ('t', 'w'): syms.append((int(p[0], 16), p[2]))
syms.sort(); addrs = [s[0] for s in syms]
anchor = [a for a, n in syms if n.startswith('profMain')][0]
base = anchor - 0x10000000  # offsets are logged as pc - runtime(profMain) + 0x10000000
agg = collections.Counter()
for a, c in pairs:
    k = bisect.bisect_right(addrs, a + base) - 1
    agg[syms[k][1] if k >= 0 else '?'] += c
print('%d samples; JIT/other (outside the image) %d%%' % (total, outside))
for n, c in agg.most_common(45): print('%5.1f%% %s' % (c * 100 / total, n[:100]))
