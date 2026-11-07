"""
Locate OrganizerCore::refreshDirectoryStructure in ModOrganizer.exe by finding
RIP-relative LEA references to the "refreshing structure" log string.
"""
from __future__ import annotations

import argparse
import struct
import sys
from typing import Iterable

import pefile


def _section(pe: pefile.PE, name: bytes) -> pefile.SectionStructure | None:
    for s in pe.sections:
        if s.Name.rstrip(b"\x00") == name:
            return s
    return None


def _find_string_rva(pe: pefile.PE, needle: bytes) -> int:
    for sec in pe.sections:
        data = sec.get_data()
        idx = data.find(needle)
        if idx != -1:
            return sec.VirtualAddress + idx
    raise ValueError(f"String {needle!r} not found")


def _rip_lea_targets(
    text_data: bytes, text_rva: int, image_base: int
) -> Iterable[tuple[int, int, int]]:
    """Yield (file_offset_in_text, insn_rva, target_va) for 48 8d 0d rip-relative LEA."""
    i = 0
    n = len(text_data)
    while i + 7 <= n:
        b0, b1, b2 = text_data[i], text_data[i + 1], text_data[i + 2]
        if b0 == 0x48 and b1 == 0x8D and b2 == 0x0D:
            disp = struct.unpack_from("<i", text_data, i + 3)[0]
            insn_rva = text_rva + i
            insn_va = image_base + insn_rva
            target_va = insn_va + 7 + disp
            yield (i, insn_rva, target_va)
        i += 1


def _find_function_start(text_data: bytes, hit_off: int, max_lookback: int = 0x200) -> int:
    """
    MSVC x64: look for common leaf prologue opening
    mov [rsp+0x10], rbx (48 89 5C 24 10) before the hit.
    """
    start = max(0, hit_off - max_lookback)
    pattern = bytes([0x48, 0x89, 0x5C, 0x24, 0x10])
    slab = text_data[start:hit_off]
    idx = slab.rfind(pattern)
    if idx == -1:
        return hit_off
    return start + idx


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    args = ap.parse_args()

    pe = pefile.PE(args.exe, fast_load=True)
    pe.parse_data_directories()

    image_base = pe.OPTIONAL_HEADER.ImageBase
    text_sec = _section(pe, b".text")
    if text_sec is None:
        print("No .text section", file=sys.stderr)
        return 2

    text_rva = text_sec.VirtualAddress
    text_data = text_sec.get_data()
    str_rva = _find_string_rva(pe, b"refreshing structure\x00")
    str_va = image_base + str_rva

    hits = []
    for off, insn_rva, target_va in _rip_lea_targets(text_data, text_rva, image_base):
        if target_va == str_va:
            hits.append(off)

    if not hits:
        print("No LEA xref to string", file=sys.stderr)
        return 3

    # Usually one hit inside refreshDirectoryStructure
    off = hits[0]
    if len(hits) > 1:
        print(f"Note: multiple LEAs ({len(hits)}), using first at +0x{hits[0]:x}", file=sys.stderr)

    fn_off = _find_function_start(text_data, off)
    fn_rva = text_rva + fn_off
    fn_va = image_base + fn_rva

    print(f"image_base=0x{image_base:X}")
    print(f"string_rva=0x{str_rva:X} string_va=0x{str_va:X}")
    print(f"lea_off=0x{off:X} lea_rva=0x{text_rva + off:X}")
    print(f"fn_rva=0x{fn_rva:X} fn_va=0x{fn_va:X}")
    print(f"prologue_bytes={text_data[fn_off:fn_off+16].hex()}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
