#!/usr/bin/env bash
set -euo pipefail

[[ "${EUID}" -eq 0 ]] || { echo "Run as root" >&2; exit 1; }
systemctl disable --now homophone-replacer.service 2>/dev/null || true
rm -f /etc/systemd/system/homophone-replacer.service
systemctl daemon-reload
if [[ "${1:-}" == "--purge" ]]; then
  rm -rf /opt/homophone-replacer
else
  find /opt/homophone-replacer -mindepth 1 -maxdepth 1 \
    ! -name config ! -name data ! -name logs -exec rm -rf {} +
fi
