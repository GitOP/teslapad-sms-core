#ifndef OUT_SMS_MACHO_MAP_H
#define OUT_SMS_MACHO_MAP_H

#include <mach-o/loader.h>
#include <stdint.h>

typedef struct {
    uint8_t *image;
    size_t image_size;
    uintptr_t image_base;
    intptr_t slide;
    struct mach_header_64 *mh;
    uint64_t file_size;
} out_sms_macho_map_t;

int out_sms_macho_map_file(const char *path, out_sms_macho_map_t *out);
void out_sms_macho_unmap(out_sms_macho_map_t *map);

int out_sms_macho_uuid_matches(const struct mach_header_64 *mh);

#endif
