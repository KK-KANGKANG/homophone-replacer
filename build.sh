#!/usr/bin/env bash
set -euo pipefail

# Usage:
#   ./build.sh                # Release build (default)
#   CONFIG=Debug ./build.sh   # Debug build
#   GENERATOR="Ninja" ./build.sh  # Use Ninja
#   BUILD_DIR=out ./build.sh  # Custom build dir

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT_DIR/build"}
CONFIG=${CONFIG:-Release}
GENERATOR=${GENERATOR:-}

mkdir -p "$BUILD_DIR"

if [[ -n "${GENERATOR}" ]]; then
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" -G "$GENERATOR"
else
  cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG"
fi

if command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
else
  JOBS=4
fi

cmake --build "$BUILD_DIR" --config "$CONFIG" -j"$JOBS"

echo
echo "Build finished. Binaries:"
echo "  $BUILD_DIR/bin/homophone-replacer-standalone"
echo
echo "Run example (from project root):"
echo "  ./build/bin/homophone-replacer-standalone --text '他想知道玄界芯片问题的答案' --debug"
echo
echo "If shared libraries are not found, set runtime path first:"
echo "  export LD_LIBRARY_PATH=\"$BUILD_DIR/lib:$LD_LIBRARY_PATH\""
echo

