#include "../TeslaSMSOutBypass/include/out_sms_gate_core.h"
#include "../TeslaSMSOutBypass/include/out_sms_gate_pin.h"
#include "../include/patch_pin.h"
#include "out_sms_macho_map.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
static int out_sms_sha256_file(const char *path, char hex_out[65]) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        CC_SHA256_Update(&ctx, buf, (CC_LONG)n);
    fclose(f);
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);
    static const char *h = "0123456789abcdef";
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
        hex_out[i * 2] = h[digest[i] >> 4];
        hex_out[i * 2 + 1] = h[digest[i] & 0xf];
    }
    hex_out[64] = '\0';
    return 1;
}
#else
/* Portable SHA-256 (public domain, RFC 6234) so the host/Linux harness computes
 * a REAL digest and pin enforcement truly passes on the pinned image (no stub). */
typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} out_sms_sha256_ctx;

static uint32_t out_sms_ror(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static void out_sms_sha256_transform(out_sms_sha256_ctx *c, const uint8_t d[64]) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t m[64], a,b,e_,f,g,h,aa,t1,t2;
    for (uint32_t i = 0, j = 0; i < 16; i++, j += 4)
        m[i] = ((uint32_t)d[j] << 24) | ((uint32_t)d[j+1] << 16) | ((uint32_t)d[j+2] << 8) | (uint32_t)d[j+3];
    for (uint32_t i = 16; i < 64; i++) {
        uint32_t s0 = out_sms_ror(m[i-15],7) ^ out_sms_ror(m[i-15],18) ^ (m[i-15] >> 3);
        uint32_t s1 = out_sms_ror(m[i-2],17) ^ out_sms_ror(m[i-2],19) ^ (m[i-2] >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }
    a=c->state[0]; b=c->state[1]; aa=c->state[2]; t2=c->state[3];
    e_=c->state[4]; f=c->state[5]; g=c->state[6]; h=c->state[7];
    {
      uint32_t cc=aa, dd=t2;
      for (uint32_t i = 0; i < 64; i++) {
        uint32_t S1 = out_sms_ror(e_,6) ^ out_sms_ror(e_,11) ^ out_sms_ror(e_,25);
        uint32_t ch = (e_ & f) ^ (~e_ & g);
        t1 = h + S1 + ch + K[i] + m[i];
        uint32_t S0 = out_sms_ror(a,2) ^ out_sms_ror(a,13) ^ out_sms_ror(a,22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        t2 = S0 + maj;
        h=g; g=f; f=e_; e_=dd + t1; dd=cc; cc=b; b=a; a=t1 + t2;
      }
      c->state[0]+=a; c->state[1]+=b; c->state[2]+=cc; c->state[3]+=dd;
      c->state[4]+=e_; c->state[5]+=f; c->state[6]+=g; c->state[7]+=h;
    }
}

static void out_sms_sha256_init(out_sms_sha256_ctx *c) {
    c->datalen = 0; c->bitlen = 0;
    c->state[0]=0x6a09e667; c->state[1]=0xbb67ae85; c->state[2]=0x3c6ef372; c->state[3]=0xa54ff53a;
    c->state[4]=0x510e527f; c->state[5]=0x9b05688c; c->state[6]=0x1f83d9ab; c->state[7]=0x5be0cd19;
}

static void out_sms_sha256_update(out_sms_sha256_ctx *c, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        c->data[c->datalen++] = data[i];
        if (c->datalen == 64) {
            out_sms_sha256_transform(c, c->data);
            c->bitlen += 512;
            c->datalen = 0;
        }
    }
}

static void out_sms_sha256_final(out_sms_sha256_ctx *c, uint8_t hash[32]) {
    uint32_t i = c->datalen;
    c->data[i++] = 0x80;
    if (c->datalen < 56) {
        while (i < 56) c->data[i++] = 0;
    } else {
        while (i < 64) c->data[i++] = 0;
        out_sms_sha256_transform(c, c->data);
        memset(c->data, 0, 56);
    }
    c->bitlen += (uint64_t)c->datalen * 8;
    for (int k = 0; k < 8; k++)
        c->data[63 - k] = (uint8_t)(c->bitlen >> (8 * k));
    out_sms_sha256_transform(c, c->data);
    for (int k = 0; k < 4; k++)
        for (int j = 0; j < 8; j++)
            hash[j*4 + k] = (uint8_t)(c->state[j] >> (24 - k * 8));
}

static int out_sms_sha256_file(const char *path, char hex_out[65]) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    out_sms_sha256_ctx ctx;
    out_sms_sha256_init(&ctx);
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        out_sms_sha256_update(&ctx, buf, n);
    fclose(f);
    unsigned char digest[32];
    out_sms_sha256_final(&ctx, digest);
    static const char *h = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        hex_out[i * 2] = h[digest[i] >> 4];
        hex_out[i * 2 + 1] = h[digest[i] & 0xf];
    }
    hex_out[64] = '\0';
    return 1;
}
#endif

static void json_escape(FILE *out, const char *s) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\')
            fputc('\\', out);
        fputc((int)*p, out);
    }
    fputc('"', out);
}

static int parse_linkedit_map(const char *v, out_sms_linkedit_map_mode_t *m) {
    if (!v || !strcmp(v, "segment"))
        *m = OUT_SMS_LINKEDIT_MAP_SEGMENT;
    else if (!strcmp(v, "mh_dataoff"))
        *m = OUT_SMS_LINKEDIT_MAP_MH_DATAOFF;
    else
        return -1;
    return 0;
}

static int parse_fnstarts_base(const char *v, out_sms_fnstarts_base_mode_t *m) {
    if (!v || !strcmp(v, "text_seg"))
        *m = OUT_SMS_FNSTARTS_BASE_TEXT_SEG;
    else if (!strcmp(v, "text_sect"))
        *m = OUT_SMS_FNSTARTS_BASE_TEXT_SECT;
    else
        return -1;
    return 0;
}

static void apply_env_policy(out_sms_gate_policy_t *pol) {
    const char *e;
    if ((e = getenv("OUT_SMS_BLOCK_MAX_A_DELTA")) != NULL && *e)
        pol->block_max_a_delta = (uint32_t)strtoul(e, NULL, 0);
    if ((e = getenv("OUT_SMS_LINKEDIT_MAP")) != NULL && *e)
        (void)parse_linkedit_map(e, &pol->linkedit_map);
    if ((e = getenv("OUT_SMS_FNSTARTS_BASE")) != NULL && *e)
        (void)parse_fnstarts_base(e, &pol->fnstarts_base);
    if ((e = getenv("OUT_SMS_ENFORCE_COMM_PIN")) != NULL) {
        pol->enforce_comm_pin = (strcmp(e, "0") && strcasecmp(e, "false"));
    }
}

static void emit_json(FILE *out, const char *candidate_id, const out_sms_gate_policy_t *pol, int uuid_ok,
                      int sha_ok, const out_sms_gate_result_t *gr, out_sms_gate_stage_t st) {
    fprintf(out, "{");
    fprintf(out, "\"candidate_id\":");
    json_escape(out, candidate_id ? candidate_id : "default");
    fprintf(out, ",\"stage\":%d,\"stage_name\":", (int)st);
    json_escape(out, out_sms_gate_stage_name(st));
    fprintf(out, ",\"uuid_ok\":%s,\"sha_ok\":%s", uuid_ok ? "true" : "false", sha_ok ? "true" : "false");
    fprintf(out, ",\"policy\":{");
    fprintf(out, "\"block_max_a_delta\":%u,\"linkedit_map\":%d,\"fnstarts_base\":%d,\"enforce_comm_pin\":%s",
            pol->block_max_a_delta, (int)pol->linkedit_map, (int)pol->fnstarts_base,
            pol->enforce_comm_pin ? "true" : "false");
    fprintf(out, "}");
    fprintf(out, ",\"result\":{");
    fprintf(out, "\"str_va\":\"0x%llx\"", (unsigned long long)gr->str_va);
    fprintf(out, ",\"block_head\":\"0x%llx\"", (unsigned long long)gr->block_head);
    fprintf(out, ",\"gate_va\":\"0x%llx\"", (unsigned long long)gr->gate_va);
    fprintf(out, ",\"gate_insn\":\"0x%x\"", gr->gate_insn);
    fprintf(out, ",\"fn_start\":\"0x%llx\"", (unsigned long long)gr->fn_start);
    fprintf(out, ",\"fn_end\":\"0x%llx\"", (unsigned long long)gr->fn_end);
    fprintf(out, ",\"ref_count\":%u,\"branch_to_lblk_count\":%u", gr->ref_count, gr->branch_to_lblk_count);
    fprintf(out, "},\"expected_gate_va\":\"0x%llx\",\"expected_gate_insn\":\"0x%x\"",
            (unsigned long long)OUT_SMS_PIN_PLACEHOLDER_GATE_VA, OUT_SMS_PATCH_ORIG_U32);
    fprintf(out, "}\n");
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage: %s --macho PATH [--candidate-id NAME] [--no-enforce-pin]\n", argv0);
    fprintf(stderr, "Env: OUT_SMS_BLOCK_MAX_A_DELTA, OUT_SMS_LINKEDIT_MAP=segment|mh_dataoff,\n");
    fprintf(stderr, "     OUT_SMS_FNSTARTS_BASE=text_seg|text_sect, OUT_SMS_ENFORCE_COMM_PIN\n");
}

int main(int argc, char **argv) {
    const char *macho = NULL;
    const char *candidate_id = "production";
    out_sms_gate_policy_t pol = out_sms_gate_policy_default();
    apply_env_policy(&pol);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--macho") && i + 1 < argc)
            macho = argv[++i];
        else if (!strcmp(argv[i], "--candidate-id") && i + 1 < argc)
            candidate_id = argv[++i];
        else if (!strcmp(argv[i], "--no-enforce-pin"))
            pol.enforce_comm_pin = 0;
        else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!macho) {
        usage(argv[0]);
        return 2;
    }

    out_sms_macho_map_t map;
    if (out_sms_macho_map_file(macho, &map) != 0) {
        fprintf(stderr, "map failed: %s\n", macho);
        return 1;
    }

    int uuid_ok = out_sms_macho_uuid_matches(map.mh);
    char sha_hex[65];
    int sha_ok = out_sms_sha256_file(macho, sha_hex)
                     && strcasecmp(sha_hex, PATCH_PIN_EXPECTED_SHA256) == 0
                     && map.file_size == OUT_SMS_EXPECTED_COMM_SIZE;

    out_sms_gate_result_t gr;
    out_sms_gate_stage_t st =
        out_sms_resolve_gate_policy(map.mh, map.slide, map.file_size, uuid_ok, sha_ok, &pol, &gr);

    emit_json(stdout, candidate_id, &pol, uuid_ok, sha_ok, &gr, st);
    out_sms_macho_unmap(&map);
    return st == OUT_SMS_GATE_OK ? 0 : 1;
}
