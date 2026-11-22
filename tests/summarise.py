#!/usr/bin/env python3
"""Summarises an autotest log.txt: per run, frame rate, time per stage and thread CPU use."""
import re, sys
run = None; d = {}; order = []
for l in open(sys.argv[1], errors='replace'):
    m = re.match(r'autotest: run (\d+): .*?(\{.*\})', l)
    if m: run = m.group(1) + ' ' + m.group(2); d[run] = []; order.append(run)
    if l.startswith('autotest: measuring ') and run: d[run].clear()
    m = re.match(r'autotest: running \((.*)\)', l)
    if m and run: d[run + 'S'] = m.group(1)
    m = re.match(r't(\d+) (?:Game [\d.]+ fps\s+)?Emu ([\d.]+) fps.*wait ([\d.]+) block ([\d.]+) emu ([\d.]+) draw ([\d.]+) swap ([\d.]+)', l)
    if m and run and int(m.group(1)) >= 4: d[run].append([float(x) for x in m.groups()[1:]])
    m = re.match(r'\s+threads: (.*)', l)
    if m and run: d[run + 'T'] = m.group(1)
    m = re.match(r'autotest: run \d+ (measured .*|measurement incomplete)', l)
    if m and run: d[run + 'M'] = m.group(1)
    if l.startswith(('autotest: audio refills ', 'autotest: audio underruns ')) and run: d[run + 'A'] = l.removeprefix('autotest: ').strip()
for k in order:
    v = d[k]
    if not v: print(k, '| no per-second data |', d.get(k + 'M', d.get(k + 'S'))); continue
    avg = lambda i: sum(x[i] for x in v) / len(v)
    print('%s | %s | fps avg %.1f min %.1f | wait %.1f core %.1f draw %.1f swap %.1f ms' % (k, d.get(k + 'S', '?'), avg(0), min(x[0] for x in v), avg(1), avg(3), avg(4), avg(5)))
    print('      ', d.get(k + 'T', '')[:120])
    if k + 'M' in d: print('      ', d[k + 'M'])
    if k + 'A' in d: print('      ', d[k + 'A'])
