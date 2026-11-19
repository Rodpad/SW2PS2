#!/usr/bin/env python3
"""eeprofile.py sample.txt : self time per function on the EE thread (cpu_thread_entry) of a macOS `sample` report.
JIT code (no symbol) is grouped as [JIT]."""
import re, sys, collections
lines = open(sys.argv[1]).read().split('\n')
starts = [i for i, l in enumerate(lines) if re.match(r'\s+\d+ Thread_', l)]
end = [i for i, l in enumerate(lines) if l.startswith('Total number in stack')]; end = end[0] if end else len(lines); starts.append(end)
pat = re.compile(r'^(\s*[+!:| ]*)(\d+) (.*)$')
for a, b in zip(starts, starts[1:]):
    blk = lines[a:b]
    if not any('cpu_thread_entry' in l for l in blk[:15]): continue
    nodes = []
    for l in blk[1:]:
        m = pat.match(l)
        if not m: continue
        name = m.group(3)
        name = '[JIT]' if name.startswith('???') else re.sub(r'\s+\(in .*$', '', name)
        nodes.append((len(m.group(1)), int(m.group(2)), name))
    selfc = collections.Counter()
    for i, (d, c, n) in enumerate(nodes):
        kids = 0
        for d2, c2, n2 in nodes[i + 1:]:
            if d2 <= d: break
            if d2 == min(x[0] for x in nodes[i + 1:] if x[0] > d) if any(x[0] > d for x in nodes[i+1:i+2]) else False: pass
        # children: the immediately deeper lines until depth returns to <= d
        j = i + 1; cd = None
        while j < len(nodes) and nodes[j][0] > d:
            if cd is None: cd = nodes[j][0]
            if nodes[j][0] == cd: kids += nodes[j][1]
            j += 1
        selfc[n] += c - kids
    total = sum(v for k, v in selfc.items() if 'wait' not in k and 'trap' not in k)
    waits = sum(v for k, v in selfc.items() if 'wait' in k or 'trap' in k)
    print('EE thread busy samples %d, waiting %d' % (total, waits))
    for n, v in selfc.most_common(40):
        if 'wait' in n or 'trap' in n: continue
        print('%5.1f%% %s' % (v * 100 / total, n[:90]))
    break
