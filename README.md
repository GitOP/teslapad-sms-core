# teslapad-sms-core

Public **build + test** tree for CommCenter SMS research. This repo ships **placeholder pins** and **synthetic** Mach-O fixtures only.

- CI: `.github/workflows/ci.yml` (ubuntu resolver execution + macos Theos placeholder packages)
- Synthetic fixtures: `scripts/gen_synth_macho_fixture.py` → `synth-fixture/runtime_image.bin` (generated at test time, not committed)
- Private device pin: `scripts/inject_real_pin.sh` + a gitignored pin file (never commit real fingerprints)

```bash
bash scripts/run_host_tests.sh
```