#include "out_sms_macho_map.h"

#include "../../include/patch_pin.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int out_sms_macho_uuid_matches(const struct mach_header_64 *mh) {
    const uint8_t *cursor = (const uint8_t *)(mh + 1);
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cursor;
        if (lc->cmd == LC_UUID) {
            const struct uuid_command *uc = (const struct uuid_command *)lc;
            return patch_pin_uuid_matches(uc->uuid);
        }
        cursor += lc->cmdsize;
    }
    return 0;
}

int out_sms_macho_map_file(const char *path, out_sms_macho_map_t *out) {
    memset(out, 0, sizeof(*out));
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size == 0) {
        close(fd);
        return -1;
    }
    const uint8_t *file = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED)
        return -1;
    const uint64_t file_size = (uint64_t)st.st_size;

    if (st.st_size < (off_t)sizeof(struct mach_header_64)) {
        munmap((void *)file, (size_t)st.st_size);
        return -1;
    }
    const struct mach_header_64 *fh = (const struct mach_header_64 *)file;
    if (fh->magic != MH_MAGIC_64) {
        munmap((void *)file, (size_t)st.st_size);
        return -1;
    }

    uintptr_t min_va = UINT64_MAX;
    uintptr_t max_va = 0;
    const uint8_t *cursor = (const uint8_t *)(file + 1);
    for (uint32_t i = 0; i < fh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cursor;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            if (sg->vmaddr < min_va)
                min_va = (uintptr_t)sg->vmaddr;
            uintptr_t end = (uintptr_t)(sg->vmaddr + sg->vmsize);
            if (end > max_va)
                max_va = end;
        }
        cursor += lc->cmdsize;
    }
    if (min_va == UINT64_MAX || max_va <= min_va) {
        munmap((void *)file, (size_t)st.st_size);
        return -1;
    }

    size_t image_size = (size_t)(max_va - min_va);
    uint8_t *image = calloc(1, image_size);
    if (!image) {
        munmap((void *)file, (size_t)st.st_size);
        return -1;
    }

    cursor = (const uint8_t *)(file + 1);
    for (uint32_t i = 0; i < fh->ncmds; i++) {
        const struct load_command *lc = (const struct load_command *)cursor;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *sg = (const struct segment_command_64 *)lc;
            uintptr_t dst_off = (uintptr_t)(sg->vmaddr - min_va);
            if (sg->filesize && dst_off + sg->filesize <= image_size)
                memcpy(image + dst_off, file + sg->fileoff, (size_t)sg->filesize);
        }
        cursor += lc->cmdsize;
    }
    munmap((void *)file, (size_t)st.st_size);

    out->image = image;
    out->image_size = image_size;
    out->image_base = min_va;
    out->slide = (intptr_t)image - (intptr_t)min_va;
    out->mh = (struct mach_header_64 *)image;
    if (out->mh->magic != MH_MAGIC_64) {
        out_sms_macho_unmap(out);
        return -1;
    }
    out->file_size = file_size;
    return 0;
}

void out_sms_macho_unmap(out_sms_macho_map_t *map) {
    if (map && map->image) {
        free(map->image);
        map->image = NULL;
    }
}