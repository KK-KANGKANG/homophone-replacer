#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-${ROOT_DIR}/.fst-build}"
IMAGE="${PYNINI_IMAGE:-homophone-pynini:2.1.7}"
MODE="tone-aware"
INSTALL=false

for arg in "$@"; do
  case "${arg}" in
    --exact-only) MODE="exact" ;;
    --install) INSTALL=true ;;
    *) echo "Usage: $0 [--exact-only] [--install]" >&2; exit 2 ;;
  esac
done

if [[ "${MODE}" == "exact" && "${INSTALL}" == true ]]; then
  echo "--exact-only cannot be combined with --install" >&2
  exit 2
fi

mkdir -p "${OUTPUT_DIR}"

DOCKER_MEMORY="$(docker info --format '{{.MemTotal}}')"
RECOMMENDED_MEMORY=$((16 * 1024 * 1024 * 1024))
if (( DOCKER_MEMORY < RECOMMENDED_MEMORY )); then
  echo "Warning: Docker has less than 16GB memory; FST build may be killed." >&2
fi

docker build \
  --platform linux/amd64 \
  -f "${ROOT_DIR}/Dockerfile.pynini" \
  -t "${IMAGE}" \
  "${ROOT_DIR}"

docker run --rm --platform linux/amd64 \
  -v "${ROOT_DIR}:/work" \
  -w /work \
  "${IMAGE}" \
  python3 -m unittest make_replace.tests.test_build_replace_fst -v

if [[ "${MODE}" == "exact" ]]; then
  FST_NAME="replace-exact.fst"
  REPORT_NAME="exact-report.txt"
  EXTRA_ARGS=(--exact-only)
else
  FST_NAME="replace-tone-aware.fst"
  REPORT_NAME="tone-aware-report.txt"
  EXTRA_ARGS=()
fi

docker run --rm --platform linux/amd64 \
  -v "${ROOT_DIR}:/work" \
  -v "${OUTPUT_DIR}:/output" \
  -w /work \
  "${IMAGE}" \
  python3 -m make_replace.main build \
    --mapping data/hr-files/mapping.txt \
    --fst-output "/output/${FST_NAME}" \
    --report-output "/output/${REPORT_NAME}" \
    "${EXTRA_ARGS[@]}"

echo "Generated: ${OUTPUT_DIR}/${FST_NAME}"
echo "Report: ${OUTPUT_DIR}/${REPORT_NAME}"

if [[ "${INSTALL}" == true ]]; then
  cp "${OUTPUT_DIR}/${FST_NAME}" "${ROOT_DIR}/data/hr-files/replace.fst"
  cp "${OUTPUT_DIR}/${REPORT_NAME}" \
    "${ROOT_DIR}/data/hr-files/fst-build-report.txt"
  echo "Installed production FST and report into data/hr-files"
fi
