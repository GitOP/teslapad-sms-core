#ifndef OUT_SMS_EXPERIMENT_H
#define OUT_SMS_EXPERIMENT_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "../../include/patch_pin.h"
#include "out_sms_gate_pin.h"

/* os_log token in __TEXT.__oslogstring (leading '#' is anchor). */
#define OUT_SMS_LOG_TOKEN              "#I Sms not allowed on device."
#define OUT_SMS_OSLOGSTRING_SECT       "__oslogstring"
#define OUT_SMS_TEXT_SECT              "__text"

#define OUT_SMS_ARM_PATH_PRIMARY       "/tmp/tesla-sms-out-bypass-arm"
#define OUT_SMS_DISABLE_PATH_PRIMARY   "/tmp/tesla-sms-out-bypass-disable"
#define OUT_SMS_VALIDATE_PATH_PRIMARY  "/tmp/tesla-sms-out-bypass-validate"
#define OUT_SMS_INBOUND_DISABLE_PATH   "/var/mobile/.eng-run/sms-tweak-disable"

#define OUT_SMS_ARM_PATH_TMP           OUT_SMS_ARM_PATH_PRIMARY
#define OUT_SMS_ARM_PATH_ENG           "/var/mobile/.eng-run/sms-out-bypass-arm"
#define OUT_SMS_DISABLE_PATH_ENG       "/var/mobile/.eng-run/sms-out-bypass-disable"

#define OUT_SMS_STATUS_PATH            "/tmp/tesla-sms-out-bypass-status"
#define OUT_SMS_DIAG_PATH              "/tmp/tesla-sms-out-bypass-diag.log"
#define OUT_SMS_CRASH_STATE_PATH       "/tmp/tesla-sms-out-bypass-crash-state"
#define OUT_SMS_MAX_CRASHES            2
#define OUT_SMS_SYNC_POLL_SEC          1

#define OUT_SMS_INBOUND_PATCH_VADDR    PATCH_PIN_VADDR
#define OUT_SMS_GATE_BLOCK_MAX_A_DELTA 0x40u
/* Local branch-target analysis window around the string-ref ADRP site A
 * (gate sits ~0x448 before A). Window must span back far enough to include
 * the gate; keep it bounded so resolution stays local and fail-closed. */
#define OUT_SMS_GATE_LOCAL_SCAN_BACK   0x800u
#define OUT_SMS_GATE_LOCAL_SCAN_FWD    0x80u

#define OUT_SMS_STATUS_SKIP_INACTIVE       "skip_inactive"
#define OUT_SMS_STATUS_LOADED_INACTIVE     "loaded_inactive"
#define OUT_SMS_STATUS_OVERRIDE_ACTIVE     "override_active"
#define OUT_SMS_STATUS_SKIP_KILLSWITCH     "skip_killswitch"
#define OUT_SMS_STATUS_SKIP_PIN            "skip_pin"
#define OUT_SMS_STATUS_SKIP_SHA            "skip_sha"
#define OUT_SMS_STATUS_PATCH_FAIL          "patch_fail"
#define OUT_SMS_STATUS_PATCH_OK            "patch_ok"

#define OUT_SMS_REASON_TARGET_VALIDATED    "target_validated"
#define OUT_SMS_REASON_TARGET_VALIDATED_UP "TARGET_VALIDATED"

static inline int out_sms_status_line_valid(const char *outcome) {
    return (strcmp(outcome, OUT_SMS_STATUS_SKIP_INACTIVE) == 0
            || strcmp(outcome, OUT_SMS_STATUS_LOADED_INACTIVE) == 0
            || strcmp(outcome, OUT_SMS_STATUS_OVERRIDE_ACTIVE) == 0
            || strcmp(outcome, OUT_SMS_STATUS_SKIP_KILLSWITCH) == 0
            || strcmp(outcome, OUT_SMS_STATUS_SKIP_PIN) == 0
            || strcmp(outcome, OUT_SMS_STATUS_SKIP_SHA) == 0
            || strcmp(outcome, OUT_SMS_STATUS_PATCH_FAIL) == 0
            || strcmp(outcome, OUT_SMS_STATUS_PATCH_OK) == 0);
}

#endif
