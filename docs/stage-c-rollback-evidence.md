# Stage C — rollback dry-run evidence (0.2.x watchdog)

Generated: 2026-11-18T03:01:17Z

## Method
- SMS_OUT_BYPASS_DRY_RUN=1, FAKE_HEALTH_FAIL=1, grace_sec=1
- Control paths: /tmp/tesla-sms-out-bypass-{arm,disable}

## Result
- Exit code: 2 (expected 2)

## Log
```
2026-11-18T03:01:15Z watchdog_start window_sec=5 grace_sec=1
2026-11-18T03:01:15Z health_fail_ignored_in_grace
2026-11-18T03:01:16Z health_fail streak=1
2026-11-18T03:01:17Z health_fail streak=2
rollback_at=2026-11-18T03:01:17Z
reason=health_check_failed
action=disable_sentinel_and_optional_dpkg_remove
```
