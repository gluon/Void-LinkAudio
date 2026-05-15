#!/usr/bin/env bash
# build.sh -- universal binary build wrapper for void.linkaudio Pd externals
#
# Reason for existence: pd-lib-builder hardcodes `arch := $(target.arch)`
# internally with `:=`, which overwrites any Makefile-level `arch =`.
# Command-line arguments to make do override the `:=`, so we invoke make
# with the desired arch list explicitly.
#
# macOS : universal arm64 + x86_64 binary (lipo).
# Linux : two passes — native arch + cross-compile to the other arch.
# Windows / other : single-arch.
set -euo pipefail
cd "$(dirname "$0")"

case "$(uname)" in
    Darwin)
        make "$@" arch="arm64 x86_64"
        ;;
    Linux)
        HOST_ARCH="$(uname -m)"
        case "$HOST_ARCH" in
            aarch64|arm64)
                NATIVE_TAG="linux-arm64"
                CROSS_TAG="linux-x64"
                CROSS_CC="x86_64-linux-gnu-gcc"
                CROSS_CXX="x86_64-linux-gnu-g++"
                ;;
            x86_64)
                NATIVE_TAG="linux-x64"
                CROSS_TAG="linux-arm64"
                CROSS_CC="aarch64-linux-gnu-gcc"
                CROSS_CXX="aarch64-linux-gnu-g++"
                ;;
            *)
                echo "Unknown Linux host arch: $HOST_ARCH"
                exit 1
                ;;
        esac

        # --- Native build ---
        echo "==> Native build ($NATIVE_TAG)"
        make "$@"
        mkdir -p "dist/$NATIVE_TAG"
        cp -f *.pd_linux "dist/$NATIVE_TAG/" 2>/dev/null || true

        # --- Cross-compile build ---
        if command -v "$CROSS_CC" >/dev/null 2>&1; then
            echo "==> Cross-compile build ($CROSS_TAG)"
            make clean >/dev/null 2>&1 || true
            make "$@" CC="$CROSS_CC" CXX="$CROSS_CXX"
            mkdir -p "dist/$CROSS_TAG"
            cp -f *.pd_linux "dist/$CROSS_TAG/" 2>/dev/null || true
        else
            echo "==> Cross-compiler $CROSS_CC not found — skipping $CROSS_TAG"
            echo "    Install with: sudo apt install gcc-x86-64-linux-gnu g++-x86-64-linux-gnu"
        fi
        ;;
    *)
        make "$@"
        ;;
esac