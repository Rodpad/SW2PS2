#!/bin/sh
# Build and run one unattended iteration using the already-bootstrapped test agent.
# Usage: tests/iterate.sh --switch IP --port 6700 [--no-core] [--bootstrap] RUNS.json [seconds] [slot] [game]
set -eu
cd "$(dirname "$0")/.."
core=1; bootstrap=0; ip=${SWITCH_IP:-}; port=${FTP_PORT:-6700}
while [ "$#" -gt 0 ]; do
  case "$1" in
    --switch) ip=$2; shift 2 ;;
    --port) port=$2; shift 2 ;;
    --core) core=1; shift ;;
    --no-core) core=0; shift ;;
    --bootstrap) bootstrap=1; shift ;;
    --) shift; break ;;
    *) break ;;
  esac
done
: "${ip:?pass --switch or set SWITCH_IP}"
runs=${1:?pass a JSON runs file}; seconds=${2:-15}; slot=${3:-1}; game=${4:-Tekken Tag Tournament (USA) (v2.00).cue}
python3 -c 'import json,sys; a=json.load(open(sys.argv[1])); assert isinstance(a,list) and a, "runs must be a nonempty JSON array"' "$runs"
id=$(date +%Y%m%d-%H%M%S)-$$
out="build/perf/iterations/$id"
mkdir -p "$out"
cp "$runs" "$out/runs.json"
git diff --binary > "$out/workspace.patch"
printf 'ip=%s\nport=%s\nseconds=%s\nslot=%s\ngame=%s\n' "$ip" "$port" "$seconds" "$slot" "$game" > "$out/request.txt"
# Force a unique, runtime-visible identity even when only the core changed.
printf '#define SW2_ITERATION_ID "%s"\n' "$id" > build/iteration_build_id.h
if [ "$core" -eq 1 ]; then
  if ! ninja -C third_party/ouncesx2-eval/build-sw2-vk PCSX2 sw2-armsx2-libretro > "$out/core-build.log" 2>&1; then
    cat "$out/core-build.log" >&2; exit 1
  fi
fi
if ! DEVKITPRO=${DEVKITPRO:-/opt/devkitpro} DEVKITA64=${DEVKITA64:-/opt/devkitpro/devkitA64} make -j4 > "$out/build.log" 2>&1; then
  cat "$out/build.log" >&2; exit 1
fi
shasum -a 256 sw2ps2.elf > "$out/elf.sha256"
if [ "$bootstrap" -eq 1 ]; then
  set -- --bootstrap
else
  set --
fi
if ! tests/remote_test.sh --switch "$ip" --port "$port" "$@" --runs-file "$runs" "$seconds" "$slot" "$game" > "$out/summary.log" 2>&1; then
  cat "$out/summary.log" >&2
  echo "Iteration artifacts: $out" >&2
  exit 1
fi
cp build/log.txt "$out/console.log"
if ! grep -Fq "iteration $id;" "$out/console.log"; then
  echo "The console did not report the expected build ID $id; results are invalid." >&2
  echo "Iteration artifacts: $out" >&2
  exit 1
fi
shasum -a 256 build/sw2ps2-test.nro > "$out/nro.sha256"
cat "$out/summary.log"
echo "Iteration artifacts: $out"
