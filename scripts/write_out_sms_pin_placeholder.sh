#!/usr/bin/env bash
# Public CI / package build: placeholder pin only (non-device-armed).
set -euo pipefail
OUT="${1:-TeslaSMSOutBypass/include/out_sms_pin_generated.h}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Pin header already matches out_sms_gate_pin.h placeholders — generate explicit include wrapper.
cat >"${OUT}" <<'EOF'
#ifndef OUT_SMS_PIN_GENERATED_H
#define OUT_SMS_PIN_GENERATED_H
#include "out_sms_gate_pin.h"
#endif
EOF
echo "Wrote placeholder pin wrapper: ${OUT}"