#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="${1:-config/service.json}"
SERVER="${SERVER_BINARY:-${ROOT_DIR}/bin/homophone-replacer-server}"
if [[ ! -x "${SERVER}" ]]; then
  SERVER="${ROOT_DIR}/build/local/bin/homophone-replacer-server"
fi
[[ -x "${SERVER}" ]] || { echo "Server executable not found: ${SERVER}" >&2; exit 1; }
cd "${ROOT_DIR}"
[[ -f "${CONFIG}" ]] || { echo "Config file not found: ${CONFIG}" >&2; exit 1; }
export LD_LIBRARY_PATH="${ROOT_DIR}/lib:${ROOT_DIR}/build/local/lib:${LD_LIBRARY_PATH:-}"
exec "${SERVER}" --config "${CONFIG}"
