#!/bin/sh
# Fetches the emulator cores SW2PS2 builds on, at the exact commits it was developed against,
# and applies SW2PS2's changes (patches/). Run from the repository root.
set -e
cd "$(dirname "$0")/.."
while read -r dir url commit; do
  [ -n "$dir" ] || continue
  if [ ! -d "third_party/$dir/.git" ]; then
    git clone "$url" "third_party/$dir"
  fi
  git -C "third_party/$dir" checkout -q "$commit"
done < patches/upstreams.txt
git -C third_party/ouncesx2-eval/src apply ../../../patches/ouncesx2.patch
git -C third_party/armsx2-eval/src apply ../../../patches/armsx2.patch
echo "third_party ready"
