#!/usr/bin/env bash
# Host regression: validate watchdog rollback logic without CommCenter injection.
set -euo pipefail

ROOT="${SMS_OUT_DRY_ROOT:-$(mktemp -d)}"
JB="${ROOT}/var/jb"
STATE="/tmp/tesla-sms-out-bypass-watchdog"
ARM="/tmp/tesla-sms-out-bypass-arm"
DISABLE="/tmp/tesla-sms-out-bypass-disable"
WATCHDOG_SRC="$(cd "$(dirname "$0")/.." && pwd)/TeslaSMSOutBypass/watchdog/sms-out-bypass-watchdog.sh"

mkdir -p "${JB}/Library/TeslaSMSOutBypass/watchdog"
cp "${WATCHDOG_SRC}" "${JB}/Library/TeslaSMSOutBypass/watchdog/"
chmod +x "${JB}/Library/TeslaSMSOutBypass/watchdog/sms-out-bypass-watchdog.sh"

export JB_PREFIX="${JB}"
export SMS_OUT_BYPASS_DRY_RUN=1
export FAKE_HEALTH_FAIL=1
export SMS_OUT_BYPASS_WINDOW_SEC=5
export SMS_OUT_BYPASS_POLL_SEC=1
export SMS_OUT_BYPASS_HEALTH_GRACE_SEC=1

rm -rf "${STATE}"
rm -f "${DISABLE}"
: >"${ARM}"

LOG="${STATE}/watchdog-summary.txt"

set +e
bash "${JB}/Library/TeslaSMSOutBypass/watchdog/sms-out-bypass-watchdog.sh"
rc=$?
set -e

if [[ ! -f "${DISABLE}" ]]; then
  echo "FAIL: kill-switch sentinel not created" >&2
  exit 1
fi
if ! grep -q 'health_check_failed' "${LOG}"; then
  echo "FAIL: expected health_check_failed in ${LOG}" >&2
  cat "${LOG}" >&2 || true
  exit 1
fi

echo "Stage C dry-run OK"