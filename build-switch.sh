#!/bin/sh
# Builds the LRPS2 core for the Switch 2 in a copy of the source tree
# (objects are built in-tree, so the Mac host build keeps its own).
set -e
cd "$(dirname "$0")"
mkdir -p build-switch
rsync -a --exclude='*.o' --exclude='*.d' --exclude='*.a' --exclude='*.dylib' --exclude='.build-tag' --exclude='.git' third_party/LRPS2/ build-switch/LRPS2/
export DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
cd build-switch/LRPS2
make -k -j10 platform=switch2 > ../build.log 2>&1 || true
grep -c ' error' ../build.log || true
