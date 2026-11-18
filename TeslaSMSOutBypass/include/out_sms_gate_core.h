#ifndef OUT_SMS_GATE_CORE_H
#define OUT_SMS_GATE_CORE_H

#include <stddef.h>
#include <stdint.h>

#include "out_sms_experiment.h"
#include "out_sms_gate_policy.h"

/* Types shared with device tweak (also declared in out_sms_gate_resolve.h). */
#ifndef OUT_SMS_GATE_RESOLVE_TYPES
#define OUT_SMS_GATE_RESOLVE_TYPES
typedef enum {
    OUT_SMS_GATE_OK = 0,
    OUT_SMS_GATE_IMAGE_MISMATCH,
    OUT_SMS_GATE_STRING_NOT_FOUND,
    OUT_SMS_GATE_STRING_SECTION_UNEXPECTED,
    OUT_SMS_GATE_REFERENCE_NOT_FOUND,
    OUT_SMS_GATE_REFERENCE_AMBIGUOUS,
    OUT_SMS_GATE_FUNCTION_NOT_IDENTIFIED,
    OUT_SMS_GATE_PATCH_TARGET_CONFLICT,
    OUT_SMS_GATE_PATCH_BYTES_MISMATCH,
    OUT_SMS_GATE_HOOK_BACKEND_UNSUPPORTED,
} out_sms_gate_stage_t;

typedef struct {
    out_sms_gate_stage_t stage;
    uintptr_t str_va;
    uintptr_t call_site_adrp;
    uintptr_t call_site_end;
    uintptr_t block_head;
    uintptr_t gate_va;
    uint32_t gate_insn;
    uintptr_t fn_start;
    uintptr_t fn_end;
    uint32_t ref_count;
    uint32_t branch_to_lblk_count;
} out_sms_gate_result_t;
#endif

struct mach_header_64;

const char *out_sms_gate_stage_name(out_sms_gate_stage_t s);

/* Production entry (default policy) — same path the CommCenter tweak uses. */
out_sms_gate_stage_t out_sms_resolve_gate_ex(const struct mach_header_64 *mh, intptr_t slide, uint64_t file_size,
                                             int enforce_comm_pin, int uuid_ok, int sha_ok,
                                             out_sms_gate_result_t *res);

/* Harness / matrix: runtime policy (shared implementation). */
out_sms_gate_stage_t out_sms_resolve_gate_policy(const struct mach_header_64 *mh, intptr_t slide, uint64_t file_size,
                                                 int uuid_ok, int sha_ok, const out_sms_gate_policy_t *policy,
                                                 out_sms_gate_result_t *res);

#endif
