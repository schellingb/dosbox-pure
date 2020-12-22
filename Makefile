#
#  Copyright (C) 2020 Bernhard Schelling
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

ISWIN      := $(findstring :,$(firstword $(subst \, ,$(subst /, ,$(abspath .)))))
PIPETONULL := $(if $(ISWIN),>nul 2>nul,>/dev/null 2>/dev/null)
PROCCPU    := $(shell $(if $(ISWIN),GenuineIntel Intel sse sse2,cat /proc/cpuinfo))

SOURCES := \
	*.cpp       \
	src/*.cpp   \
	src/*/*.cpp \
	src/*/*/*.cpp

SOURCES_EXCLUDE := \
	src/hardware/opl.cpp

CXX := g++

ifneq ($(and $(filter ARMv7,$(PROCCPU)),$(filter neon,$(PROCCPU))),)
  CPUFLAGS := -mcpu=cortex-a72 -mfpu=neon-fp-armv8 -mfloat-abi=hard -ffast-math
  ifneq ($(findstring version 10,$(shell g++ -v 2>&1)),)
    # Switch to gcc 9 to avoid buggy assembly genetation of gcc 10
    # On armv7l, gcc 10.2 with -O2 on the file core_dynrec.cpp generates assembly that wrongfully passes NULL to the runcode function
    # resulting in a segfault crash. It can be observed by writing block->cache.start to stdout twice where it is NULL at first
    # and then the actual value thereafter. This affects upstream SVN DOSBox as well as this core.
    CXX := g++-9
  endif
else
  CPUFLAGS :=
endif

OUTNAME := dosbox_pure_libretro.so

ifeq ($(BUILD),DEBUG)
  BUILDDIR := debug
  CFLAGS   := -DDEBUG -D_DEBUG -g -O0
  LDFLAGS  :=
else ifeq ($(BUILD),PROFILE)
  BUILDDIR := profile
  CFLAGS   := -DNDEBUG -O2
  LDFLAGS  := 
else ifeq ($(BUILD),RELEASEDBG)
  BUILDDIR := releasedbg
  CFLAGS   := -DNDEBUG -ggdb -O2
  LDFLAGS  := -ggdb -O2
else ifeq ($(BUILD),ASAN)
  BUILDDIR := asan
  CFLAGS   := -DDEBUG -D_DEBUG -g -O0 -fsanitize=address -fno-omit-frame-pointer
  LDFLAGS  := -fsanitize=address -g -O0
else
  BUILD    := RELEASE
  BUILDDIR := release
  CFLAGS   := -DNDEBUG -O2 -s -fno-ident
  LDFLAGS  := -O2 -s -Wl,--strip-all -fno-ident
endif

CFLAGS  += $(CPUFLAGS) -fpic -fomit-frame-pointer -fno-exceptions -fno-non-call-exceptions -Wno-psabi -Wno-address-of-packed-member -Wno-format
CFLAGS  += -fvisibility=hidden -ffunction-sections -fdata-sections
CFLAGS  += -pthread -D__LIBRETRO__ -Iinclude

LDFLAGS += $(CPUFLAGS) -lpthread -Wl,--gc-sections -shared
#LDFLAGS += -static-libstdc++ -static-libgcc #adds 1MB to output

.PHONY: all clean
all: $(OUTNAME)

$(info Building $(OUTNAME) with $(BUILD) configuration (obj files stored in build/$(BUILDDIR)) ...)
SOURCES := $(filter-out $(SOURCES_EXCLUDE), $(wildcard $(SOURCES)))
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
	$(info Linking $@ ...)
	@$(CXX) $(LDFLAGS) -o $@ $^

define COMPILE
	$(info Compiling $2 ...)
	@$(CXX) $(CFLAGS) -MMD -MP -o $1 -c $2
endef
