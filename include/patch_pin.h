#ifndef PATCH_PIN_H
#define PATCH_PIN_H

#include <stdint.h>
#include <string.h>

/* Public placeholder inbound pin — inject real pin privately before device builds. */
#define PATCH_PIN_IMAGE_BASE       0x100000000ULL
#define PATCH_PIN_VADDR            0x1000cafe1ULL
#define PATCH_PIN_FILE_OFFSET      (PATCH_PIN_VADDR - PATCH_PIN_IMAGE_BASE)
#define PATCH_PIN_ORIG_U32         0xcafe0001u
#define PATCH_PIN_NOP_U32          0xd503201fu

#define PATCH_PIN_UUID_0  0xde
#define PATCH_PIN_UUID_1  0xad
#define PATCH_PIN_UUID_2  0xbe
#define PATCH_PIN_UUID_3  0xef
#define PATCH_PIN_UUID_4  0x00
#define PATCH_PIN_UUID_5  0x00
#define PATCH_PIN_UUID_6  0x00
#define PATCH_PIN_UUID_7  0x01
#define PATCH_PIN_UUID_8  0x00
#define PATCH_PIN_UUID_9  0x00
#define PATCH_PIN_UUID_10 0x00
#define PATCH_PIN_UUID_11 0x00
#define PATCH_PIN_UUID_12 0x00
#define PATCH_PIN_UUID_13 0x00
#define PATCH_PIN_UUID_14 0x00
#define PATCH_PIN_UUID_15 0x00

static const char PATCH_PIN_EXPECTED_SHA256[] =
    "0000000000000000000000000000000000000000000000000000000000000000";

static const uint8_t PATCH_PIN_EXPECTED_UUID[16] = {
    PATCH_PIN_UUID_0,  PATCH_PIN_UUID_1,  PATCH_PIN_UUID_2,  PATCH_PIN_UUID_3,
    PATCH_PIN_UUID_4,  PATCH_PIN_UUID_5,  PATCH_PIN_UUID_6,  PATCH_PIN_UUID_7,
    PATCH_PIN_UUID_8,  PATCH_PIN_UUID_9,  PATCH_PIN_UUID_10, PATCH_PIN_UUID_11,
    PATCH_PIN_UUID_12, PATCH_PIN_UUID_13, PATCH_PIN_UUID_14, PATCH_PIN_UUID_15
};

static inline uintptr_t patch_pin_runtime_target(uintptr_t slide) {
    return slide + (uintptr_t)PATCH_PIN_VADDR;
}

#define SMS_TWEAK_STATUS_PATCH_OK         "patch_ok"
#define SMS_TWEAK_STATUS_SKIP_KILLSWITCH  "skip_killswitch"
#define SMS_TWEAK_STATUS_SKIP_PIN         "skip_pin"
#define SMS_TWEAK_STATUS_SKIP_SHA         "skip_sha"
#define SMS_TWEAK_STATUS_PATCH_FAIL       "patch_fail"

static inline int sms_tweak_status_line_valid(const char *outcome) {
    return (strcmp(outcome, SMS_TWEAK_STATUS_PATCH_OK) == 0
            || strcmp(outcome, SMS_TWEAK_STATUS_SKIP_KILLSWITCH) == 0
            || strcmp(outcome, SMS_TWEAK_STATUS_SKIP_PIN) == 0
            || strcmp(outcome, SMS_TWEAK_STATUS_SKIP_SHA) == 0
            || strcmp(outcome, SMS_TWEAK_STATUS_PATCH_FAIL) == 0);
}

static inline int patch_pin_uuid_matches(const uint8_t *uuid) {
    for (int i = 0; i < 16; i++) {
        if (uuid[i] != PATCH_PIN_EXPECTED_UUID[i])
            return 0;
    }
    return 1;
}

#endif