# Preflight — TeslaSMSOutBypass 0.2.0

Run **on device** by eng lead after installing the inactive deb (no CommCenter kickstart required for package-only checks):

```bash
JB_PREFIX=/var/jb bash /path/to/(private — device preflight)
```

Output: `/tmp/tesla-sms-out-preflight-summary.json`

## Verify inactive vs loaded-inactive (before Phase 4 Go)

1. Install deb only: `dpkg -i com.gitop.teslapadsmsoutbypass_0.2.0_iphoneos-arm64.deb`
2. **Do not** create `/tmp/tesla-sms-out-bypass-arm` yet.
3. After founder-approved CommCenter kickstart (Phase 4 only):
   - `grep outcome= /tmp/tesla-sms-out-bypass-status` → expect `loaded_inactive`
   - `tail /tmp/tesla-sms-out-bypass-diag.log` → `hook_target_resolved` ok:true, `policy_override_applied` absent
4. To confirm **not** 0.1.0 `skip_inactive` from eng-run arm: do not use `.eng-run` paths; only `/tmp/tesla-sms-out-bypass-arm`.

## Arm path (Phase 4 only — not authorized in Phases 1–3)

```bash
rm -f /tmp/tesla-sms-out-bypass-disable
touch /tmp/tesla-sms-out-bypass-arm
# Within ~1s, status should move to outcome=override_active without CC restart
```

## Rollback hygiene

```bash
touch /tmp/tesla-sms-out-bypass-disable
rm -f /tmp/tesla-sms-out-bypass-arm
dpkg -r com.gitop.teslapadsmsoutbypass
# Prefer CC kickstart after disable sync if patch had been active
