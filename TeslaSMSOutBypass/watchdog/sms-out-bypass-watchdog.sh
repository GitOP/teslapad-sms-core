#!/var/jb/usr/bin/bash
# On-device rollback watchdog. Independent of CommCenter and SSH/<VPN_REDACTED>.
set -euo pipefail

JB_PREFIX="${JB_PREFIX:-/var/jb}"
STATE_DIR="/tmp/tesla-sms-out-bypass-watchdog"
DISABLE_SENTINEL="/tmp/tesla-sms-out-bypass-disable"
ARM_FILE="/tmp/tesla-sms-out-bypass-arm"
SUMMARY="${STATE_DIR}/watchdog-summary.txt"
HEALTH_SCRIPT="${JB_PREFIX}/Library/TeslaSMSOutBypass/watchdog/health-check.sh"
WINDOW_SEC="${SMS_OUT_BYPASS_WINDOW_SEC:-900}"
INTERVAL_SEC="${SMS_OUT_BYPASS_POLL_SEC:-30}"
GRACE_SEC="${SMS_OUT_BYPASS_HEALTH_GRACE_SEC:-180}"

log() {
  printf '%s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$*" >>"${SUMMARY}"
}

rollback() {
  local reason="$1"
  mkdir -p "${STATE_DIR}"
  : >"${DISABLE_SENTINEL}"
  if command -v dpkg >/dev/null 2>&1; then
    dpkg -r com.gitop.teslapadsmsoutbypass 2>/dev/null || true
  fi
  {
    echo "rollback_at=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "reason=${reason}"
    echo "action=disable_sentinel_and_optional_dpkg_remove"
  } >>"${SUMMARY}"
}

health_ok() {
  if [[ -n "${FAKE_HEALTH_FAIL:-}" ]]; then
    return 1
  fi
  if [[ -n "${SMS_OUT_BYPASS_DRY_RUN:-}" ]]; then
    return 0
  fi
  if [[ ! -x "${HEALTH_SCRIPT}" ]]; then
    return 0
  fi
  "${HEALTH_SCRIPT}"
}

main() {
  mkdir -p "${STATE_DIR}"
  if [[ ! -f "${ARM_FILE}" ]]; then
    log "watchdog_exit=no_arm_file"
    exit 0
  fi
  if [[ -f "${DISABLE_SENTINEL}" ]]; then
    log "watchdog_exit=already_disabled"
    exit 0
  fi

  local start_ts deadline grace_end fail_streak=0
  start_ts=$(date +%s)
  grace_end=$(( start_ts + GRACE_SEC ))
  deadline=$(( start_ts + WINDOW_SEC ))
  log "watchdog_start window_sec=${WINDOW_SEC} grace_sec=${GRACE_SEC}"

  while [[ $(date +%s) -lt ${deadline} ]]; do
    if [[ -f "${DISABLE_SENTINEL}" ]]; then
      log "watchdog_stop=kill_switch"
      exit 0
    fi
    if ! health_ok; then
      local now
      now=$(date +%s)
      if [[ ${now} -lt ${grace_end} ]]; then
        log "health_fail_ignored_in_grace"
        sleep "${INTERVAL_SEC}"
        continue
      fi
      fail_streak=$(( fail_streak + 1 ))
      log "health_fail streak=${fail_streak}"
      if [[ ${fail_streak} -ge 2 ]]; then
        rollback "health_check_failed"
        exit 2
      fi
    else
      fail_streak=0
    fi
    sleep "${INTERVAL_SEC}"
  done

  rollback "window_timeout"
  exit 3
}

main "$@"
