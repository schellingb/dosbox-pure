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
ISMAC      := $(wildcard /Applications)
PIPETONULL := $(if $(ISWIN),>nul 2>nul,>/dev/null 2>/dev/null)
PROCCPU    := $(shell $(if $(ISWIN),GenuineIntel Intel sse sse2,cat /proc/cpuinfo))

LOCAL_PATH := $(call my-dir)
CORE_DIR   := $(LOCAL_PATH)/..

#SOURCES := \
#	$(CORE_DIR)/*.cpp       \
#	$(CORE_DIR)/src/*.cpp   \
#	$(CORE_DIR)/src/*/*.cpp \
#	$(CORE_DIR)/src/*/*/*.cpp
include $(LOCAL_PATH)/Sources.mk

  LDFLAGS :=  -Wl,-dead_strip

  CPUFLAGS :=

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
  LDFLAGS  += -O2 -fno-ident
endif

CFLAGS  += $(CPUFLAGS) -std=c++11 -fpic -fomit-frame-pointer -fno-exceptions -fno-non-call-exceptions -Wno-address-of-packed-member -Wno-format -Wno-switch
CFLAGS  += -fvisibility=hidden -ffunction-sections -fdata-sections
CFLAGS  += -pthread -D__LIBRETRO__ -Iinclude

LDFLAGS += $(CPUFLAGS) -shared
#LDFLAGS += $(CPUFLAGS) -lpthread -shared
#LDFLAGS += -static-libstdc++ -static-libgcc #adds 1MB to output

WITH_DYNAREC :=
ifeq ($(TARGET_ARCH_ABI), armeabi)
    WITH_DYNAREC := oldarm
else ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    WITH_DYNAREC := arm
else ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
    WITH_DYNAREC := arm64
else ifeq ($(TARGET_ARCH_ABI), x86)
    WITH_DYNAREC := x86_new
else ifeq ($(TARGET_ARCH_ABI), x86_64)
    WITH_DYNAREC := x86_64
else ifeq ($(TARGET_ARCH_ABI), mips)
    WITH_DYNAREC := mips
else ifeq ($(TARGET_ARCH_ABI), mips64)
    WITH_DYNAREC := mips64
endif

include $(CLEAR_VARS)
LOCAL_MODULE       := retro
LOCAL_SRC_FILES    := $(SOURCES)
LOCAL_C_INCLUDES   := $(CORE_DIR)/include
LOCAL_CFLAGS       := $(CFLAGS)
LOCAL_CPPFLAGS     := $(CLAGS)
LOCAL_LDFLAGS      := $(LDFLAGS) #-Wl,-version-script=$(CORE_DIR)/libretro/link.T
LOCAL_LDLIBS       := -llog
LOCAL_CPP_FEATURES := rtti exceptions
LOCAL_DISABLE_FATAL_LINKER_WARNINGS := true
LOCAL_ARM_MODE     := arm
include $(BUILD_SHARED_LIBRARY)
