#include "include/out_sms_gate_core.h"

#include <mach-o/loader.h>
#include <string.h>

static uint32_t out_sms_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int64_t out_sms_adrp_page(uint32_t insn, uint64_t pc) {
    uint32_t immlo = (insn >> 29) & 0x3u;
    uint32_t immhi = (insn >> 5) & 0x7ffffu;
    int64_t imm = (int64_t)((immhi << 2) | immlo);
    if (imm & (1 << 20))
        imm |= ~((1LL << 21) - 1);
    return (int64_t)((pc & ~0xfffULL) + (uint64_t)(imm << 12));
}

static int64_t out_sms_add_imm12(uint32_t insn) {
    /* ADD/XD immediate uses unsigned imm12 (not sign-extended). */
    return (int64_t)((insn >> 10) & 0xfffu);
}

static int out_sms_is_adrp(uint32_t insn) {
    return (insn & 0x9f000000u) == 0x90000000u;
}

static int out_sms_is_add_imm(uint32_t insn) {
    return (insn & 0xffc00000u) == 0x91000000u;
}

static int out_sms_is_cond_branch(uint32_t insn) {
    return (insn & 0xff000010u) == 0x54000000u;
}

static int out_sms_is_cbz_cbnz(uint32_t insn) {
    return (insn & 0x7e000000u) == 0x34000000u;
}

static int out_sms_is_tbz_tbnz(uint32_t insn) {
    return (insn & 0x7e000000u) == 0x36000000u;
}

static int out_sms_is_branch_insn(uint32_t insn) {
    return out_sms_is_cond_branch(insn) || out_sms_is_cbz_cbnz(insn) || out_sms_is_tbz_tbnz(insn);
}

static uint64_t out_sms_branch_target(uint32_t insn, uint64_t pc) {
    if (out_sms_is_cond_branch(insn)) {
        int32_t imm = (int32_t)((insn >> 5) & 0x7ffffu);
        if (imm & (1 << 18))
            imm |= ~((1 << 19) - 1);
        return (uint64_t)((int64_t)pc + ((int64_t)imm << 2));
    }
    if (out_sms_is_cbz_cbnz(insn) || out_sms_is_tbz_tbnz(insn)) {
        int32_t imm = (int32_t)((insn >> 5) & 0x7ffffu);
        if (imm & (1 << 18))
            imm |= ~((1 << 19) - 1);
        return (uint64_t)((int64_t)pc + ((int64_t)imm << 2));
    }
    return 0;
}

static size_t out_sms_decode_uleb128(const uint8_t *p, const uint8_t *end, uint64_t *out) {
    uint64_t value = 0;
    size_t shift = 0;
    size_t i = 0;
    while (p + i < end) {
        uint8_t byte = p[i++];
        value |= (uint64_t)(byte & 0x7fu) << shift;
        if ((byte & 0x80u) == 0) {
            *out = value;
            return i;
        }
        shift += 7;
        if (shift > 63)
            return 0;
    }
    return 0;
}

static int out_sms_find_segment(const struct mach_header_64 *mh, const char *segname,
                                struct segment_command_64 *seg_out) {
    const uint8_t *cursor = (const uint8_t *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cursor;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (strncmp(sg->segname, segname, sizeof(sg->segname)) == 0) {
                if (seg_out)
                    *seg_out = *sg;
                return 1;
            }
        }
        cursor += lc->cmdsize;
    }
    return 0;
}

static const uint8_t *out_sms_linkedit_payload(const struct mach_header_64 *mh, intptr_t slide, uint32_t dataoff,
                                               uint32_t datasize, out_sms_linkedit_map_mode_t map_mode,
                                               const uint8_t **end_out) {
    const uint8_t *runtime = NULL;
    if (map_mode == OUT_SMS_LINKEDIT_MAP_MH_DATAOFF) {
        runtime = (const uint8_t *)((const uint8_t *)mh + dataoff);
    } else {
        struct segment_command_64 linkedit;
        if (!out_sms_find_segment(mh, "__LINKEDIT", &linkedit))
            return NULL;
        if (dataoff < linkedit.fileoff || dataoff + datasize < dataoff
            || dataoff + datasize > linkedit.fileoff + linkedit.filesize)
            return NULL;
        runtime =
            (const uint8_t *)(slide + (intptr_t)linkedit.vmaddr + (int64_t)(dataoff - linkedit.fileoff));
    }
    if (!runtime || datasize == 0)
        return NULL;
    if (end_out)
        *end_out = runtime + datasize;
    return runtime;
}

static int out_sms_find_section(const struct mach_header_64 *mh, intptr_t slide, const char *segname,
                                const char *sectname, const uint8_t **body, size_t *size, uintptr_t *va) {
    if (!mh || mh->magic != MH_MAGIC_64)
        return 0;
    const uint8_t *cursor = (const uint8_t *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cursor;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (strncmp(sg->segname, segname, sizeof(sg->segname)) != 0) {
                cursor += lc->cmdsize;
                continue;
            }
            const struct section_64 *sec = (const struct section_64 *)(sg + 1);
            for (uint32_t s = 0; s < sg->nsects; s++) {
                if (!strncmp(sec[s].sectname, sectname, sizeof(sec[s].sectname))) {
                    *va = (uintptr_t)sec[s].addr;
                    *body = (const uint8_t *)(slide + (intptr_t)sec[s].addr);
                    *size = (size_t)sec[s].size;
                    return 1;
                }
            }
        }
        cursor += lc->cmdsize;
    }
    return 0;
}

static uintptr_t out_sms_text_seg_vmaddr(const struct mach_header_64 *mh) {
    struct segment_command_64 text;
    if (out_sms_find_segment(mh, "__TEXT", &text))
        return (uintptr_t)text.vmaddr;
    return 0;
}

static int out_sms_parse_function_starts(const struct mach_header_64 *mh, intptr_t slide, uintptr_t text_va,
                                         size_t text_size, out_sms_fnstarts_base_mode_t base_mode,
                                         out_sms_linkedit_map_mode_t linkedit_map, uintptr_t *starts,
                                         size_t max_starts, size_t *count_out) {
    const uint8_t *cursor = (const uint8_t *)(mh + 1);
    const uint8_t *data = NULL;
    const uint8_t *data_end = NULL;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cursor;
        if (lc->cmd == LC_FUNCTION_STARTS) {
            const struct linkedit_data_command *ld = (const struct linkedit_data_command *)lc;
            data = out_sms_linkedit_payload(mh, slide, ld->dataoff, ld->datasize, linkedit_map, &data_end);
            if (!data)
                return 0;
            break;
        }
        cursor += lc->cmdsize;
    }
    if (!data || !data_end || data_end <= data)
        return 0;

    uintptr_t text_base =
        (base_mode == OUT_SMS_FNSTARTS_BASE_TEXT_SECT) ? text_va : out_sms_text_seg_vmaddr(mh);
    if (!text_base)
        text_base = text_va;

    size_t n = 0;
    uint64_t addr = (uint64_t)text_base;
    const uint8_t *p = data;
    while (p < data_end && n < max_starts) {
        uint64_t delta = 0;
        size_t adv = out_sms_decode_uleb128(p, data_end, &delta);
        if (adv == 0)
            break;
        p += adv;
        if (delta == 0)
            continue;
        addr += delta;
        if (addr >= (uint64_t)text_va && addr < (uint64_t)text_va + text_size)
            starts[n++] = (uintptr_t)addr;
    }
    *count_out = n;
    return (int)(n > 0);
}

static uintptr_t out_sms_fn_bounds(uintptr_t *starts, size_t n, uintptr_t va, uintptr_t text_end,
                                   uintptr_t *fn_end) {
    if (!n)
        return 0;
    for (size_t i = 0; i < n; i++) {
        if (starts[i] <= va && (i + 1 == n || starts[i + 1] > va)) {
            *fn_end = (i + 1 < n) ? starts[i + 1] : text_end;
            return starts[i];
        }
    }
    return 0;
}

static uint32_t out_sms_insn_at(const uint8_t *text, size_t text_size, uintptr_t text_va, uintptr_t va) {
    if (va < text_va || va + 4 > text_va + text_size)
        return 0;
    size_t off = (size_t)(va - text_va);
    if (off + 4 > text_size)
        return 0;
    return out_sms_u32_le(text + off);
}

typedef struct {
    uintptr_t branch_va;
    uintptr_t target;
} out_sms_branch_rec_t;

static size_t out_sms_collect_branch_targets(const uint8_t *text, size_t text_sz, uintptr_t text_va,
                                             uintptr_t fn_start, uintptr_t fn_end,
                                             uintptr_t *targets, size_t max_targets) {
    size_t n = 0;
    for (uintptr_t va = fn_start; va + 4 <= fn_end && va + 4 <= text_va + text_sz; va += 4) {
        uint32_t ins = out_sms_insn_at(text, text_sz, text_va, va);
        if (!out_sms_is_branch_insn(ins))
            continue;
        uintptr_t tgt = (uintptr_t)out_sms_branch_target(ins, (uint64_t)va);
        if (tgt < fn_start || tgt >= fn_end)
            continue;
        if (n < max_targets)
            targets[n++] = tgt;
    }
    return n;
}

static uintptr_t out_sms_block_end_for_target(uintptr_t D, uintptr_t A, uintptr_t fn_end, uintptr_t *targets,
                                              size_t target_count) {
    uintptr_t end = fn_end;
    for (size_t i = 0; i < target_count; i++) {
        uintptr_t T = targets[i];
        if (T <= D)
            continue;
        if (T < end)
            end = T;
    }
  (void)A;
    return end;
}

const char *out_sms_gate_stage_name(out_sms_gate_stage_t s) {
    switch (s) {
    case OUT_SMS_GATE_OK:
        return "TARGET_VALIDATED";
    case OUT_SMS_GATE_IMAGE_MISMATCH:
        return "IMAGE_MISMATCH";
    case OUT_SMS_GATE_STRING_NOT_FOUND:
        return "STRING_NOT_FOUND";
    case OUT_SMS_GATE_STRING_SECTION_UNEXPECTED:
        return "STRING_SECTION_UNEXPECTED";
    case OUT_SMS_GATE_REFERENCE_NOT_FOUND:
        return "REFERENCE_NOT_FOUND";
    case OUT_SMS_GATE_REFERENCE_AMBIGUOUS:
        return "REFERENCE_AMBIGUOUS";
    case OUT_SMS_GATE_FUNCTION_NOT_IDENTIFIED:
        return "FUNCTION_NOT_IDENTIFIED";
    case OUT_SMS_GATE_PATCH_TARGET_CONFLICT:
        return "PATCH_TARGET_CONFLICT";
    case OUT_SMS_GATE_PATCH_BYTES_MISMATCH:
        return "PATCH_BYTES_MISMATCH";
    case OUT_SMS_GATE_HOOK_BACKEND_UNSUPPORTED:
        return "HOOK_BACKEND_UNSUPPORTED";
    default:
        return "unknown";
    }
}

out_sms_gate_stage_t out_sms_resolve_gate_ex(const struct mach_header_64 *mh, intptr_t slide, uint64_t file_size,
                                             int enforce_comm_pin, int uuid_ok, int sha_ok,
                                             out_sms_gate_result_t *res) {
    out_sms_gate_policy_t pol = out_sms_gate_policy_default();
    pol.enforce_comm_pin = enforce_comm_pin ? 1 : 0;
    return out_sms_resolve_gate_policy(mh, slide, file_size, uuid_ok, sha_ok, &pol, res);
}

out_sms_gate_stage_t out_sms_resolve_gate_policy(const struct mach_header_64 *mh, intptr_t slide, uint64_t file_size,
                                                 int uuid_ok, int sha_ok, const out_sms_gate_policy_t *policy,
                                                 out_sms_gate_result_t *res) {
    if (policy && policy->slide_override_set)
        slide = policy->slide_override;

    memset(res, 0, sizeof(*res));
    res->stage = OUT_SMS_GATE_OK;

    uint32_t block_max = policy ? policy->block_max_a_delta : OUT_SMS_GATE_BLOCK_MAX_A_DELTA;
    out_sms_linkedit_map_mode_t linkedit_map =
        policy ? policy->linkedit_map : OUT_SMS_LINKEDIT_MAP_SEGMENT;
    out_sms_fnstarts_base_mode_t fn_base =
        policy ? policy->fnstarts_base : OUT_SMS_FNSTARTS_BASE_TEXT_SEG;

    if (policy && policy->enforce_comm_pin && (!uuid_ok || !sha_ok || file_size != OUT_SMS_EXPECTED_COMM_SIZE)) {
        res->stage = OUT_SMS_GATE_IMAGE_MISMATCH;
        return res->stage;
    }

    const uint8_t *oslog = NULL;
    size_t oslog_sz = 0;
    uintptr_t oslog_va = 0;
    if (!out_sms_find_section(mh, slide, "__TEXT", OUT_SMS_OSLOGSTRING_SECT, &oslog, &oslog_sz, &oslog_va)) {
        res->stage = OUT_SMS_GATE_STRING_NOT_FOUND;
        return res->stage;
    }

    const char *needle = OUT_SMS_LOG_TOKEN;
    size_t needle_len = strlen(needle);
    uintptr_t str_va = 0;
    for (size_t off = 0; off + needle_len + 1 <= oslog_sz; off++) {
        if (memcmp(oslog + off, needle, needle_len) == 0 && oslog[off + needle_len] == '\0') {
            str_va = oslog_va + off;
            break;
        }
    }
    if (!str_va) {
        res->stage = OUT_SMS_GATE_STRING_NOT_FOUND;
        return res->stage;
    }
    res->str_va = str_va;

    const uint8_t *text = NULL;
    size_t text_sz = 0;
    uintptr_t text_va = 0;
    if (!out_sms_find_section(mh, slide, "__TEXT", OUT_SMS_TEXT_SECT, &text, &text_sz, &text_va)) {
        res->stage = OUT_SMS_GATE_STRING_SECTION_UNEXPECTED;
        return res->stage;
    }

    if (str_va < oslog_va || str_va >= oslog_va + oslog_sz) {
        res->stage = OUT_SMS_GATE_STRING_SECTION_UNEXPECTED;
        return res->stage;
    }

    uint32_t ref_count = 0;
    uintptr_t sole_adrp = 0;
    uintptr_t sole_add_end = 0;

    for (size_t off = 0; off + 8 <= text_sz; off += 4) {
        uint32_t a = out_sms_u32_le(text + off);
        if (!out_sms_is_adrp(a))
            continue;
        uint64_t pc = (uint64_t)(text_va + off);
        uint32_t b = out_sms_u32_le(text + off + 4);
        if (!out_sms_is_add_imm(b))
            continue;
        int64_t page = out_sms_adrp_page(a, pc);
        uint64_t target = (uint64_t)(page + out_sms_add_imm12(b));
        if (target != (uint64_t)str_va)
            continue;
        ref_count++;
        sole_adrp = text_va + off;
        sole_add_end = text_va + off + 8;
    }

    res->ref_count = ref_count;
    if (ref_count == 0) {
        res->stage = OUT_SMS_GATE_REFERENCE_NOT_FOUND;
        return res->stage;
    }
    if (ref_count != 1) {
        res->stage = OUT_SMS_GATE_REFERENCE_AMBIGUOUS;
        return res->stage;
    }

    res->call_site_adrp = sole_adrp;
    res->call_site_end = sole_add_end;
    uintptr_t A = sole_adrp;

    /*
     * LOCAL branch-target analysis anchored at the string-reference ADRP
     * site A. Robust: does NOT depend on whole-function bounds from
     * LC_FUNCTION_STARTS (ULEB128 base / zerofill-slide mapping on the real
     * __LINKEDIT yielded wrong bounds -> FUNCTION_NOT_IDENTIFIED). Scan a
     * bounded window around A, clamped to __text, then reuse the same
     * unique-block-head / unique-gate selection below. Fail closed when not
     * unique or when A falls outside the window.
     */
    (void)fn_base;
    (void)linkedit_map;
    (void)out_sms_parse_function_starts; /* retained for whole-function cross-check */
    (void)out_sms_fn_bounds;             /* retained for whole-function cross-check */

    uintptr_t text_end = text_va + text_sz;
    uintptr_t fn_start = (A > text_va + (uintptr_t)OUT_SMS_GATE_LOCAL_SCAN_BACK)
                             ? (A - (uintptr_t)OUT_SMS_GATE_LOCAL_SCAN_BACK)
                             : text_va;
    uintptr_t fn_end = A + (uintptr_t)OUT_SMS_GATE_LOCAL_SCAN_FWD + 4u;
    if (fn_end > text_end)
        fn_end = text_end;
    if (fn_start >= fn_end || A < fn_start || A >= fn_end) {
        res->stage = OUT_SMS_GATE_FUNCTION_NOT_IDENTIFIED;
        return res->stage;
    }
    res->fn_start = fn_start;
    res->fn_end = fn_end;

    uintptr_t branch_targets[512];
    size_t tgt_n = out_sms_collect_branch_targets(text, text_sz, text_va, fn_start, fn_end, branch_targets,
                                                  sizeof(branch_targets) / sizeof(branch_targets[0]));

    uintptr_t lblk_cands[32];
    size_t lblk_n = 0;

    for (size_t t = 0; t < tgt_n; t++) {
        uintptr_t D = branch_targets[t];
        if (D < fn_start || D >= fn_end || D > A)
            continue;
        if ((uint64_t)(A - D) > (uint64_t)block_max)
            continue;
        uintptr_t block_end = out_sms_block_end_for_target(D, A, fn_end, branch_targets, tgt_n);
        if (A < D || A >= block_end)
            continue;
        int dup = 0;
        for (size_t i = 0; i < lblk_n; i++) {
            if (lblk_cands[i] == D) {
                dup = 1;
                break;
            }
        }
        if (!dup && lblk_n < sizeof(lblk_cands) / sizeof(lblk_cands[0]))
            lblk_cands[lblk_n++] = D;
    }

    if (!lblk_n) {
        res->stage = OUT_SMS_GATE_FUNCTION_NOT_IDENTIFIED;
        return res->stage;
    }

    if (lblk_n != 1) {
        res->stage = OUT_SMS_GATE_REFERENCE_AMBIGUOUS;
        res->branch_to_lblk_count = (uint32_t)lblk_n;
        return res->stage;
    }
    uintptr_t lblk = lblk_cands[0];

    uintptr_t gate_va = 0;
    uint32_t gate_hits = 0;
    for (uintptr_t gva = fn_start; gva + 4 <= fn_end && gva + 4 <= text_va + text_sz; gva += 4) {
        uint32_t ins = out_sms_insn_at(text, text_sz, text_va, gva);
        if (!out_sms_is_branch_insn(ins))
            continue;
        if ((uintptr_t)out_sms_branch_target(ins, (uint64_t)gva) != lblk)
            continue;
        gate_hits++;
        gate_va = gva;
    }

    if (gate_hits != 1 || !gate_va) {
        res->stage = (gate_hits > 1) ? OUT_SMS_GATE_REFERENCE_AMBIGUOUS : OUT_SMS_GATE_FUNCTION_NOT_IDENTIFIED;
        res->branch_to_lblk_count = gate_hits;
        return res->stage;
    }

    res->block_head = lblk;
    res->gate_va = gate_va;
    res->branch_to_lblk_count = 1;

    uint32_t gins = out_sms_insn_at(text, text_sz, text_va, gate_va);
    res->gate_insn = gins;

    if (gate_va == OUT_SMS_INBOUND_PATCH_VADDR) {
        res->stage = OUT_SMS_GATE_PATCH_TARGET_CONFLICT;
        return res->stage;
    }

    uint32_t cur = out_sms_u32_le(text + (gate_va - text_va));

    if (cur == OUT_SMS_PATCH_NOP_U32) {
        res->stage = OUT_SMS_GATE_PATCH_TARGET_CONFLICT;
        res->gate_insn = cur;
        return res->stage;
    }

    if (gate_va != OUT_SMS_PATCH_VADDR || cur != OUT_SMS_PATCH_ORIG_U32) {
        res->stage = OUT_SMS_GATE_PATCH_BYTES_MISMATCH;
        res->gate_insn = cur;
        return res->stage;
    }

    res->stage = OUT_SMS_GATE_OK;
    return res->stage;
}