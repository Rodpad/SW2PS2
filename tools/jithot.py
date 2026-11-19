#!/usr/bin/env python3
"""jithot.py sample.txt prefix : hottest JIT blocks on the EE thread (self samples), from a macOS
`sample` report and a SW2_JIT_DUMP block log. Prints guest pc, host range and sample share."""
import re, sys, bisect, collections
sample, pre = sys.argv[1], sys.argv[2]
blocks = {}
for l in open(pre + '.blocks'):
    a = l.split()
    if a[0] == 'base': continue
    blocks[int(a[1], 16)] = int(a[0], 16)
starts = sorted(blocks)
lines = open(sample).read().split('\n')
ths = [i for i, l in enumerate(lines) if re.match(r'\s+\d+ Thread_', l)]
end = [i for i, l in enumerate(lines) if l.startswith('Total number in stack')]; end = end[0] if end else len(lines); ths.append(end)
pat = re.compile(r'^(\s*[+!:| ]*)(\d+) (.*)$')
for a, b in zip(ths, ths[1:]):
    blk = lines[a:b]
    if not any('cpu_thread_entry' in l for l in blk[:15]): continue
    nodes = []
    for l in blk[1:]:
        m = pat.match(l)
        if m: nodes.append((len(m.group(1)), int(m.group(2)), m.group(3)))
    per = collections.Counter(); busy = 0
    for i, (d, c, n) in enumerate(nodes):
        j = i + 1; cd = None; kids = 0
        while j < len(nodes) and nodes[j][0] > d:
            if cd is None: cd = nodes[j][0]
            if nodes[j][0] == cd: kids += nodes[j][1]
            j += 1
        s = c - kids
        if 'wait' in n or 'trap' in n: continue
        busy += s
        m = re.search(r'\[0x([0-9a-f]+)\]', n)
        if n.startswith('???') and m:
            addr = int(m.group(1), 16); k = bisect.bisect_right(starts, addr) - 1
            if k >= 0: per[starts[k]] += s
    tot_jit = sum(per.values())
    print('busy %d samples, JIT %d (%.0f%%)' % (busy, tot_jit, tot_jit * 100 / busy))
    for fn, s in per.most_common(int(sys.argv[3]) if len(sys.argv) > 3 else 15):
        k = starts.index(fn); nxt = starts[k + 1] if k + 1 < len(starts) else fn + 4096
        print('%5.1f%%  pc %08x  host %x-%x (%d bytes)' % (s * 100 / busy, blocks[fn], fn, nxt, nxt - fn))
    break
