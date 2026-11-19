#!/bin/sh
# Runs an automated test on the console without anyone touching it, once the
# SW2PS2 test agent is waiting there (launch SW2PS2 once after the first upload).
#   tests/remote_test.sh '<runs JSON array>' [seconds] [slot] [game]
# Uploads the current build under an alternating name (the running one is
# locked), uploads autotest.json naming it, waits for the agent to restart into
# it and finish, then fetches and summarises log.txt.
# NXLINK=1: no agent needed -- hbmenu must be in NetLoader mode (Y); the build
# is sent and started with nxlink instead of being handed over to.
set -e
cd "$(dirname "$0")/.."
IP=${SWITCH_IP:?set SWITCH_IP to the console's IP address (its FTP server on port 5000)}; FTP="ftp://$IP:5000/switch/sw2ps2"
RUNS=${1:-'[{"boost": true}]'}; SECONDS_EACH=${2:-14}; SLOT=${3:-1}; GAME=${4:-Katamari_Damacy.iso}
# Test builds carry a red TEST icon and the title "SW2PS2 TEST", so they are never mistaken
# for the one to play (/switch/sw2ps2/sw2ps2.nro).
T=/opt/devkitpro/tools/bin
VER=$(sed -n 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//p' Makefile)
"$T/nacptool" --create "SW2PS2 TEST" "Rodpad" "$VER" build/test.nacp >/dev/null
"$T/elf2nro" "${ELF:-sw2ps2.elf}" build/sw2ps2-test.nro --icon=sw2ps2-test.jpg --nacp=build/test.nacp --romfsdir=romfs >/dev/null
last=$(cat build/.remote-nro 2>/dev/null || echo b)
if [ "$last" = a ]; then name=b; else name=a; fi
echo "$name" > build/.remote-nro
NRO="sw2ps2-$name.nro"
if [ -z "${NXLINK:-}" ]; then curl -sS -T build/sw2ps2-test.nro "$FTP/$NRO"; fi
printf '{"game": "%s", "slot": %s, "seconds": %s, "agent": true, "nro": "%s", %s"runs": %s}\n' "$GAME" "$SLOT" "$SECONDS_EACH" "$NRO" "${EXTRA:-}" "$RUNS" > build/autotest.json
curl -sS -Q "DELE /switch/sw2ps2/autotest.finished" "ftp://$IP:5000/" >/dev/null 2>&1 || true
if [ -n "${NXLINK:-}" ]; then
  sed -i '' 's/"nro": "[^"]*", //' build/autotest.json
  curl -sS -T build/autotest.json "$FTP/autotest.json"
  for try in $(seq 1 360); do "$T/nxlink" -a "$IP" build/sw2ps2-test.nro >/dev/null 2>&1 && break; sleep 10; done  # until NetLoader is open
else
  curl -sS -T build/autotest.json "$FTP/autotest.json"
fi
n=$(printf '%s' "$RUNS" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))')
limit=$(( n * (SECONDS_EACH + 25) + ${WAIT_EXTRA:-60} )); waited=0
while [ $waited -lt $limit ]; do
  sleep 5; waited=$((waited + 5))
  if curl -sS --max-time 8 "$FTP/" 2>/dev/null | grep -q 'autotest.finished'; then
    curl -sS --max-time 60 "$FTP/log.txt" -o build/log.txt
    cp build/log.txt "build/log-$(date +%H%M%S).txt"
    python3 tests/summarise.py build/log.txt
    exit 0
  fi
done
echo "TIMEOUT after ${limit}s: the agent did not finish (is SW2PS2 waiting on the console?)"
curl -sS --max-time 20 "$FTP/log.txt" -o build/log.txt && tail -5 build/log.txt | cut -c1-160
exit 1
