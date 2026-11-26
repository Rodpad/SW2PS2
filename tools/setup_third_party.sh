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
# The OunceVK driver release the app links (Makefile OUNCEVK), unpacked from its pacman package.
OVK=20261117.1; OVK_SHA=061dd6a172255e272c5258578745533db043aff05e8b321d8a86850fd702ffeb  # not 20261118+: they crash on exit to hbmenu (see Makefile)
if [ ! -d "third_party/ouncevk-$OVK/lib" ]; then
  tmp=$(mktemp -d)
  curl -fsSL -o "$tmp/ovk.pkg.tar.zst" "https://github.com/ouncebrew/ouncevk/releases/download/$OVK/ouncevk-$OVK-1-any.pkg.tar.zst"
  echo "$OVK_SHA  $tmp/ovk.pkg.tar.zst" | shasum -a 256 -c -
  mkdir -p "$tmp/x" "third_party/ouncevk-$OVK" && tar -xf "$tmp/ovk.pkg.tar.zst" -C "$tmp/x"
  cp -R "$tmp/x/opt/devkitpro/portlibs/switch2/." "third_party/ouncevk-$OVK/"
  rm -rf "$tmp"
fi
git -C third_party/armsx2-eval/src apply ../../../patches/armsx2.patch
echo "third_party ready"
