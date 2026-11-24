#!/usr/bin/env python3
"""Summarises an autotest log.txt: per run, frame rate, time per stage and thread CPU use."""
import re, sys
run = None; d = {}; order = []
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'autotest: run (\d+): .*?(\{.*\})', l)
    if m: run = m.group(1) + ' ' + m.group(2); d[run] = []; order.append(run)
    if l.startswith('autotest: measuring ') and run:
        d[run].clear()
        d.pop(run + 'F', None)
    m = re.match(r'autotest: running \((.*)\)', l)
    if m and run: d[run + 'S'] = m.group(1)
    m = re.match(r't(\d+) (?:Game ([\d.]+) fps\s+)?Emu ([\d.]+) fps.*wait ([\d.]+) block ([\d.]+) emu ([\d.]+) draw ([\d.]+) swap ([\d.]+)', l)
    if m and run and int(m.group(1)) >= 4:
        d[run].append([float(x) for x in m.groups()[2:]])
        if m.group(2) is not None: d.setdefault(run + 'F', []).append(float(m.group(2)))
    m = re.match(r'\s+threads: (.*)', l)
    if m and run: d[run + 'T'] = m.group(1)
    m = re.match(r'autotest: run \d+ (measured .*|measurement incomplete)', l)
    if m and run: d[run + 'M'] = m.group(1)
    if l.startswith('autotest: frame lows:') and run: d[run + 'L'] = l.removeprefix('autotest: frame lows:').strip()
    if l.startswith(('autotest: audio refills ', 'autotest: audio underruns ')) and run: d[run + 'A'] = l.removeprefix('autotest: ').strip()
    if l.startswith('autotest: Vulkan submits ') and run: d[run + 'V'] = l.removeprefix('autotest: ').strip()
    if l.startswith('autotest: handoff copies ') and run: d[run + 'H'] = l.removeprefix('autotest: ').strip()
    if l.startswith('autotest: GS pipeline lookups ') and run: d[run + 'G'] = l.removeprefix('autotest: ').strip()
for k in order:
    v = d[k]
    if not v: print(k, '| no per-second data |', d.get(k + 'M', d.get(k + 'S'))); continue
    avg = lambda i: sum(x[i] for x in v) / len(v)
    print('%s | %s | emu fps avg %.1f min %.1f | wait %.1f core %.1f draw %.1f swap %.1f ms' % (k, d.get(k + 'S', '?'), avg(0), min(x[0] for x in v), avg(1), avg(3), avg(4), avg(5)))
    if k + 'F' in d: print('       game fps avg %.1f min %.1f' % (sum(d[k + 'F']) / len(d[k + 'F']), min(d[k + 'F'])))
    print('      ', d.get(k + 'T', '')[:120])
    if k + 'M' in d: print('      ', d[k + 'M'])
    if k + 'L' in d: print('      ', d[k + 'L'])
    if k + 'A' in d: print('      ', d[k + 'A'])
    if k + 'V' in d: print('      ', d[k + 'V'])
    if k + 'H' in d: print('      ', d[k + 'H'])
    if k + 'G' in d: print('      ', d[k + 'G'])
