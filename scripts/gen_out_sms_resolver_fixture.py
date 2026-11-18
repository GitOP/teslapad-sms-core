#!/usr/bin/env python3
"""Generate compact synthetic fixtures for outbound gate resolver host tests."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

from synth_pin_constants import (
    ADRP_VA,
    BLOCK_HEAD,
    GATE_U32,
    GATE_VA,
    IMAGE_BASE,
    OSLOG_SIZE,
    OSLOG_VA,
    STR_VA,
    TEXT_SIZE,
    TEXT_VA,
    TOKEN,
    TOKEN_WRONG_OFFSET3 as TOKEN_WRONG,
)

DATA_VA = IMAGE_BASE + 0x2000000
DATA_FILE = 0x100
DATA_VMSIZE = DATA_FILE + 0x4000
LINKEDIT_VA = DATA_VA + DATA_VMSIZE
RUNTIME_SIZE = max((LINKEDIT_VA - IMAGE_BASE) + 0x200, (OSLOG_VA - IMAGE_BASE) + OSLOG_SIZE + 0x1000, 0x320000)

FN_START = ADRP_VA - 0x800
FN_END = ADRP_VA + 0x200

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
CPU_SUBTYPE_ARM64_ALL = 0
MH_EXECUTE = 0x2
LC_SEGMENT_64 = 0x19
LC_UUID = 0x1B
LC_FUNCTION_STARTS = 0x26


def enc_adrp(rd: int, target_page: int, pc: int) -> int:
    page_pc = pc & ~0xFFF
    imm = ((target_page - page_pc) >> 12) & ((1 << 21) - 1)
    return 0x90000000 | ((imm & 3) << 29) | ((imm >> 2) << 5) | rd


def enc_add_imm12(rd: int, rn: int, imm: int) -> int:
    return 0x91000000 | ((imm & 0xFFF) << 10) | (rn << 5) | rd


def enc_cbz_w0(pc: int, target: int) -> int:
    imm = (target - pc) // 4
    return 0x34000000 | ((imm & 0x7FFFF) << 5)


def uleb128(values: list[int]) -> bytes:
    out = bytearray()
    for v in values:
        while True:
            b = v & 0x7F
            v >>= 7
            if v:
                out.append(b | 0x80)
            else:
                out.append(b)
                break
    return bytes(out)


def sect(name, seg, addr, size, offset, align=2):
    n = name.encode()[:16].ljust(16, b"\0")
    s = seg.encode()[:16].ljust(16, b"\0")
    return struct.pack("<16s16sQQIIIIIIII", n, s, addr, size, offset, align, 0, 0, 0, 0, 0, 0)


def seg(name, vmaddr, vmsize, fileoff, filesize, nsects=0, maxprot=7, initprot=7):
    n = name.encode()[:16].ljust(16, b"\0")
    return struct.pack("<16sQQQQIIII", n, vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, 0)


def patch_text(blob: bytearray) -> None:
    base = TEXT_VA - IMAGE_BASE
    struct.pack_into("<I", blob, base + (GATE_VA - TEXT_VA), GATE_U32)
    adrp = enc_adrp(3, STR_VA & ~0xFFF, ADRP_VA)
    add = enc_add_imm12(3, 3, STR_VA & 0xFFF)
    struct.pack_into("<I", blob, base + (ADRP_VA - TEXT_VA), adrp)
    struct.pack_into("<I", blob, base + (ADRP_VA - TEXT_VA) + 4, add)


def build_load_commands(fst_dataoff: int, fst_len: int) -> bytes:
    t_off = TEXT_VA - IMAGE_BASE
    o_off = OSLOG_VA - IMAGE_BASE
    le_off = LINKEDIT_VA - IMAGE_BASE
    text_lc = struct.pack("<II", LC_SEGMENT_64, 72 + 2 * 80)
    text_lc += seg("__TEXT", IMAGE_BASE, 0x2200000, 0, TEXT_SIZE + OSLOG_SIZE, 2)
    text_lc += sect("__text", "__TEXT", TEXT_VA, TEXT_SIZE, t_off)
    text_lc += sect("__oslogstring", "__TEXT", OSLOG_VA, OSLOG_SIZE, o_off)
    data_lc = struct.pack("<II", LC_SEGMENT_64, 72) + seg("__DATA", DATA_VA, DATA_VMSIZE, 0, DATA_FILE, 0)
    linkedit_lc = struct.pack("<II", LC_SEGMENT_64, 72) + seg("__LINKEDIT", LINKEDIT_VA, 0x200, le_off, 0x200, 0)
    uuid_lc = struct.pack("<II", LC_UUID, 24) + bytes(16)
    fst_lc = struct.pack("<IIII", LC_FUNCTION_STARTS, 16, fst_dataoff, fst_len)
    return text_lc + data_lc + linkedit_lc + uuid_lc + fst_lc


def build_fixture(oslog_token: bytes, *, corrupt_fst: bool) -> dict:
    mem = bytearray(RUNTIME_SIZE)
    patch_text(mem)
    o_off = OSLOG_VA - IMAGE_BASE
    str_off = o_off + (STR_VA - OSLOG_VA)
    mem[str_off : str_off + len(oslog_token)] = oslog_token

    fst = uleb128([FN_START - IMAGE_BASE, FN_END - FN_START])
    fst_linkedit_off = LINKEDIT_VA - IMAGE_BASE
    fst_legacy_off = 0x2000
    if corrupt_fst:
        mem[fst_legacy_off : fst_legacy_off + len(fst)] = fst
        fst_dataoff = fst_legacy_off
    else:
        mem[fst_linkedit_off : fst_linkedit_off + len(fst)] = fst
        fst_dataoff = fst_linkedit_off

    cmds = build_load_commands(fst_dataoff, len(fst))
    mh = struct.pack(
        "<IIIIIIII",
        MH_MAGIC_64,
        CPU_TYPE_ARM64,
        CPU_SUBTYPE_ARM64_ALL,
        MH_EXECUTE,
        5,
        len(cmds),
        0x82000085,
        0,
    )
    mem[0:32] = mh
    mem[32 : 32 + len(cmds)] = cmds
    return {"load_cmds_len": len(cmds), "fst_len": len(fst), "corrupt_fst": corrupt_fst, "oslog_token": oslog_token}


def to_c_array(data: bytes, label: str) -> str:
    lines = [f"static const uint8_t {label}[] = {{"]
    for i in range(0, len(data), 16):
        chunk = data[i : i + 16]
        lines.append("    " + ", ".join(f"0x{b:02x}" for b in chunk) + ",")
    lines.append("};")
    lines.append(f"static const size_t {label}_len = sizeof({label});")
    return "\n".join(lines)


def emit_header(path: Path) -> None:
    good = build_fixture(TOKEN, corrupt_fst=False)
    wrong = build_fixture(TOKEN_WRONG, corrupt_fst=False)
    broken = build_fixture(TOKEN, corrupt_fst=True)

    # Store only load-command blobs + tokens; test asm.c rebuilds image via shared layout.
    good_cmds = bytes(build_load_commands(LINKEDIT_VA - IMAGE_BASE, good["fst_len"]))
    broken_cmds = bytes(build_load_commands(0x2000, broken["fst_len"]))
    fst = uleb128([FN_START - IMAGE_BASE, FN_END - FN_START])

    hdr = f"""/* Generated by scripts/gen_out_sms_resolver_fixture.py */
#ifndef OUT_SMS_GATE_MACHO_FIXTURE_H
#define OUT_SMS_GATE_MACHO_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

#define OUT_SMS_FIXTURE_IMAGE_BASE 0x{IMAGE_BASE:x}ULL
#define OUT_SMS_FIXTURE_RUNTIME_SIZE 0x{RUNTIME_SIZE:x}u
#define OUT_SMS_FIXTURE_LINKEDIT_OFF 0x{LINKEDIT_VA - IMAGE_BASE:x}u
#define OUT_SMS_FIXTURE_FST_LEGACY_OFF 0x2000u

{to_c_array(good_cmds, "out_sms_fixture_load_cmds")}
{to_c_array(broken_cmds, "out_sms_fixture_load_cmds_broken")}
{to_c_array(fst, "out_sms_fixture_function_starts")}
{to_c_array(TOKEN, "out_sms_fixture_token_ok")}
{to_c_array(TOKEN_WRONG, "out_sms_fixture_token_bad")}

#endif
"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(hdr)
    print(path, len(hdr))


if __name__ == "__main__":
    emit_header(Path(sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/out_sms_gate_macho_fixture.h"))