# KNOWN_FACTS (public)

Methodology notes only. **Device-specific CommCenter fingerprints live in a private pin file** (`scripts/inject_real_pin.sh`); this public tree uses **placeholder** VAs/UUID/sha in `out_sms_gate_pin.h` and `patch_pin.h`.

## Resolver (0.2.4+)

- Shared core: `TeslaSMSOutBypass/src/out_sms_gate_core.c`
- Gate resolution uses **local branch-target scan** around the unique `#…` os_log token anchor (not whole-function LC_FUNCTION_STARTS on the hot path).
- Mach-O mapper: `fh+1` load-command walk; skip `filesize==0` segments; fail-closed on truncated file extents.

## CI proof (synthetic)

- `scripts/gen_synth_macho_fixture.py` builds structurally-faithful **synthetic** Mach-O windows (no Apple bytes).
- `scripts/run_host_tests.sh` compiles and runs the resolver; must reach **`TARGET_VALIDATED`** on the placeholder pin layout.

## Packages

| Package | Version | Public default |
| --- | --- | --- |
| `com.gitop.teslapadsmsoutbypass` | 0.2.4 | Inactive / placeholder pin |
| `com.gitop.teslapadsmsbypass` | 1.0.2 | Placeholder inbound pin |