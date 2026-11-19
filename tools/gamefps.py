#!/usr/bin/env python3
"""gamefps.py log.txt : per test run, the game's own frame rate (display-buffer flips) and the emulation rate."""
import re, sys
log = open(sys.argv[1], errors='replace').read()
parts = re.split(r'autotest: run (\d+): ', log)[1:]
for i in range(0, len(parts), 2):
    n, body = parts[i], parts[i + 1]
    name = re.match(r"game '([^']*)'", body); cfg = re.search(r'\d+ s, (\{.*\})', body.split('\n')[0])
    g = [float(x) for x in re.findall(r'^t\d+ Game (\d+) fps', body, re.M)][3:]
    e = [float(x) for x in re.findall(r'^t\d+ Game \d+ fps   Emu ([\d.]+)', body, re.M)][3:]
    st = re.search(r'EE (\d+%).*cycle skip (\w+)', body)
    if g: print('%s %-34s game %4.1f fps   emu %4.1f fps   EE %s skip %s   %s' % (n, name.group(1)[:34] if name else '?', sum(g) / len(g), sum(e) / len(e), st.group(1) if st else '?', st.group(2) if st else '?', cfg.group(1) if cfg else ''))
