#---------------------------------------------------------------------------------
# SW2PS2 for hbmenu (Switch 2 homebrew, kaiHEN): devkitPro + ounce-devkit.
#   ./build-switch.sh            (the LRPS2 core -> build-switch/LRPS2/*.a)
#   export DEVKITPRO=/opt/devkitpro && make
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libounce/switch2_rules
# OUNCEVK: a newer OunceVK driver (lib/, include/) used ahead of the portlibs copy; make OUNCEVK= uses the portlibs one.
OUNCEVK	?=	$(TOPDIR)/third_party/ouncevk-20261118
ifneq ($(strip $(OUNCEVK)),)
OVERLAY_LIBPATHS	:=	-L$(OUNCEVK)/lib $(OVERLAY_LIBPATHS)
CPPFLAGS	:=	-I$(OUNCEVK)/include $(CPPFLAGS)
endif

APP_TITLE	:=	SW2PS2
APP_AUTHOR	:=	Rodpad
APP_VERSION	:=	0.1.0

TARGET		:=	sw2ps2
BUILD		:=	build
CORE_BUILD	?=	build-switch
CORE		=	$(CORE_BUILD)/LRPS2
SOURCES		:=	source native
INCLUDES	:=	source native third_party/LRPS2/libretro/libretro-common/include
ROMFS		:=	romfs
ICON		:=	sw2ps2.jpg


ARCH	:=	$(SWITCH2_CPU) -mtp=soft -fPIE
CFLAGS	:=	-g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES) -Wno-unused-function
CFLAGS	+=	$(INCLUDE) -D__SWITCH__ -DNDEBUG -D__LIBRETRO__ -DAPP_VERSION=\"$(APP_VERSION)\" -I$(PORTLIBS)/include/freetype2
# Pre-release expiry: the build's own time, stamped into buildstamp.o (rebuilt every build, below).
SW2_BUILD_TIME	?=	$(shell date +%s)
export SW2_BUILD_TIME
CFLAGS	+=	-DSW2_BUILD_TIME=$(SW2_BUILD_TIME)LL
CXXFLAGS	:= $(CFLAGS) -std=gnu++17
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map) -Wl,--gc-sections
# CORE_KIND=armsx2 builds on the ARMSX2/OunceSX2 core (third_party/ouncesx2-eval/build-sw2) instead of LRPS2.
CORE_KIND	?=	lrps2
ifeq ($(CORE_KIND),armsx2)
CXXFLAGS	+=	-DSW2_ARMSX2 -DENABLE_LIBRETRO -DENABLE_OPENGL -DUSE_GLES -I$(TOPDIR)/third_party/ouncesx2-eval/src -I$(TOPDIR)/third_party/ouncesx2-eval/src/common/include -I$(TOPDIR)/third_party/ouncesx2-eval/src/3rdparty/fmt/include -I$(TOPDIR)/third_party/ouncesx2-eval/src/switch/app -I$(TOPDIR)/third_party/ouncesx2-eval/src/pcsx2 -I$(TOPDIR)/third_party/ouncesx2-eval/src/3rdparty/glad/include -I$(TOPDIR)/third_party/ouncesx2-eval/src/3rdparty/fast_float/include -std=gnu++20
CFLAGS	+=	-DSW2_ARMSX2
# ARMSX2_VK=1: the core built with its Vulkan renderer too (build-sw2-vk), and the front end's
# Vulkan display (source/vkpresent.cpp) for the "GPU (Vulkan)" renderer. ES front end.
ARMSX2_VK	?=	0
ifeq ($(ARMSX2_VK),1)
AX		:=	$(TOPDIR)/third_party/ouncesx2-eval/build-sw2-vk
CXXFLAGS	+=	-DSW2_VK -DENABLE_VULKAN
CFLAGS	+=	-DSW2_VK
ARMSX2_GL	:=	es
else
AX		:=	$(TOPDIR)/third_party/ouncesx2-eval/build-sw2
endif
# ARMSX2_GL=es: the OpenGL ES build (no texture barriers on the Switch 2's Mesa ES: accurate blending
# falls back to per-draw copies, which crash). Default: desktop OpenGL 4.6.
ARMSX2_GL	?=	desktop
ifeq ($(ARMSX2_GL),es)
AXWRAP	:=	$(AX)/libsw2-armsx2-libretro.a
else
AXWRAP	:=	$(AX)/libsw2-armsx2-libretro-gl.a
CXXFLAGS	+=	-DSW2_GL_DESKTOP
CFLAGS	+=	-DSW2_GL_DESKTOP
endif
AXLIBS	:=	$(AXWRAP) $(AX)/pcsx2/libPCSX2_LTO.a $(AX)/common/libcommon.a \
		$(AX)/3rdparty/ccc/libccc.a $(AX)/3rdparty/cpuinfo/libcpuinfo.a $(AX)/3rdparty/cubeb/libcubeb.a \
		$(AX)/3rdparty/demangler/libdemanglegnu.a $(AX)/3rdparty/fmt/libfmt.a $(AX)/3rdparty/freesurround/libfreesurround.a \
		$(AX)/3rdparty/imgui/libimgui.a $(AX)/3rdparty/libchdr/liblibchdr.a $(AX)/3rdparty/libzip/libzip.a \
		$(AX)/3rdparty/lzma/libpcsx2-lzma.a $(AX)/3rdparty/plutosvg/libplutosvg.a $(AX)/3rdparty/plutovg/libplutovg.a \
		$(AX)/3rdparty/rapidyaml/libpcsx2-rapidyaml.a $(AX)/3rdparty/rcheevos/librcheevos.a $(AX)/3rdparty/simpleini/libsimpleini.a \
		$(AX)/3rdparty/soundtouch/libpcsx2-soundtouch.a $(AX)/3rdparty/vixl/libvixl.a \
		$(AX)/horizon-deps/libwebp/libwebp.a $(AX)/horizon-deps/libwebp/libsharpyuv.a $(AX)/horizon-deps/lz4/liblz4.a \
		$(AX)/horizon-deps/sdl3/libSDL3.a $(AX)/3rdparty/glad/libglad.a $(if $(filter 1,$(ARMSX2_VK)),$(AX)/3rdparty/glslang/libglslang.a) $(AX)/liblibzstd_static.a $(TOPDIR)/third_party/ouncesx2-eval/deps-sw2/lib/libjpeg.a
LIBS	:= -Wl,--wrap=pthread_create -Wl,--wrap=pthread_join -Wl,--wrap=smGetServiceWrapper -Wl,--wrap=smGetServiceOriginal -Wl,--start-group $(AXLIBS) -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lkaihen -lfreetype -lharfbuzz -lpng16 -lbz2 -lminizip -lz -lnx -Wl,--end-group
else
LIBS	:= $(TOPDIR)/$(CORE)/pcsx2_libretro_switch2.a -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lkaihen -lfreetype -lharfbuzz -lpng16 -lbz2 -lminizip -lz -lnx
endif
LIBDIRS	:= $(PORTLIBS) $(LIBNX)

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)
CPPFILES	:=	main.cpp cores.cpp uigfx.cpp audio.cpp bridge_lrps2.cpp buildstamp.cpp
ifeq ($(CORE_KIND),armsx2)
CPPFILES	+=	armsx2_compat.cpp armsx2_app.cpp
ifeq ($(ARMSX2_VK),1)
CPPFILES	+=	vkpresent.cpp
endif
endif
export LD	:=	$(CXX)
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o)
export OFILES 	:=	$(OFILES_SRC)
export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export APP_ICON := $(TOPDIR)/$(ICON)
export NROFLAGS += --icon=$(APP_ICON) --nacp=$(CURDIR)/$(TARGET).nacp --romfsdir=$(CURDIR)/$(ROMFS)

.PHONY: $(BUILD) clean all
all: $(BUILD)
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@rm -f $@/buildstamp.o
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf
else
DEPENDS	:=	$(OFILES:.o=.d)
all	:	$(OUTPUT).nro
$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf	:	$(OFILES)
-include $(DEPENDS)
endif
