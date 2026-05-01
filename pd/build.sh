#!/usr/bin/env bash
# build.sh -- universal binary build wrapper for void.linkaudio Pd externals
#
# Reason for existence: pd-lib-builder hardcodes `arch := $(target.arch)`
# internally with `:=`, which overwrites any Makefile-level `arch =`.
# Command-line arguments to make do override the `:=`, so we invoke make
# with the desired arch list explicitly.
#
# On macOS this produces a universal arm64 + x86_64 binary.
# On Linux / Windows this is a no-op (single-arch).

set -euo pipefail

cd "$(dirname "$0")"

case "$(uname)" in
    Darwin)
        make "$@" arch="arm64 x86_64"
        ;;
    *)
        make "$@"
        ;;
esac
