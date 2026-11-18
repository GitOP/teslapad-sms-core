#ifndef OUT_SMS_GATE_RESOLVE_H
#define OUT_SMS_GATE_RESOLVE_H

#include "out_sms_gate_core.h"

/* Back-compat for Tweak.x — pins enforced at runtime. */
static inline out_sms_gate_stage_t out_sms_resolve_gate(
    const struct mach_header_64 *mh, intptr_t slide, int uuid_ok, int sha_ok, uint64_t file_size,
    const uint8_t *uuid_expected, out_sms_gate_result_t *res) {
    (void)uuid_expected;
    return out_sms_resolve_gate_ex(mh, slide, file_size, 1, uuid_ok, sha_ok, res);
}

#endif
