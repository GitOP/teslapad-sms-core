#include "../TeslaSMSOutBypass/include/out_sms_gate_core.h"
#include "../TeslaSMSOutBypass/include/out_sms_gate_pin.h"

#include <stdio.h>
#include <stdlib.h>

#define IMAGE_BASE 0x100000000ULL

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : ".";
    char path[512];
    snprintf(path, sizeof path, "%s/runtime_image.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *image = calloc(1, (size_t)n);
    if (!image || fread(image, 1, (size_t)n, f) != (size_t)n)
        return 2;
    fclose(f);
    intptr_t slide = (intptr_t)image - (intptr_t)IMAGE_BASE;
    out_sms_gate_policy_t pol = out_sms_gate_policy_default();
    pol.enforce_comm_pin = 0;
    out_sms_gate_result_t gr;
    out_sms_gate_stage_t st = out_sms_resolve_gate_policy(
        (struct mach_header_64 *)image, slide, OUT_SMS_EXPECTED_COMM_SIZE,
        1 /* uuid handled in image */, 0, &pol, &gr);
    printf("{\"stage\":%d,\"stage_name\":\"%s\",\"gate_va\":\"0x%llx\",\"block_head\":\"0x%llx\",\"gate_insn\":\"0x%x\"}\n",
           (int)st, out_sms_gate_stage_name(st), (unsigned long long)gr.gate_va,
           (unsigned long long)gr.block_head, gr.gate_insn);
    free(image);
    return (st == OUT_SMS_GATE_OK && gr.gate_va == OUT_SMS_PIN_PLACEHOLDER_GATE_VA
            && gr.block_head == OUT_SMS_PIN_PLACEHOLDER_BLOCK_HEAD && gr.gate_insn == OUT_SMS_PATCH_ORIG_U32)
               ? 0
               : 1;
}