#include "../include/patch_pin.h"

#include <stdio.h>
#include <string.h>

static int expect_eq_ulong(unsigned long a, unsigned long b, const char *label) {
    if (a != b) {
        fprintf(stderr, "FAIL %s: expected 0x%lx got 0x%lx\n", label, b, a);
        return 1;
    }
    return 0;
}

static int expect_eq_u32(uint32_t a, uint32_t b, const char *label) {
    if (a != b) {
        fprintf(stderr, "FAIL %s: expected 0x%08x got 0x%08x\n", label, b, a);
        return 1;
    }
    return 0;
}

static int test_offsets_and_patch_bytes(void) {
    int failures = 0;
    failures += expect_eq_ulong(PATCH_PIN_FILE_OFFSET, (unsigned long)(PATCH_PIN_VADDR - PATCH_PIN_IMAGE_BASE),
                                "file offset");
    failures += expect_eq_u32(PATCH_PIN_ORIG_U32, 0xcafe0001u, "orig insn");
    failures += expect_eq_u32(PATCH_PIN_NOP_U32, 0xd503201fu, "aarch64 NOP");
    return failures;
}

int main(void) {
    int failures = 0;
    failures += test_offsets_and_patch_bytes();
    failures += patch_pin_uuid_matches(PATCH_PIN_EXPECTED_UUID) ? 0 : 1;
    if (failures != 0) {
        fprintf(stderr, "%d test group(s) failed\n", failures);
        return 1;
    }
    puts("patch_pin host tests passed");
    return 0;
}