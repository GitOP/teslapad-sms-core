#include "../TeslaSMSOutBypass/include/out_sms_gate_core.h"
#include "../TeslaSMSOutBypass/include/out_sms_gate_pin.h"

#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMAGE_BASE 0x100000000ULL

static uint8_t *load_runtime(const char *root, size_t *out_len) {
    char path[512];
    snprintf(path, sizeof path, "%s/synth-fixture/runtime_image.bin", root);
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *image = calloc(1, (size_t)n);
    if (!image)
        return NULL;
    fread(image, 1, (size_t)n, f);
    fclose(f);
    *out_len = (size_t)n;
    return image;
}

int main(int argc, char **argv) {
    const char *root = (argc > 1) ? argv[1] : ".";
    size_t len = 0;
    int fail = 0;
    uint8_t *mem = load_runtime(root, &len);
    if (!mem)
        return 1;
    intptr_t slide = (intptr_t)mem - (intptr_t)IMAGE_BASE;
    out_sms_gate_result_t gr;
    out_sms_gate_stage_t st = out_sms_resolve_gate_ex((struct mach_header_64 *)mem, slide, 0, 0, 1, 1, &gr);
    if (st != OUT_SMS_GATE_OK || gr.gate_va != OUT_SMS_PIN_PLACEHOLDER_GATE_VA)
        fail++;
    size_t off = (size_t)((0x1000D0000ULL - IMAGE_BASE) + (OUT_SMS_PIN_STRING_VADDR - 0x1000D0000ULL));
    memcpy(mem + off, "I Sms not allowed on device.", 28);
    st = out_sms_resolve_gate_ex((struct mach_header_64 *)mem, slide, 0, 0, 1, 1, &gr);
    if (st != OUT_SMS_GATE_STRING_NOT_FOUND)
        fail++;
    free(mem);
    if (fail) {
        fprintf(stderr, "%d resolver test(s) failed\n", fail);
        return 1;
    }
    puts("out_sms_gate_resolver tests passed");
    return 0;
}