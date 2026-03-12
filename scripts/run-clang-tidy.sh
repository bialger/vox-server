#!/usr/bin/env bash
# Run clang-tidy on project sources with MinGW toolchain flags.
# Required when building with MinGW GCC but analyzing with Clang (e.g. on Windows with VS installed).
# Clang would otherwise pick up MSVC headers and fail with "expected Clang 19.0.0 or newer".

set -e
MINGW_TOOLCHAIN="${MINGW_TOOLCHAIN:-C:/Program Files/mingw64}"
BUILD_DIR="${1:-build}"

mapfile -t sourcefiles < <(git ls-files '*.c' '*.cpp')
clang-tidy "${sourcefiles[@]}" -p "$BUILD_DIR" \
  --extra-arg-before=--driver-mode=gcc \
  --extra-arg-before=--target=x86_64-w64-windows-gnu \
  --extra-arg-before="--gcc-toolchain=$MINGW_TOOLCHAIN"
