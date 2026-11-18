#!/usr/bin/env bash
# Lightweight connectivity checks (used only after founder arms experiment).
# Does not depend on CommCenter answering; does not use SSH/<VPN_REDACTED>.
set -euo pipefail

if ifconfig pdp_ip0 2>/dev/null | grep -q 'inet '; then
  exit 0
fi
exit 1
