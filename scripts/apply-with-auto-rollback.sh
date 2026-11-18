#!/usr/bin/env bash
# Documentation-only apply template (do not run from public CI). Device deployment is private-repo only.
set -euo pipefail

SUMMARY="/tmp/teslapad-sms-tweak-apply-summary.txt"
DEB_PATH="${1:-}"

if [[ -z "${DEB_PATH}" ]]; then
  echo "Usage: $0 /path/to/com.gitop.teslapadsmsbypass_*.deb" >&2
  exit 2
fi

if [[ -f /var/mobile/.eng-run/sms-tweak-disable ]] || [[ -f /tmp/teslapad-sms-tweak-disable ]]; then
  echo "Kill-switch file present; refusing to install." | tee "${SUMMARY}"
  exit 3
fi

echo "INFO: install ${DEB_PATH} and restart CommCenter only on an authorized test device." | tee "${SUMMARY}"
echo "This public repo does not ship device-specific SSH/host automation."