#!/usr/bin/env bash
set -euo pipefail

NDK_PATH="${ANDROID_NDK_HOME:-}"
if [[ -z "$NDK_PATH" ]]; then
  if [[ $# -ge 1 ]]; then
    NDK_PATH="$1"
    shift
  else
    echo "Set ANDROID_NDK_HOME or pass NDK path as first arg" >&2
    exit 1
  fi
fi

API="${ANDROID_API:-24}"
ABIS=("armeabi-v7a" "arm64-v8a" "x86" "x86_64")

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/third_party/libcodec2"
INSTALL_ROOT="$ROOT/third_party/libcodec2_android"
BUILD_ROOT="$INSTALL_ROOT/build"

for ABI in "${ABIS[@]}"; do
  echo "Building codec2 for ${ABI}"
  BUILD_DIR="$BUILD_ROOT/$ABI"

  cmake -S "$SRC" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM="android-$API" \
    -DBUILD_SHARED_LIBS=ON \
    -DUNITTEST=OFF \
    -DINSTALL_EXAMPLES=OFF \
    -DLPCNET=OFF \
    -DCMAKE_BUILD_TYPE=Release

  cmake --build "$BUILD_DIR" --config Release --target codec2
  cmake --install "$BUILD_DIR" --prefix "$INSTALL_ROOT/$ABI"

done
