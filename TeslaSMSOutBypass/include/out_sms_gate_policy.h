#ifndef OUT_SMS_GATE_POLICY_H
#define OUT_SMS_GATE_POLICY_H

#include <stdint.h>

#include "out_sms_experiment.h"

typedef enum {
    OUT_SMS_LINKEDIT_MAP_SEGMENT = 0,
    OUT_SMS_LINKEDIT_MAP_MH_DATAOFF, /* matrix negative — not used in tweak */
} out_sms_linkedit_map_mode_t;

typedef enum {
    OUT_SMS_FNSTARTS_BASE_TEXT_SEG = 0,
    OUT_SMS_FNSTARTS_BASE_TEXT_SECT,
} out_sms_fnstarts_base_mode_t;

typedef struct {
    uint32_t block_max_a_delta;
    out_sms_linkedit_map_mode_t linkedit_map;
    out_sms_fnstarts_base_mode_t fnstarts_base;
    int slide_override_set;
    intptr_t slide_override;
    int enforce_comm_pin;
} out_sms_gate_policy_t;

static inline out_sms_gate_policy_t out_sms_gate_policy_default(void) {
    out_sms_gate_policy_t p;
    p.block_max_a_delta = OUT_SMS_GATE_BLOCK_MAX_A_DELTA;
    p.linkedit_map = OUT_SMS_LINKEDIT_MAP_SEGMENT;
    p.fnstarts_base = OUT_SMS_FNSTARTS_BASE_TEXT_SEG;
    p.slide_override_set = 0;
    p.slide_override = 0;
    p.enforce_comm_pin = 1;
    return p;
}

#endif
