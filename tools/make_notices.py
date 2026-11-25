#!/usr/bin/env python3
"""Write romfs/licenses/THIRD_PARTY_NOTICES.txt: the licence of every third-party library linked
into SW2PS2, as their licences ask when the program is distributed. It ships inside the .nro.

Texts come from the library sources (third_party/, after tools/setup_third_party.sh and the core
build) and from the devkit (/opt/devkitpro). Run from the repository root after changing what
the app links (Makefile LIBS / AXLIBS) and commit the result.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OS = os.path.join(ROOT, "third_party/ouncesx2-eval/src")
AND = os.path.join(OS, "platforms/android/app/src/main/cpp/3rdparty")
DKP = os.environ.get("DEVKITPRO", "/opt/devkitpro")
OUT = os.path.join(ROOT, "romfs/licenses/THIRD_PARTY_NOTICES.txt")

def f(p): return ("file", p)
def head(p, stop): return ("head", p, stop)  # a source file's leading comment, up to the line matching stop
def text(s): return ("text", s)

# (component, what it does in SW2PS2, licence, texts)
COMPONENTS = [
    ("OunceSX2 / ARMSX2 / PCSX2", "the PS2 emulator core", "GPL-3.0-or-later", [text("The GNU GPL v3: GPL-3.0.txt in this folder (LICENSE in the SW2PS2 source). Source: https://github.com/Rodpad/SW2PS2 with patches/ against https://github.com/nyathea/OunceSX2.")]),
    ("OunceVK (Mesa NVK + Zink)", "Vulkan / OpenGL ES graphics driver", "MIT (Mesa)", [f(os.path.join(ROOT, "third_party/ouncevk-20261123.1/share/licenses/ouncevk/mesa-license.rst")), f(os.path.join(ROOT, "third_party/ouncevk-20261123.1/share/licenses/ouncevk/zlib-license.txt"))]),
    ("kaiHEN", "JIT and system services on the Switch 2", "ISC", [f(f"{DKP}/portlibs/switch2/share/licenses/kaihen/kaihen-ISC.txt")]),
    ("libounce / libnx", "Switch 2 system library", "ISC", [f(f"{DKP}/libounce/licenses/libounce-ISC.txt"), f(f"{DKP}/libounce/licenses/libnx-ISC.txt")]),
    ("FreeType", "font rendering", "FreeType Licence (FTL)", [f(f"{DKP}/portlibs/switch/licenses/switch-freetype/LICENSE.TXT"), f(f"{DKP}/portlibs/switch/licenses/switch-freetype/FTL.TXT")]),
    ("HarfBuzz 10.0.1", "text shaping (with FreeType)", "Old MIT", [f(os.path.join(ROOT, "tools/licenses/harfbuzz-COPYING.txt"))]),
    ("libpng", "PNG screenshots and images", "libpng licence", [f(f"{DKP}/portlibs/switch/licenses/switch-libpng/LICENSE")]),
    ("zlib", "compression", "zlib", [f(f"{DKP}/portlibs/switch/licenses/switch-zlib/LICENSE")]),
    ("minizip", "zip reading", "zlib", [head(f"{DKP}/portlibs/switch/include/minizip/unzip.h", r"^\*/|^\s*\*/")]),
    ("bzip2", "compression", "bzip2 licence (BSD-style)", [f(f"{DKP}/portlibs/switch/licenses/switch-bzip2/LICENSE")]),
    ("libjpeg-turbo", "JPEG images", "IJG / BSD-3-Clause / zlib", [f(os.path.join(ROOT, "third_party/ouncesx2-eval/deps-sw2/share/doc/libjpeg-turbo/LICENSE.md")), f(os.path.join(ROOT, "third_party/ouncesx2-eval/deps-sw2/share/doc/libjpeg-turbo/README.ijg"))]),
    ("SDL3", "audio and input plumbing inside the core", "zlib", [f(f"{AND}/sdl3/LICENSE.txt")]),
    ("libwebp", "WebP images", "BSD-3-Clause", [f(f"{AND}/libwebp/COPYING")]),
    ("LZ4", "compression", "BSD-2-Clause", [f(f"{AND}/lz4/LICENSE")]),
    ("Zstandard", "compression", "BSD-3-Clause", [f(f"{AND}/zstd/LICENSE")]),
    ("glslang", "shader compiler (Vulkan renderer)", "BSD-3-Clause and others (see text)", [f(f"{OS}/3rdparty/glslang/glslang/LICENSE.txt")]),
    ("glad", "OpenGL loader (generated)", "(WTFPL OR CC0-1.0) AND Apache-2.0", [head(f"{OS}/3rdparty/glad/include/glad/gl.h", r"^\s*\*/")]),
    ("VIXL", "ARM64 code generator for the recompilers", "BSD-3-Clause", [f(f"{OS}/3rdparty/vixl/LICENCE")]),
    ("cpuinfo", "CPU detection", "BSD-2-Clause", [f(f"{OS}/3rdparty/cpuinfo/LICENSE"), f(f"{OS}/3rdparty/cpuinfo/deps/clog/LICENSE")]),
    ("cubeb", "audio output", "ISC", [f(f"{OS}/3rdparty/cubeb/LICENSE")]),
    ("{fmt}", "text formatting", "MIT", [f(f"{OS}/3rdparty/fmt/LICENSE")]),
    ("Dear ImGui", "core on-screen display", "MIT", [f(f"{OS}/3rdparty/imgui/LICENSE.txt")]),
    ("libchdr", "CHD disc images", "BSD-3-Clause", [f(f"{OS}/3rdparty/libchdr/LICENSE.txt")]),
    ("libzip", "zip archives", "BSD-3-Clause", [f(f"{OS}/3rdparty/libzip/LICENSE")]),
    ("LZMA SDK", "7z / xz decompression", "Public domain", [f(f"{OS}/3rdparty/lzma/LICENSE.txt")]),
    ("PlutoSVG / PlutoVG", "SVG icons in the core", "MIT", [f(f"{OS}/3rdparty/plutosvg/LICENSE"), f(f"{OS}/3rdparty/plutovg/LICENSE")]),
    ("rcheevos", "achievements library (part of the core)", "MIT", [f(f"{OS}/3rdparty/rcheevos/LICENSE")]),
    ("SimpleIni", "settings files", "MIT", [f(f"{OS}/3rdparty/simpleini/LICENCE.txt")]),
    ("SoundTouch", "audio time-stretching", "LGPL-2.1-or-later", [f(f"{OS}/3rdparty/soundtouch/COPYING.TXT")]),
    ("FreeSurround", "surround audio decoding", "GPL-2.0-or-later", [head(f"{OS}/3rdparty/freesurround/include/FreeSurroundDecoder.h", r"^#pragma"), f(f"{OS}/3rdparty/freesurround/GPL.txt")]),
    ("GNU demangler (libiberty)", "symbol names in diagnostics", "LGPL-2.1-or-later / GPL", [f(f"{OS}/3rdparty/demangler/DemanglerLicenseLGPL.txt")]),
    ("ccc (chaoticgd)", "PS2 symbol table parsing", "MIT", [f(os.path.join(ROOT, "tools/licenses/ccc-LICENSE.txt"))]),
    ("rapidyaml", "game database parsing", "MIT", [f(os.path.join(ROOT, "tools/licenses/rapidyaml-LICENSE.txt"))]),
    ("libretro API header", "core interface", "MIT", [head(f"{OS}/3rdparty/libretro/libretro.h", r"^\s*\*/")]),
    ("GCC runtime (libstdc++, libgcc) and newlib", "C / C++ runtime", "GPL-3.0 with the GCC Runtime Library Exception; newlib: BSD-style", [text("libstdc++ and libgcc are covered by the GCC Runtime Library Exception, which places no conditions on the compiled program. newlib's C library is under the licences listed in its COPYING.NEWLIB (BSD-style).")]),
    ("Fonts: Roboto, Source Code Pro", "the app's text", "Apache-2.0 / OFL-1.1", [text("See roboto-Apache-2.0.txt and source-code-pro-OFL-1.1.txt in this folder.")]),
    ("Per-game CPU settings", "the built-in speed list", "CC BY 4.0", [text("Adapted (converted to SW2PS2's own table) from the AetherSX2/NetherSX2 playable lists by Darkelf2024, https://github.com/Darkelf2024/PS2-Playable-Lists-for-AetherSX2-NetherSX2, licensed CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/).")]),
]

def read(spec):
    kind = spec[0]
    if kind == "text": return spec[1]
    path = spec[1]
    if not os.path.exists(path):
        sys.exit(f"missing: {path} (run tools/setup_third_party.sh and build the core first)")
    data = open(path, encoding="utf-8", errors="replace").read()
    if kind == "file": return data.strip()
    lines = []
    for line in data.splitlines():
        lines.append(line)
        if len(lines) > 1 and re.match(spec[2], line): break
    return "\n".join(lines).strip()

out = ["SW2PS2 - third-party notices",
       "",
       "SW2PS2 is free software under the GNU General Public License v3 or later. It includes the",
       "components below, each under its own licence; their notices follow. Full source:",
       "https://github.com/Rodpad/SW2PS2",
       ""]
for name, use, lic, _ in COMPONENTS: out.append(f"  - {name}: {use} ({lic})")
for name, use, lic, specs in COMPONENTS:
    out += ["", "=" * 78, f"{name} - {lic}", "=" * 78, ""]
    out += [read(s) + "\n" for s in specs]
os.makedirs(os.path.dirname(OUT), exist_ok=True)
open(OUT, "w", encoding="utf-8").write("\n".join(out).rstrip() + "\n")
print(OUT, sum(1 for _ in open(OUT, encoding="utf-8")), "lines")
