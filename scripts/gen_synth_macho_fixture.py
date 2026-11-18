#!/usr/bin/env python3
"""Generate synthetic Mach-O byte windows for public CI (no Apple proprietary bytes)."""
from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

from synth_pin_constants import (
    ADRP_VA,
    BLOCK_HEAD,
    COMM_SIZE,
    GATE_U32,
    GATE_VA,
    IMAGE_BASE,
    OSLOG_SIZE,
    OSLOG_VA,
    STR_VA,
    SYNTH_UUID,
    TEXT_SIZE,
    TEXT_VA,
    TOKEN,
    TOKEN_WRONG_OFFSET3,
)

MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
MH_EXECUTE = 2
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
            out.append(b | (0x80 if v else 0))
            if not v:
                break
    return bytes(out)


def sect(name, seg, addr, size, offset, align=2):
    n = name.encode()[:16].ljust(16, b"\0")
    s = seg.encode()[:16].ljust(16, b"\0")
    return struct.pack("<16s16sQQIIIIIIII", n, s, addr, size, offset, align, 0, 0, 0, 0, 0, 0)


def seg(name, vmaddr, vmsize, fileoff, filesize, nsects=0, maxprot=7, initprot=7):
    n = name.encode()[:16].ljust(16, b"\0")
    return struct.pack("<16sQQQQIIII", n, vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, 0)


def build_load_commands(fst_dataoff: int, fst_len: int, *, include_pagezero: bool = True) -> bytes:
    t_off = TEXT_VA - IMAGE_BASE
    o_off = OSLOG_VA - IMAGE_BASE
    parts = b""
    if include_pagezero:
        pz = struct.pack("<II", LC_SEGMENT_64, 72) + seg(
            "__PAGEZERO", 0, 0x100000000, 0, 0, 0
        )
        parts += pz
    text_lc = struct.pack("<II", LC_SEGMENT_64, 72 + 2 * 80)
    text_lc += seg("__TEXT", IMAGE_BASE, 0x2200000, 0, TEXT_SIZE + OSLOG_SIZE, 2)
    text_lc += sect("__text", "__TEXT", TEXT_VA, TEXT_SIZE, t_off)
    text_lc += sect("__oslogstring", "__TEXT", OSLOG_VA, OSLOG_SIZE, o_off)
    data_lc = struct.pack("<II", LC_SEGMENT_64, 72) + seg(
        "__DATA", IMAGE_BASE + 0x2000000, 0x1000, 0, 0, 0
    )
    le_off = 0x300000
    linkedit_lc = struct.pack("<II", LC_SEGMENT_64, 72) + seg(
        "__LINKEDIT", IMAGE_BASE + 0x3000000, 0x200, le_off, 0x200, 0
    )
    uuid_lc = struct.pack("<II", LC_UUID, 24) + SYNTH_UUID
    fst_lc = struct.pack("<IIII", LC_FUNCTION_STARTS, 16, fst_dataoff, fst_len)
    return parts + text_lc + data_lc + linkedit_lc + uuid_lc + fst_lc


def patch_gate_chain(mem: bytearray) -> None:
    base = TEXT_VA - IMAGE_BASE
    struct.pack_into("<I", mem, base + (GATE_VA - TEXT_VA), GATE_U32)
    adrp = enc_adrp(3, STR_VA & ~0xFFF, ADRP_VA)
    add = enc_add_imm12(3, 3, STR_VA & 0xFFF)
    struct.pack_into("<I", mem, base + (ADRP_VA - TEXT_VA), adrp)
    struct.pack_into("<I", mem, base + (ADRP_VA - TEXT_VA) + 4, add)


def build_runtime_image(*, token: bytes, corrupt_fst: bool) -> bytearray:
    runtime = max(
        (OSLOG_VA - IMAGE_BASE) + OSLOG_SIZE + 0x1000,
        0x320000,
    )
    mem = bytearray(runtime)
    patch_gate_chain(mem)
    o_off = OSLOG_VA - IMAGE_BASE
    str_off = o_off + (STR_VA - OSLOG_VA)
    mem[str_off : str_off + len(token)] = token

    fn_start = ADRP_VA - 0x800
    fn_end = ADRP_VA + 0x200
    fst = uleb128([fn_start - IMAGE_BASE, fn_end - fn_start])
    fst_linkedit_off = 0x300000
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
        0,
        MH_EXECUTE,
        6,
        len(cmds),
        0x82000085,
        0,
    )
    mem[0:32] = mh
    mem[32 : 32 + len(cmds)] = cmds
    return mem


def write_windows(out_dir: Path) -> dict:
    mem = build_runtime_image(token=TOKEN, corrupt_fst=False)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "runtime_image.bin").write_bytes(bytes(mem))
    win_dir = out_dir / "windows"
    win_dir.mkdir(exist_ok=True)
    # Header: mach hdr + load commands
    cmds_end = 32 + struct.unpack_from("<I", mem, 52)[0]  # sizeofcmds at offset? actually mh+sizeofcmds field offset 48
    sizeofcmds = struct.unpack_from("<I", mem, 48)[0]
    hdr_size = 32 + sizeofcmds
    (win_dir / "header.bin").write_bytes(bytes(mem[:hdr_size]))
    text_off = TEXT_VA - IMAGE_BASE
    scan_back = 0x800
    scan_fwd = 0x84
    t0 = text_off + (ADRP_VA - TEXT_VA) - scan_back
    t1 = text_off + (ADRP_VA - TEXT_VA) + scan_fwd
    (win_dir / "text_region.bin").write_bytes(bytes(mem[t0:t1]))
    os_off = OSLOG_VA - IMAGE_BASE + (STR_VA - OSLOG_VA)
    (win_dir / "oslog_token.bin").write_bytes(bytes(mem[os_off : os_off + 0x40]))
    manifest = {
        "synthetic": True,
        "image_base": hex(IMAGE_BASE),
        "comm_size": hex(COMM_SIZE),
        "placeholder_gate_va": hex(GATE_VA),
        "placeholder_gate_insn": hex(GATE_U32),
        "cases": [
            "production_local_scan",
            "token_anchor_hash_not_plus3",
            "legacy_function_starts_wrong_offset",
            "pagezero_filesize_zero_mapper",
            "linkedit_segment_map",
            "ambiguous_reference_rejection",
        ],
    }
    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def emit_synthetic_fixture_header(path: Path) -> None:
    """Also regenerate tests/fixtures header used by unit matrix."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import gen_out_sms_resolver_fixture  # noqa: F401

    gen_out_sms_resolver_fixture.emit_header(path)


def token_at_str(mem: bytearray, token: bytes) -> bool:
    off = (OSLOG_VA - IMAGE_BASE) + (STR_VA - OSLOG_VA)
    return bytes(mem[off : off + len(token)]) == token


def selftest_cases() -> int:
    """Exercise failure modes without weakening success criteria."""
    fail = 0
    good = build_runtime_image(token=TOKEN, corrupt_fst=False)
    bad_tok = build_runtime_image(token=TOKEN_WRONG_OFFSET3, corrupt_fst=False)
    bad_fst = build_runtime_image(token=TOKEN, corrupt_fst=True)
    if not token_at_str(good, TOKEN):
        fail += 1
    if token_at_str(bad_tok, TOKEN) or not token_at_str(bad_tok, TOKEN_WRONG_OFFSET3):
        fail += 1
    if bytes(good) == bytes(bad_fst):
        fail += 1
    return fail


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    out = Path(sys.argv[1] if len(sys.argv) > 1 else root / "synth-fixture")
    write_windows(out)
    emit_synthetic_fixture_header(root / "tests/fixtures/out_sms_gate_macho_fixture.h")
    if selftest_cases():
        print("gen_synth_macho_fixture: selftest failed", file=sys.stderr)
        return 1
    print(f"wrote {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())