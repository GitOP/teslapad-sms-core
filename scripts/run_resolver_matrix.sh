#!/usr/bin/env bash
# Resolver policy matrix (host CI). Fails closed on production row errors.
set -euo pipefail

ROOT="${ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
ARTIFACT_DIR="${ARTIFACT_DIR:-${ROOT}/resolver-matrix-artifacts}"
mkdir -p "${ARTIFACT_DIR}"

H="${HARNESS_BIN:-/tmp/out_sms_resolve_harness}"
RESULTS="${ARTIFACT_DIR}/resolver-matrix-results.json"
LOG="${ARTIFACT_DIR}/resolver-matrix.log"

CFLAGS="-O2 -Wall -Wextra -std=c11 -I${ROOT}/compat -I${ROOT} -I${ROOT}/TeslaSMSOutBypass -I${ROOT}/TeslaSMSOutBypass/include -I${ROOT}/tools -I${ROOT}/tests"

build_fixture_runner() {
  python3 "${ROOT}/scripts/gen_out_sms_resolver_fixture.py" "${ROOT}/tests/fixtures/out_sms_gate_macho_fixture.h"
  cc $CFLAGS \
    "${ROOT}/tests/test_out_sms_gate_resolver.c" \
    "${ROOT}/TeslaSMSOutBypass/src/out_sms_gate_core.c" \
    -o /tmp/test_out_sms_gate_resolver
  /tmp/test_out_sms_gate_resolver "${ROOT}"
}

build_harness() {
  cc $CFLAGS \
    "${ROOT}/tools/out_sms_resolve_harness.c" \
    "${ROOT}/tools/out_sms_macho_map.c" \
    "${ROOT}/TeslaSMSOutBypass/src/out_sms_gate_core.c" \
    -o "${H}"
}

run_macho_matrix() {
  local macho="${1:?macho path}"
  build_harness
  : > "${RESULTS}.partial"
  echo '[' >> "${RESULTS}.partial"

  local sep=""
  local fail=0

  record() {
    local id="$1" expect_rc="$2"
    shift 2
    local line rc
    set +e
    line="$("$@")"
    rc=$?
    set -e
    printf '%s%s\n' "${sep}" "${line}" >> "${RESULTS}.partial"
    sep=","
    if [[ "${expect_rc}" -eq 0 && "${rc}" -ne 0 ]]; then
      echo "FAIL: ${id} expected OK rc=${rc}" >&2
      fail=1
    fi
    if [[ "${expect_rc}" -ne 0 && "${rc}" -eq 0 ]]; then
      echo "FAIL: ${id} expected reject got OK" >&2
      fail=1
    fi
  }

  record_optional() {
    local id="$1"
    shift
    local line
    set +e
    line="$("$@")"
    set -e
    printf '%s%s\n' "${sep}" "${line}" >> "${RESULTS}.partial"
    sep=","
  }

  record production_defaults 0 env OUT_SMS_ENFORCE_COMM_PIN=0 \
    "${H}" --macho "${macho}" --candidate-id production_defaults --no-enforce-pin

  record production_block_max_80 0 env OUT_SMS_ENFORCE_COMM_PIN=0 OUT_SMS_BLOCK_MAX_A_DELTA=0x80 \
    "${H}" --macho "${macho}" --candidate-id production_block_max_80 --no-enforce-pin

  record legacy_linkedit_bad 1 env OUT_SMS_ENFORCE_COMM_PIN=0 OUT_SMS_LINKEDIT_MAP=mh_dataoff \
    "${H}" --macho "${macho}" --candidate-id legacy_linkedit_bad --no-enforce-pin

  record block_max_tiny 1 env OUT_SMS_ENFORCE_COMM_PIN=0 OUT_SMS_BLOCK_MAX_A_DELTA=4 \
    "${H}" --macho "${macho}" --candidate-id block_max_tiny --no-enforce-pin

  record_optional fnstarts_text_sect env OUT_SMS_ENFORCE_COMM_PIN=0 OUT_SMS_FNSTARTS_BASE=text_sect \
    "${H}" --macho "${macho}" --candidate-id fnstarts_text_sect --no-enforce-pin

  record_optional enforce_pin_uuid env OUT_SMS_ENFORCE_COMM_PIN=1 \
    "${H}" --macho "${macho}" --candidate-id enforce_pin_uuid

  echo ']' >> "${RESULTS}.partial"
  mv "${RESULTS}.partial" "${RESULTS}"
  cp "${RESULTS}" "${LOG}" 2>/dev/null || true

  if [[ "${fail}" -ne 0 ]]; then
    cat "${RESULTS}" >&2
    return 1
  fi
  python3 "${ROOT}/scripts/validate_resolver_matrix.py" "${RESULTS}"
}

main() {
  build_fixture_runner
  if [[ $# -ge 1 && -f "$1" ]]; then
    run_macho_matrix "$1"
  else
    echo '{"matrix":"fixture_only","fixture_tests":"pass","facts":"docs/KNOWN_FACTS.md"}' > "${RESULTS}"
  fi
  echo "resolver matrix OK — ${RESULTS}"
}

main "$@"