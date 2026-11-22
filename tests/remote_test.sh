#!/bin/sh
# Runs an automated test on the console without anyone touching it, once the
# SW2PS2 test agent is waiting there (launch SW2PS2 once after the first upload).
#   tests/remote_test.sh '<runs JSON array>' [seconds] [slot] [game]
# Uploads the current build under an alternating name (the running one is
# locked), then asks the agent to restart into it, fetches and summarises log.txt.
# Set FTP_PORT=6700 when using SW2Transfer.
# NXLINK=1: start the agent once from hbmenu's NetLoader mode (Y).
# Later invocations reuse the waiting agent and require no console input.
# REUSE_RUNNING=1: run a new configuration in the currently running build.
set -e
cd "$(dirname "$0")/.."
IP=${SWITCH_IP:?set SWITCH_IP to the console IP address}
FTP_ROOT="ftp://$IP:${FTP_PORT:-5000}"
FTP="$FTP_ROOT/switch/sw2ps2"
RUNS=${1:-'[{"boost": true}]'}; SECONDS_EACH=${2:-14}; SLOT=${3:-1}; GAME=${4:-Katamari_Damacy.iso}
if [ -n "${NXLINK:-}" ] && [ -n "${REUSE_RUNNING:-}" ]; then
  echo "Choose either NXLINK or REUSE_RUNNING." >&2
  exit 1
fi
if [ -z "${NXLINK:-}" ]; then
  idle_marker=$(curl -fsS --max-time 8 "$FTP/autotest.finished" 2>/dev/null || true)
  case "$idle_marker" in
    done|done\ *) ;;
    *) echo "No idle test agent found. Open NetLoader once, then run with NXLINK=1." >&2; exit 1 ;;
  esac
  if [ -n "${REUSE_RUNNING:-}" ]; then
    case "$idle_marker" in
      done\ *reuse-log) ;;
      *) echo "The active test build does not support in-process test requests." >&2; exit 1 ;;
    esac
  fi
fi
# Test builds carry a red TEST icon and the title "SW2PS2 TEST", so they are never mistaken
# for the one to play (/switch/sw2ps2/sw2ps2.nro).
T=/opt/devkitpro/tools/bin
if [ -z "${REUSE_RUNNING:-}" ]; then
  VER=$(sed -n 's/^APP_VERSION[[:space:]]*:=[[:space:]]*//p' Makefile)
  "$T/nacptool" --create "SW2PS2 TEST" "Rodpad" "$VER" build/test.nacp >/dev/null
  "$T/elf2nro" "${ELF:-sw2ps2.elf}" build/sw2ps2-test.nro --icon=sw2ps2-test.jpg --nacp=build/test.nacp --romfsdir=romfs >/dev/null
fi
last=$(cat build/.remote-nro 2>/dev/null || echo b)
if [ "$last" = a ]; then name=b; else name=a; fi
NRO="sw2ps2-$name.nro"
if [ -n "${REUSE_RUNNING:-}" ]; then
  target='"reuse": true, '
elif [ -z "${NXLINK:-}" ]; then
  curl -fsS --max-time 120 -T build/sw2ps2-test.nro "$FTP/$NRO"
  target='"nro": "'"$NRO"'", '
else
  target=
fi
test_id="$(date +%Y%m%d%H%M%S)-$$"
printf '{"game": "%s", "slot": %s, "seconds": %s, "agent": true, "testId": "%s", %s%s"runs": %s}\n' "$GAME" "$SLOT" "$SECONDS_EACH" "$test_id" "$target" "${EXTRA:-}" "$RUNS" > build/autotest.json
n=$(python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))["runs"]))' build/autotest.json)
curl -fsS --max-time 8 -Q "DELE /switch/sw2ps2/autotest.finished" "$FTP_ROOT/" >/dev/null 2>&1 || true
curl -fsS --max-time 10 -T build/autotest.json "$FTP/autotest.upload"
curl -fsS --max-time 10 -Q "RNFR /switch/sw2ps2/autotest.upload" \
  -Q "RNTO /switch/sw2ps2/autotest.json" "$FTP_ROOT/" >/dev/null
if [ -n "${NXLINK:-}" ]; then
  if ! "$T/nxlink" -a "$IP" -r 2 build/sw2ps2-test.nro; then
    curl -fsS --max-time 8 -Q "DELE /switch/sw2ps2/autotest.json" "$FTP_ROOT/" >/dev/null 2>&1 || true
    exit 1
  fi
elif [ -z "${REUSE_RUNNING:-}" ]; then
  echo "$name" > build/.remote-nro
fi
limit=$(( n * (SECONDS_EACH + 25) + ${WAIT_EXTRA:-60} )); waited=0
while [ $waited -lt $limit ]; do
  sleep 5; waited=$((waited + 5))
  marker=$(curl -fsS --max-time 8 "$FTP/autotest.finished" 2>/dev/null || true)
  if [ "$marker" = "done $test_id" ] || [ "$marker" = "done $test_id reuse-log" ]; then
    log_name=log.txt
    if [ -n "${REUSE_RUNNING:-}" ]; then log_name="autotest-$test_id.log"; fi
    curl -fsS --max-time 60 "$FTP/$log_name" -o build/log.txt
    if [ -n "${REUSE_RUNNING:-}" ] && ! grep -Fxq "autotest: request $test_id" build/log.txt; then
      echo "The retrieved log belongs to a different test request." >&2
      exit 1
    fi
    cp build/log.txt "build/log-$(date +%H%M%S).txt"
    python3 tests/summarise.py build/log.txt
    if [ "$(grep -c '^autotest: run [0-9][0-9]* done' build/log.txt)" -ne "$n" ] ||
       grep -q 'measurement incomplete' build/log.txt; then
      echo "The test log contains an incomplete run." >&2
      exit 1
    fi
    exit 0
  fi
  case "$marker" in
    "cannot restart:"*|"cannot write test log $test_id") printf '%s\n' "$marker" >&2; exit 1 ;;
  esac
  if [ "$waited" -eq 30 ] && curl -fsS --max-time 8 "$FTP/autotest.json" -o /dev/null 2>/dev/null; then
    echo "The next build did not consume the test request after 30 seconds." >&2
    exit 1
  fi
done
echo "TIMEOUT after ${limit}s: the agent did not finish (is SW2PS2 waiting on the console?)"
curl -sS --max-time 20 "$FTP/log.txt" -o build/log.txt && tail -5 build/log.txt | cut -c1-160
exit 1
