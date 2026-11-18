#!/usr/bin/env bash
# Inject a PRIVATE pin file into out_sms_pin_generated.h (never commit the pin file).
set -euo pipefail

PIN_FILE="${1:-}"
OUT="${2:-TeslaSMSOutBypass/include/out_sms_pin_generated.h}"

die() { echo "inject_real_pin: $*" >&2; exit 1; }

[[ -n "${PIN_FILE}" && -f "${PIN_FILE}" && -r "${PIN_FILE}" ]] || die "missing or unreadable pin file"

# shellcheck disable=SC1090
source "${PIN_FILE}"

req=(OUT_SMS_PATCH_VADDR OUT_SMS_PATCH_ORIG_U32 OUT_SMS_PATCH_NOP_U32
     OUT_SMS_PIN_STRING_VADDR OUT_SMS_PIN_CALL_SITE_ADRP OUT_SMS_PIN_BLOCK_HEAD
     OUT_SMS_EXPECTED_COMM_SIZE)
for k in "${req[@]}"; do
  [[ -n "${!k:-}" ]] || die "required field ${k} missing in ${PIN_FILE}"
done

mkdir -p "$(dirname "${OUT}")"
cat >"${OUT}" <<EOF
/* AUTO-GENERATED from private pin — do not commit. */
#ifndef OUT_SMS_PIN_GENERATED_H
#define OUT_SMS_PIN_GENERATED_H
#define OUT_SMS_PATCH_VADDR      ${OUT_SMS_PATCH_VADDR}ULL
#define OUT_SMS_PATCH_ORIG_U32     ${OUT_SMS_PATCH_ORIG_U32}u
#define OUT_SMS_PATCH_NOP_U32      ${OUT_SMS_PATCH_NOP_U32:-0xd503201fu}u
#define OUT_SMS_PIN_STRING_VADDR   ${OUT_SMS_PIN_STRING_VADDR}ULL
#define OUT_SMS_PIN_CALL_SITE_ADRP ${OUT_SMS_PIN_CALL_SITE_ADRP}ULL
#define OUT_SMS_PIN_BLOCK_HEAD     ${OUT_SMS_PIN_BLOCK_HEAD}ULL
#define OUT_SMS_EXPECTED_COMM_SIZE ${OUT_SMS_EXPECTED_COMM_SIZE}u
#endif
EOF
echo "Wrote ${OUT} from private pin"