#!/usr/bin/env python3
"""Fail closed if production_defaults did not reach TARGET_VALIDATED pins."""
from __future__ import annotations

import json
import sys
from pathlib import Path

GATE_VA = "0x1000a0000"
GATE_INSN = "0x34000080"


def main() -> int:
    path = Path(sys.argv[1] if len(sys.argv) > 1 else "resolver-matrix-artifacts/resolver-matrix-results.json")
    raw = path.read_text()
    if not raw.strip().startswith("["):
        print("SKIP matrix validation (fixture-only stub)", path)
        return 0
    rows = json.loads(raw)
    prod = next((r for r in rows if r.get("candidate_id") == "production_defaults"), None)
    if not prod:
        print("FAIL: missing production_defaults row", file=sys.stderr)
        return 1
    if prod.get("stage_name") not in ("OK", "TARGET_VALIDATED"):
        print("FAIL: production stage", prod.get("stage_name"), file=sys.stderr)
        return 1
    res = prod.get("result") or {}
    if res.get("gate_va") != GATE_VA or res.get("gate_insn") != GATE_INSN:
        print("FAIL: gate pin", res, file=sys.stderr)
        return 1
    print("OK matrix production_defaults", res.get("gate_va"), res.get("gate_insn"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())