Watchdog staging (inactive by default).

Files in this directory are NOT registered with launchd at package install.
Stage D activation copies com.gitop.smsoutbypass-watchdog.plist into
/var/jb/Library/LaunchDaemons/ and bootstrap loads it — only after founder Go.

Kill switch: /var/mobile/.eng-run/sms-out-bypass-disable (presence disables hook and stops watchdog work usefully only after load).
Arm file: /var/mobile/.eng-run/sms-out-bypass-arm (required for hook + watchdog).
