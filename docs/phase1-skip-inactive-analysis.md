# Phase 1 — `skip_inactive` offline investigation (0.1.0 live attempt)

**Scope:** Offline analysis of sources + founder capture (~17:18 ET, 2026-11-17). No device access by agent.

## 1. Exact code path that wrote `skip_inactive` (0.1.0)

| Step | Location | Behavior |
| --- | --- | --- |
| 1 | `TeslaSMSOutBypass/Tweak.x` `%ctor` | Early gate before pin/patch |
| 2 | `experiment_armed()` | `lstat` on `OUT_SMS_ARM_PATH_ENG` then `OUT_SMS_ARM_PATH_TMP` |
| 3 | If both fail → | `write_status(OUT_SMS_STATUS_SKIP_INACTIVE)` → `/tmp/tesla-sms-out-bypass-status` |
| 4 | `return` | No branch resolution, no `MSHookMemory`, no patch |

0.1.0 only checked **`.eng-run` path before `/tmp`** (`experiment_armed` order in v0.1.0: eng first, then tmp).

## 2. Arm check timing

**Confirmed: ctor-only in 0.1.0.** `experiment_armed()` ran once at CommCenter load. There was no timer, no send-path hook, and no re-read after `touch` of the arm file without a new CommCenter process.

Therefore, arming **after** `kickstart` in the same CommCenter lifetime would not re-enter `%ctor`; arming **before** kickstart but with failed `lstat` still yielded `skip_inactive` at ctor time.

## 3. Was any hook/patch installed on the live attempt?

**Confirmed: no.** `skip_inactive` is written only on the early-return path **before** `apply_pinned_patch` / `apply_discovery_patch`. Status never reached `patch_ok`, `patch_ok_discovery`, or `patch_fail`.

So CommCenter behavior for outbound policy was **unchanged** by the tweak during that run (only dylib load side effects from `%ctor`, crash handlers, and status file write).

## 4. `/tmp` fallback semantics (0.1.0)

Header defined:

- `OUT_SMS_ARM_PATH_ENG` = `/var/mobile/.eng-run/sms-out-bypass-arm`
- `OUT_SMS_ARM_PATH_TMP` = `/tmp/tesla-sms-out-bypass-arm`

Code checked **eng-run first**; only if eng missing checked `/tmp`. Founder placed arm under `.eng-run` only.

**Hypothesis consistent with code:** CommCenter UID `_wireless` (member of group `mobile`, not owner `mobile`) hits directory `drwx------` on `/var/mobile/.eng-run` → `lstat` returns `EACCES` (13) or traversal failure → eng path “not armed”. If `.eng-run` was checked and failed, tmp was still evaluated — unless eng check incorrectly succeeded? For `lstat` on file inside inaccessible dir, errno typically **EACCES** on the directory.

If founder only created `.eng-run` arm and never `/tmp/tesla-sms-out-bypass-arm`, and eng path failed, **both** would fail → `skip_inactive`.

## 5. Process identity and filesystem implications

| Actor | Typical UID | `.eng-run` (0700 mobile) | `/tmp` (1777 sticky) |
| --- | --- | --- | --- |
| CommCenter | `_wireless` | Cannot traverse (not owner) | Can `lstat` files created by other UIDs |
| `mobile` / eng SSH | mobile | Can create arm | Can create arm |
| Arbitrary app | varies | No | Can create world-writable names (mitigated: eng uses dedicated filenames + preflight) |

**0.2.0 decision:** primary control files **only** under `/tmp/tesla-sms-out-bypass-{arm,disable}`. Deprecated `.eng-run` paths are **not** read by the tweak (documented for migration). Watchdog uses the same `/tmp` paths.

Trade-off: `/tmp` is world-writable; mitigations are obscure filenames, eng-lead-only workflow, kill-switch, and short experiment window with watchdog.

## 6. Watchdog premature rollback (0.1.0)

**Confirmed: yes.** `watchdog/sms-out-bypass-watchdog.sh` (0.1.0):

- `HEALTH_SCRIPT` checks `pdp_ip0` IPv4 only
- No grace period — **first** poll failure triggers `rollback("health_check_failed")` → touch disable + `dpkg -r`
- Founder observed ~90s LTE blip → rollback ~3 min after arm

**0.2.0 fix:** `SMS_OUT_BYPASS_HEALTH_GRACE_SEC` default **180** — health failures ignored until grace elapses; require **2 consecutive** failures after grace before rollback.

Also 0.1.0 watchdog used `.eng-run` arm/disable paths (mismatched with CommCenter-readable arm).

## 7. Design delta in 0.2.0 (Phase 2 summary)

- Resolve policy branch site at ctor **without** requiring arm; status `loaded_inactive` when disarmed.
- **1s timer** re-reads arm/kill and toggles reversible NOP ↔ original `B.cond` (arm/disarm without CommCenter restart once dyld load has occurred; override engages on next policy branch execution).
- Kill switch forces restore of original instruction immediately on next timer tick.
- Diagnostics: `/tmp/tesla-sms-out-bypass-diag.log` JSON lines (`tweak_loaded`, `hook_target_resolved`, `arm_killswitch_poll`, `policy_override_applied`, …).
- **Cannot** observe `original_policy_result` or per-send `hook_invoked` without a function hook — we patch a branch in place (documented in diag `note`).

## 8. Memory patch persistence after `dpkg -r`

A NOP applied in CommCenter `__TEXT` **persists in the running process** after package removal until CommCenter restarts. **Before** `dpkg -r`, eng lead must touch `/tmp/tesla-sms-out-bypass-disable` (tweak restores branch on next sync) or accept risk until kickstart. Watchdog `dpkg -r` also touches disable sentinel first in 0.2.0.