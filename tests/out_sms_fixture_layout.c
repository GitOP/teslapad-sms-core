#include "fixtures/out_sms_gate_macho_fixture.h"
#include "../TeslaSMSOutBypass/include/out_sms_gate_pin.h"

#include <mach-o/loader.h>
#include <stdlib.h>
#include <string.h>

static void enc_cbz_w0(uint8_t *p, uint64_t pc, uint64_t target) {
    uint32_t imm = (uint32_t)((target - pc) / 4);
    uint32_t insn = 0x34000000u | ((imm & 0x7ffffu) << 5);
    memcpy(p, &insn, 4);
}

static void enc_adrp_add(uint8_t *p, uint64_t pc, uint64_t str_va) {
    uint64_t page_pc = pc & ~0xfffULL;
    uint64_t target_page = str_va & ~0xfffULL;
    int64_t delta = (int64_t)((target_page - page_pc) >> 12);
    uint32_t imm = (uint32_t)(delta & ((1 << 21) - 1));
    uint32_t adrp = 0x90000000u | ((imm & 3u) << 29) | ((imm >> 2) << 5) | 3u;
    uint32_t add = 0x91000000u | (((uint32_t)(str_va & 0xfff)) << 10) | (3u << 5) | 3u;
    memcpy(p, &adrp, 4);
    memcpy(p + 4, &add, 4);
}

uint8_t *out_sms_fixture_build_mapped(int use_good_token, int corrupt_fst, size_t *out_len) {
    const uint64_t text_va = 0x100050000ULL;
    const uint64_t oslog_va = 0x1000D0000ULL;
    size_t sz = (size_t)OUT_SMS_FIXTURE_RUNTIME_SIZE;
    uint8_t *mem = calloc(1, sz);
    if (!mem)
        return NULL;

    const uint8_t *cmds = corrupt_fst ? out_sms_fixture_load_cmds_broken : out_sms_fixture_load_cmds;
    size_t cmds_len = corrupt_fst ? out_sms_fixture_load_cmds_broken_len : out_sms_fixture_load_cmds_len;

    struct mach_header_64 mh = {
        .magic = MH_MAGIC_64,
        .cputype = CPU_TYPE_ARM64,
        .cpusubtype = CPU_SUBTYPE_ARM64_ALL,
        .filetype = MH_EXECUTE,
        .ncmds = 6,
        .sizeofcmds = (uint32_t)cmds_len,
        .flags = 0x82000085u,
    };
    memcpy(mem, &mh, sizeof(mh));
    memcpy(mem + sizeof(mh), cmds, cmds_len);

    size_t text_off = (size_t)(text_va - OUT_SMS_FIXTURE_IMAGE_BASE);
    uint32_t gate_insn = OUT_SMS_PATCH_ORIG_U32;
    memcpy(mem + text_off + (OUT_SMS_PATCH_VADDR - text_va), &gate_insn, 4);
    enc_cbz_w0(mem + text_off + (OUT_SMS_PIN_BLOCK_HEAD - text_va), OUT_SMS_PIN_BLOCK_HEAD,
              OUT_SMS_PIN_BLOCK_HEAD + 0x40);
    enc_cbz_w0(mem + text_off + (OUT_SMS_PATCH_VADDR - text_va) + 4, OUT_SMS_PATCH_VADDR + 4, OUT_SMS_PIN_BLOCK_HEAD);
    enc_adrp_add(mem + text_off + (OUT_SMS_PIN_CALL_SITE_ADRP - text_va), OUT_SMS_PIN_CALL_SITE_ADRP,
                 OUT_SMS_PIN_STRING_VADDR);

    const char *tok_good = "#I Sms not allowed on device.";
    const char *tok_bad = "I Sms not allowed on device.";
    const char *tok = use_good_token ? tok_good : tok_bad;
    size_t oslog_off = (size_t)(oslog_va - OUT_SMS_FIXTURE_IMAGE_BASE);
    memcpy(mem + oslog_off + (OUT_SMS_PIN_STRING_VADDR - oslog_va), tok, strlen(tok) + 1);

    if (corrupt_fst) {
        memcpy(mem + OUT_SMS_FIXTURE_FST_LEGACY_OFF, out_sms_fixture_function_starts,
               out_sms_fixture_function_starts_len);
    } else {
        memcpy(mem + OUT_SMS_FIXTURE_LINKEDIT_OFF, out_sms_fixture_function_starts,
               out_sms_fixture_function_starts_len);
    }

    *out_len = sz;
    return mem;
}