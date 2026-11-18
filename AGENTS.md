# Agent policy — teslapad-sms-core

- **No real CommCenter UUID/sha256/VAs** in public commits; placeholders in `out_sms_gate_pin.h` / `patch_pin.h`.
- **Required CI:** `resolver-validate` runs compiled resolver on synthetic `runtime_image.bin`; must reach `TARGET_VALIDATED` with placeholder gate pins.
- **Device arm/SMS/SSH:** private repo only. Do not iterate on hosted CI from agents unless asked.