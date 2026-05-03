# pd/Makefile -- void.linkaudio externals for Pure Data
#
# Build:    make
# Clean:    make clean
# Install:  make install objectsdir=<path/to/Pd/externals>
#
# Override Pd path:  make PDDIR=/path/to/Pd-X.Y.app/Contents/Resources
# Override arch:     make arch="x86_64"

lib.name = void.linkaudio

# Per-class sources
class.sources = \
    src/void.linkaudio.receive~.cpp \
    src/void.linkaudio.send~.cpp

# Sources statically linked into every class executable.
# This puts a copy of LinkAudioManager + its singleton state into each
# .pd_darwin / .pd_linux / .dll, matching the Max / TD pattern.
# (We can switch to shared.sources later if a single-instance singleton
# becomes a hard requirement.)
common.sources = \
    ../core/LinkAudioManager.cpp

# Pd installation path (Mac default; override on Win/Linux)
PDDIR ?= /Users/julien/DATA/WORK/_DEV/Pd-0.56-2.app/Contents/Resources

# ----- Compile flags --------------------------------------------------------
#
# Include paths, language, ASIO standalone defines (required by Link).

cflags = \
    -std=c++17 \
    -I../core \
    -I../thirdparty/link/include \
    -I../thirdparty/link/modules/asio-standalone/asio/include \
    -DASIO_STANDALONE \
    -DASIO_NO_DEPRECATED \
    -DASIO_DISABLE_THREAD_KEYWORD_EXTENSION

# C++ source-only flags; pdlibbuilder uses cxx.flags for .cpp files.
# (cflags above are added on top of these by pdlibbuilder.)
cxx.flags = -std=c++17

# ----- Platform-specific ----------------------------------------------------

# macOS: universal binary, raise deployment target to 11.0 (required by
# Link's std::atomic / sockets usage), pull in CoreFoundation.
ifeq ($(shell uname),Darwin)
    arch    = arm64 x86_64
    cflags += -DLINK_PLATFORM_MACOSX=1 -mmacosx-version-min=11.0
    ldlibs += -framework CoreFoundation
endif

# Linux: nothing special, Link uses POSIX sockets + pthreads.
ifeq ($(shell uname),Linux)
    cflags += -DLINK_PLATFORM_LINUX=1
    ldlibs += -lpthread
endif

# Windows is handled by LinkAudioPlatform.h (winsock + pragmas).
# Still need to set the platform define for Link.
# pdlibbuilder picks _WIN32 / _WIN64 from the toolchain; we add Link's flag.
ifneq (,$(filter MINGW% MSYS% CYGWIN%, $(shell uname)))
    cflags += -DLINK_PLATFORM_WINDOWS=1 -D_WIN32_WINNT=0x0601
    # GCC/MinGW ignore the #pragma comment(lib, ...) in LinkAudioPlatform.h
    # (those are MSVC-only). Link explicitly to the Win sockets / IP / multimedia
    # libraries that Link Audio needs at runtime: ntohs / ntohl / GetTickCount /
    # GetAdaptersAddresses are pulled from these.
    ldlibs += -lws2_32 -liphlpapi -lwinmm
endif

# ----- pd-lib-builder ------------------------------------------------------

include ../thirdparty/pd-lib-builder/Makefile.pdlibbuilder
