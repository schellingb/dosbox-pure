#
#  Copyright (C) 2020-2021 Bernhard Schelling
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#

ifeq ($(ISWIN),)
ISWIN      := $(findstring :,$(firstword $(subst \, ,$(subst /, ,$(abspath .)))))
endif

ifeq ($(ISMAC),)
ISMAC      := $(wildcard /Applications)
endif

PIPETONULL := $(if $(ISWIN),>nul 2>nul,>/dev/null 2>/dev/null)

SOURCES := \
  *.cpp       \
  src/*.cpp   \
  src/*/*.cpp \
  src/*/*/*.cpp

CPUFLAGS :=
ifneq ($(ISWIN),)
  OUTNAME := dosbox_pure_libretro.dll
  CXX     ?= g++
  LDFLAGS := -Wl,--gc-sections -fno-ident
else ifeq($(platform),ios-arm64)
  OUTNAME := dosbox_pure_libretro.dylib
  CXX     = clang++ -arch arm64 -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk
  LDFLAGS := -Wl,-dead_strip
else ifneq ($(ISMAC),)
  OUTNAME := dosbox_pure_libretro_ios.dylib
  CXX     ?= clang++
  LDFLAGS := -Wl,-dead_strip
else ifeq ($(platform), vita)
  OUTNAME := dosbox_pure_libretro_vita.a
  CXX     ?= arm-vita-eabi-g++
  AR      ?= arm-vita-eabi-ar
  COMMONFLAGS += -DVITA
  COMMONFLAGS += -mthumb -mcpu=cortex-a9 -mfloat-abi=hard -ftree-vectorize -ffast-math -fsingle-precision-constant -funroll-loops
  COMMONFLAGS += -mword-relocations
  COMMONFLAGS += -fno-optimize-sibling-calls
  STATIC_LINKING = 1
else ifeq ($(platform),ctr)
  OUTNAME := dosbox_pure_libretro_ctr.a
  CXX     ?= $(DEVKITARM)/bin/arm-none-eabi-g++
  AR      ?= $(DEVKITARM)/bin/arm-none-eabi-ar
  COMMONFLAGS += -DARM11 -D_3DS -Os -s -I$(CTRULIB)/include/ -DHAVE_MKDIR
  COMMONFLAGS += -march=armv6k -mtune=mpcore -mfloat-abi=hard -mword-relocations
  COMMONFLAGS += -fomit-frame-pointer -fstrict-aliasing -ffast-math -fpermissive
  COMMONFLAGS += -I$(DEVKITPRO)/libctru/include
  STATIC_LINKING = 1
else ifeq ($(platform),ngc)
  OUTNAME := dosbox_pure_libretro_ngc.a
  CXX     ?= $(DEVKITPPC)/bin/powerpc-eabi-g++
  AR      ?= $(DEVKITPPC)/bin/powerpc-eabi-ar
  COMMONFLAGS += -DGEKKO -DHW_DOL -mrvl -mcpu=750 -meabi -mhard-float -D__POWERPC__ -D__ppc__ -DMSB_FIRST -DWORDS_BIGENDIAN=1
  STATIC_LINKING = 1
else ifeq ($(platform),wii)
  OUTNAME := dosbox_pure_libretro_wii.a
  CXX     ?= $(DEVKITPPC)/bin/powerpc-eabi-g++
  AR      ?= $(DEVKITPPC)/bin/powerpc-eabi-ar
  COMMONFLAGS += -DGEKKO -mrvl -mcpu=750 -meabi -mhard-float -fpermissive
  COMMONFLAGS += -U__INT32_TYPE__ -U __UINT32_TYPE__ -D__INT32_TYPE__=int -D__POWERPC__ -D__ppc__ -DMSB_FIRST -DWORDS_BIGENDIAN=1
  STATIC_LINKING = 1
else ifeq ($(platform),wiiu)
  OUTNAME := dosbox_pure_libretro_wiiu.a
  CXX     ?= $(DEVKITPPC)/bin/powerpc-eabi-g++
  AR      ?= $(DEVKITPPC)/bin/powerpc-eabi-ar
  COMMONFLAGS += -DGEKKO -DWIIU -DHW_RVL -mcpu=750 -meabi -mhard-float -I./deps/include/
  COMMONFLAGS += -U__INT32_TYPE__ -U __UINT32_TYPE__ -D__INT32_TYPE__=int -D__POWERPC__ -D__ppc__ -DMSB_FIRST -DWORDS_BIGENDIAN=1
  STATIC_LINKING = 1
else ifeq ($(platform),libnx)
  OUTNAME := dosbox_pure_libretro_libnx.a
  export DEPSDIR = $(CURDIR)
  include $(DEVKITPRO)/libnx/switch_rules
  COMMONFLAGS += -I$(LIBNX)/include/ -ftls-model=local-exec -specs=$(LIBNX)/switch.specs
  COMMONFLAGS += $(INCLUDE) -D__SWITCH__ -DHAVE_LIBNX
  STATIC_LINKING = 1
else ifeq ($(platform), gcw0)
  # You must used the toolchain built on or around 2014-08-20
  OUTNAME := dosbox_pure_libretro.so
  CXX     ?= /opt/gcw0-toolchain/usr/bin/mipsel-linux-g++
  LDFLAGS := -Wl,--gc-sections -fno-ident
  CPUFLAGS := -ffast-math -march=mips32r2 -mtune=mips32r2 -mhard-float -fexpensive-optimizations -frename-registers
else
  OUTNAME := dosbox_pure_libretro.so
  CXX     ?= g++
  LDFLAGS := -Wl,--gc-sections -fno-ident
  # ARM optimizations
  PROCCPU := $(shell cat /proc/cpuinfo))
  ifneq ($(and $(filter ARMv7,$(PROCCPU)),$(filter neon,$(PROCCPU))),)
    CPUFLAGS := -mcpu=cortex-a72 -mfpu=neon-fp-armv8 -mfloat-abi=hard -ffast-math
  else
    ifeq ($(CORTEX_A7), 1)
      CPUFLAGS += -marm -mcpu=cortex-a7
      ifeq ($(ARM_NEON), 1)
        CPUFLAGS += -mfpu=neon-vfpv4
      endif
    endif
    ifeq ($(ARM_HARDFLOAT), 1)
      CPUFLAGS += -mfloat-abi=hard
     endif
  endif
  CXX_VER := $(shell $(CXX) -v 2>&1)
  ifneq ($(and $(findstring arm,$(CXX_VER)),$(findstring version 10,$(CXX_VER))),)
    # Switch to gcc 9 to avoid buggy assembly genetation of gcc 10
    # On armv7l, gcc 10.2 with -O2 on the file core_dynrec.cpp generates assembly that wrongfully passes NULL to the runcode function
    # resulting in a segfault crash. It can be observed by writing block->cache.start to stdout twice where it is NULL at first
    # and then the actual value thereafter. This affects upstream SVN DOSBox as well as this core.
    CXX := g++-9
  endif
endif

ifeq ($(BUILD),DEBUG)
  BUILDDIR := debug
  CFLAGS   := -DDEBUG -D_DEBUG -g -O0
else ifeq ($(BUILD),PROFILE)
  BUILDDIR := profile
  CFLAGS   := -DNDEBUG -O2
else ifeq ($(BUILD),RELEASEDBG)
  BUILDDIR := releasedbg
  CFLAGS   := -DNDEBUG -ggdb -O2
  LDFLAGS  += -ggdb -O2
else ifeq ($(BUILD),ASAN)
  BUILDDIR := asan
  CFLAGS   := -DDEBUG -D_DEBUG -g -O0 -fsanitize=address -fno-omit-frame-pointer
  LDFLAGS  += -fsanitize=address -g -O0
else
  BUILD    := RELEASE
  BUILDDIR := release
  CFLAGS   := -DNDEBUG -O2 -fno-ident
  LDFLAGS  += -O2
endif

CFLAGS  += $(CPUFLAGS) -std=c++11 -fpic -fomit-frame-pointer -fno-exceptions -fno-non-call-exceptions -Wno-address-of-packed-member -Wno-format -Wno-switch
CFLAGS  += -fvisibility=hidden -ffunction-sections
CFLAGS  += -pthread -D__LIBRETRO__ -Iinclude
CFLAGS  += $(COMMONFLAGS)
#CFLAGS  += -fdata-sections #saves around 32 bytes on most platforms but wrongfully adds up to 60MB on msys2

LDFLAGS += $(CPUFLAGS) -lpthread -shared
#LDFLAGS += -static-libstdc++ -static-libgcc #adds 1MB to output and still dynamically links against libc and libm

.PHONY: all clean
all: $(OUTNAME)

$(info Building $(OUTNAME) with $(BUILD) configuration (obj files stored in build/$(BUILDDIR)) ...)
SOURCES := $(wildcard $(SOURCES))
$(if $(findstring ~,$(SOURCES)),$(error SOURCES contains a filename with a ~ character in it - Unable to continue))
$(if $(wildcard build),,$(shell mkdir "build"))
$(if $(wildcard build/$(BUILDDIR)),,$(shell mkdir "build/$(BUILDDIR)"))
OBJS := $(addprefix build/$(BUILDDIR)/,$(subst /,~,$(patsubst %,%.o,$(SOURCES))))
-include $(OBJS:%.o=%.d)
$(foreach F,$(OBJS),$(eval $(F): $(subst ~,/,$(patsubst build/$(BUILDDIR)/%.o,%,$(F))) ; $$(call COMPILE,$$@,$$<)))

clean:
	$(info Removing all build files ...)
	@$(if $(wildcard build/$(BUILDDIR)),$(if $(ISWIN),rmdir /S /Q,rm -rf) "build/$(BUILDDIR)" $(PIPETONULL))

$(OUTNAME) : $(OBJS)
ifeq ($(STATIC_LINKING), 1)
	$(info Static linking $@ ...)
	$(AR) rcs $@ $^
else
	$(info Linking $@ ...)
	$(CXX) $(LDFLAGS) -o $@ $^
	@-/opt/gcw0-toolchain/usr/mipsel-gcw0-linux-uclibc/bin/strip --strip-all $@ $(PIPETONULL);true # gcw0
	@-strip --strip-all $@ $(PIPETONULL);true # others
	@-strip -xS $@ $(PIPETONULL);true # mac
endif

define COMPILE
	$(info Compiling $2 ...)
	@$(CXX) $(CFLAGS) -MMD -MP -o $1 -c $2
endef
