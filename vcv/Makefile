# VoidLinkAudio — VCV Rack plugin
#
# Build:
#   RACK_DIR=/path/to/Rack-SDK make
#
# Install (signed .vcvplugin into Rack user plugin folder):
#   RACK_DIR=/path/to/Rack-SDK make install

# ── Plugin name ──────────────────────────────────────────────────────────────
SLUG = VoidLinkAudio

# ── Repo paths (relative — absolute paths break on Windows because of C:) ───
# vcv/ is one level under td-linkaudio/, so ../core and ../thirdparty work
# from the vcv/ working directory. Absolute paths with drive letters confuse
# Make's pattern rules in compile.mk (the ':' in 'C:' parses as target/dep).
CORE_REL  := ../core
LINK_REL  := ../thirdparty/link

# ── Sources ──────────────────────────────────────────────────────────────────
SOURCES += $(wildcard src/*.cpp)
SOURCES += $(CORE_REL)/LinkAudioManager.cpp

# ── Includes ─────────────────────────────────────────────────────────────────
FLAGS += -I$(CORE_REL)
FLAGS += -I$(LINK_REL)/include
FLAGS += -I$(LINK_REL)/modules/asio-standalone/asio/include

# ASIO standalone (used by Link transitively for networking).
FLAGS += -DASIO_STANDALONE=1

# ── Distributables ───────────────────────────────────────────────────────────
DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += plugin.json

# ── Include the Rack SDK boilerplate ─────────────────────────────────────────
# This pulls in arch.mk which defines ARCH_MAC / ARCH_LIN / ARCH_WIN. We
# add platform-specific Link defines AFTER this include so the conditionals
# evaluate against the correct values.
RACK_DIR ?= ../../Rack-SDK

include $(RACK_DIR)/plugin.mk

# Force C++17 (Link headers and our core/ require it).
# Override the SDK's default -std=c++11 by appending later in CXXFLAGS.
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS)) -std=c++17

# ── Platform-specific Link Audio defines (post-include) ──────────────────────
ifdef ARCH_MAC
    FLAGS    += -DLINK_PLATFORM_MACOSX=1
    CFLAGS   += -DLINK_PLATFORM_MACOSX=1
    CXXFLAGS += -DLINK_PLATFORM_MACOSX=1
    LDFLAGS  += -framework CoreFoundation
endif
ifdef ARCH_LIN
    FLAGS    += -DLINK_PLATFORM_LINUX=1
    CFLAGS   += -DLINK_PLATFORM_LINUX=1
    CXXFLAGS += -DLINK_PLATFORM_LINUX=1
endif
ifdef ARCH_WIN
    FLAGS    += -DLINK_PLATFORM_WINDOWS=1
    CFLAGS   += -DLINK_PLATFORM_WINDOWS=1
    CXXFLAGS += -DLINK_PLATFORM_WINDOWS=1
    LDFLAGS  += -lws2_32 -liphlpapi -lwinmm
endif