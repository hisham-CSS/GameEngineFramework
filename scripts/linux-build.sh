#!/usr/bin/env bash
# Configure + build Cat Splat Engine on Linux.
#
# A convenience wrapper for the Linux build. The committed CMakePresets.json is
# the shared source of configurations (its presets are gated to Windows hosts);
# this script is the Linux equivalent, kept separate so it never disturbs that.
#
# Prerequisites (see docs/BUILDING_LINUX.md for the full list):
#   - CMake >= 3.21, Ninja, a C++17 compiler (gcc >= 11 or clang >= 14)
#   - vcpkg, with VCPKG_ROOT exported to its checkout
#   - GLFW's X11 dev packages installed on the host
#
# Usage:  scripts/linux-build.sh [BuildType]      (default: RelWithDebInfo)
set -euo pipefail

: "${VCPKG_ROOT:?set VCPKG_ROOT to your vcpkg checkout, e.g. export VCPKG_ROOT=\$HOME/vcpkg}"

BUILD_TYPE="${1:-RelWithDebInfo}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/out/build/linux-$BUILD_TYPE"

# x64-linux-dynamic (not the default static triplet): the engine builds as a
# shared library, and static vcpkg archives are not -fPIC by default, so they
# fail to link into libEngine.so. The dynamic triplet keeps the .so model that
# mirrors the Windows build.
cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-linux-dynamic

# CSE_KEEP_GOING=1 makes ninja report EVERY compile error instead of stopping at
# the first failing batch. That matters far more than it sounds: this port has no
# Linux machine behind it, so the only compiler that ever sees this code is a CI
# runner ~10 minutes away. Stopping early turns a list of errors into one error
# per round trip. Off by default because locally you want the first error fast.
if [[ "${CSE_KEEP_GOING:-0}" != "0" ]]; then
  cmake --build "$BUILD_DIR" -- -k 0
else
  cmake --build "$BUILD_DIR"
fi

echo
echo "Built into $BUILD_DIR/build/bin/$BUILD_TYPE"
echo "Note: the PhysX backend is Windows-only; Linux runs with Jolt + Simple."
echo "Runtime linking (rpath/\$ORIGIN) is Phase 2 — binaries may not yet launch"
echo "without LD_LIBRARY_PATH pointing at the vcpkg + Engine .so directories."
