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

LOCAL_PATH := $(call my-dir)
CORE_DIR   := $(LOCAL_PATH)/..

SOURCES := \
	$(CORE_DIR)/*.cpp       \
	$(CORE_DIR)/src/*.cpp   \
	$(CORE_DIR)/src/*/*.cpp \
	$(CORE_DIR)/src/*/*/*.cpp
SOURCES := $(wildcard $(SOURCES))

  LDFLAGS := 

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
  LDFLAGS  += -O2
endif

CFLAGS  += $(CPUFLAGS) -std=c++11 -fpic -fomit-frame-pointer -fno-exceptions -fno-non-call-exceptions -Wno-address-of-packed-member -Wno-format -Wno-switch
CFLAGS  += -fvisibility=hidden -ffunction-sections -fdata-sections
CFLAGS  += -pthread -D__LIBRETRO__ -Iinclude

LDFLAGS += $(CPUFLAGS) -shared

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
LOCAL_CPPFLAGS     := $(CFLAGS)
LOCAL_LDFLAGS      := $(LDFLAGS) #-Wl,-version-script=$(CORE_DIR)/libretro/link.T
LOCAL_LDLIBS       := -llog
LOCAL_CPP_FEATURES := rtti
LOCAL_ARM_MODE     := arm
include $(BUILD_SHARED_LIBRARY)
