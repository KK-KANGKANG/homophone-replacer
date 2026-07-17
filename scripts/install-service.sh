#!/usr/bin/env bash
set -euo pipefail

[[ "${EUID}" -eq 0 ]] || { echo "Run as root" >&2; exit 1; }
SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET_DIR=/opt/homophone-replacer
if ! id homophone-replacer >/dev/null 2>&1; then
  useradd --system --home "${TARGET_DIR}" --shell /usr/sbin/nologin homophone-replacer
fi
mkdir -p "${TARGET_DIR}"
if [[ -f "${TARGET_DIR}/config/service.json" ]]; then
  cp "${TARGET_DIR}/config/service.json" "${TARGET_DIR}/config/service.json.bak"
fi
cp -a "${SOURCE_DIR}/." "${TARGET_DIR}/"
mkdir -p "${TARGET_DIR}/logs"
chown -R homophone-replacer:homophone-replacer "${TARGET_DIR}/logs"
install -m 0644 "${TARGET_DIR}/packaging/homophone-replacer.service" \
  /etc/systemd/system/homophone-replacer.service
systemctl daemon-reload
systemctl enable homophone-replacer.service
echo "Installed. Start with: systemctl start homophone-replacer"
