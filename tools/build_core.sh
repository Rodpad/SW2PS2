#!/bin/sh
# Builds the OunceSX2 core for SW2PS2 exactly as the released app was built: libjpeg-turbo
# (from the core's own vendored copy) into third_party/ouncesx2-eval/deps-sw2, then the core in
# third_party/ouncesx2-eval/build-sw2-vk. Run tools/setup_third_party.sh first. Needs devkitPro
# with ounce-devkit (DEVKITPRO, default /opt/devkitpro), CMake and Ninja.
# Afterwards `make` in the repository root builds the app.
set -e
cd "$(dirname "$0")/.."
DKP=${DEVKITPRO:-/opt/devkitpro}
OX=$PWD/third_party/ouncesx2-eval
SRC=$OX/src
DEPS=$OX/deps-sw2
TC=$DKP/cmake/Switch.cmake

# 1. libjpeg-turbo (static, no SIMD) -> deps-sw2
JFLAGS="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -ffunction-sections -fdata-sections -D__SWITCH__"
cmake -G Ninja -S "$SRC/platforms/android/app/src/main/cpp/3rdparty/libjpeg-turbo" -B "$OX/build-jpeg-sw2" \
  -DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="$JFLAGS" \
  -DCMAKE_INSTALL_PREFIX="$DEPS" -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_SIMD=OFF -DREQUIRE_SIMD=OFF \
  -DWITH_JPEG8=OFF -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=ON
ninja -C "$OX/build-jpeg-sw2" install

# 2. The core: libretro wrapper with the Vulkan (and GL ES) renderer, core only, LTO
CFLAGS_SW2="-isystem $DKP/libounce/include -isystem $DKP/portlibs/switch2/include -mcpu=cortex-a78c+crypto -mtp=soft -ftls-model=local-exec -ffunction-sections -fdata-sections -D__SWITCH__ -fPIE"
cmake -G Ninja -S "$SRC" -B "$OX/build-sw2-vk" \
  -DCMAKE_TOOLCHAIN_FILE="$TC" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$CFLAGS_SW2" -DCMAKE_CXX_FLAGS="$CFLAGS_SW2" \
  -DSW2_CORE_ONLY=ON -DENABLE_LIBRETRO=ON -DUSE_VULKAN=ON -DUSE_OPENGL=ON -DUSE_GLES=ON -DLTO_PCSX2_CORE=ON \
  -DENABLE_QT_UI=OFF -DENABLE_SDL_FRONTEND=OFF -DENABLE_TESTS=OFF -DENABLE_RECOMPILER_TEST_HOOKS=OFF \
  -DENABLE_EERUNNER=OFF -DENABLE_GSRUNNER=OFF -DENABLE_VURUNNER=OFF -DENABLE_GS_DEBUG_LABELS=ON \
  -DARMSX2_EMBED_PATCHES=ON -DARMSX2_EMBED_RESOURCES=ON -DUSE_SYSTEM_LIBS=OFF \
  -DJPEG_INCLUDE_DIR="$DEPS/include" -DJPEG_LIBRARY="$DEPS/lib/libjpeg.a"
ninja -C "$OX/build-sw2-vk" PCSX2 sw2-armsx2-libretro
echo "core built: $OX/build-sw2-vk (now run make)"
