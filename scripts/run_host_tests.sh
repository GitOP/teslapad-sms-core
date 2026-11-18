#!/usr/bin/env bash
# Public CI entry: synthetic fixtures + resolver execution (no Apple bytes).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export CFLAGS="-O2 -Wall -Wextra -std=c11 -I${ROOT}/compat -I${ROOT} -I${ROOT}/TeslaSMSOutBypass -I${ROOT}/TeslaSMSOutBypass/include -I${ROOT}/tools -I${ROOT}/tests"
cd "$ROOT"
python3 scripts/gen_synth_macho_fixture.py "${ROOT}/synth-fixture"
cc $CFLAGS tests/test_patch_logic.c -o /tmp/test_patch_logic && /tmp/test_patch_logic
cc $CFLAGS tests/test_out_sms_experiment_logic.c -o /tmp/test_out_sms_experiment_logic && /tmp/test_out_sms_experiment_logic
cc $CFLAGS tests/test_out_sms_gate_resolver.c TeslaSMSOutBypass/src/out_sms_gate_core.c -o /tmp/test_out_sms_gate_resolver && /tmp/test_out_sms_gate_resolver "${ROOT}"
cc $CFLAGS synth-fixture/fixture_validate.c TeslaSMSOutBypass/src/out_sms_gate_core.c tools/out_sms_macho_map.c -o /tmp/fixture_validate
/tmp/fixture_validate "${ROOT}/synth-fixture"
bash scripts/run_resolver_matrix.sh
bash scripts/sms-out-rollback-dryrun.sh
echo "host tests OK"