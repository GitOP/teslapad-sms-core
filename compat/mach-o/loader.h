#include <stddef.h>
#ifndef _MACHO_LOADER_H_
#define _MACHO_LOADER_H_
#include <stdint.h>
#define MH_MAGIC_64 0xfeedfacf
#define MH_EXECUTE 2
#define CPU_TYPE_ARM64 0x0100000c
#define CPU_SUBTYPE_ARM64_ALL 0
#define LC_SEGMENT_64 0x19
#define LC_UUID 0x1b
#define LC_FUNCTION_STARTS 0x26
struct mach_header_64 { uint32_t magic; int32_t cputype; int32_t cpusubtype; uint32_t filetype; uint32_t ncmds; uint32_t sizeofcmds; uint32_t flags; uint32_t reserved; };
struct load_command { uint32_t cmd; uint32_t cmdsize; };
struct segment_command_64 { uint32_t cmd; uint32_t cmdsize; char segname[16]; uint64_t vmaddr; uint64_t vmsize; uint64_t fileoff; uint64_t filesize; int32_t maxprot; int32_t initprot; uint32_t nsects; uint32_t flags; };
struct section_64 { char sectname[16]; char segname[16]; uint64_t addr; uint64_t size; uint32_t offset; uint32_t align; uint32_t reloff; uint32_t nreloc; uint32_t flags; uint32_t reserved1; uint32_t reserved2; uint32_t reserved3; };
struct uuid_command { uint32_t cmd; uint32_t cmdsize; uint8_t uuid[16]; };
struct linkedit_data_command { uint32_t cmd; uint32_t cmdsize; uint32_t dataoff; uint32_t datasize; };
#endif
