#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$(uname -s)" == "Linux" ]] || { echo "Run this script on Linux" >&2; exit 1; }
case "$(uname -m)" in
  x86_64) PACKAGE_ARCH=x64 ;;
  aarch64|arm64) PACKAGE_ARCH=arm64 ;;
  *) echo "Unsupported Linux architecture: $(uname -m)" >&2; exit 1 ;;
esac
PACKAGE_NAME="homophone-replacer-linux-${PACKAGE_ARCH}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/linux-${PACKAGE_ARCH}}"
STAGE_ROOT="${ROOT_DIR}/dist/stage-linux-${PACKAGE_ARCH}"
STAGE="${STAGE_ROOT}/${PACKAGE_NAME}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF
cmake --build "${BUILD_DIR}" -j"${JOBS:-4}"
rm -rf "${STAGE_ROOT}"
cmake --install "${BUILD_DIR}" --prefix "${STAGE}"
tar -C "${STAGE_ROOT}" -czf "${ROOT_DIR}/dist/${PACKAGE_NAME}.tar.gz" \
  "${PACKAGE_NAME}"
echo "Generated dist/${PACKAGE_NAME}.tar.gz"
