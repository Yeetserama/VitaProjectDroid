#!/usr/bin/env python3
"""Find candidate MMIO addresses in a decrypted Vita ELF module.

The Vita peripheral window is 0xE0000000-0xE3FFFFFF.  The scanner looks in
non-executable sections and resolves ARM-mode PC-relative literal-pool loads
(`ldr rX, [pc, #imm]`); raw .text words are mostly opcodes and are opt-in.

This is a discovery aid, not proof: a hit is a candidate until it is traced
back to a function in Ghidra.

Usage:
    python re/scripts/scan-mmio.py re/dumps/fw365/audio.skprx.elf
    python re/scripts/scan-mmio.py --range 0xE0000000-0xE3FFFFFF \
        --range 0x1A000000-0x1A0FFFFF audio.skprx.elf
    python re/scripts/scan-mmio.py --include-text audio.skprx.elf
    python re/scripts/scan-mmio.py --self-test
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import tempfile
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable


ELF_MAGIC = b"\x7fELF"
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_NOBITS = 8
PT_LOAD = 1


@dataclass
class Section:
    name: str
    addr: int
    offset: int
    size: int
    flags: int
    sh_type: int

    @property
    def executable(self) -> bool:
        return bool(self.flags & SHF_EXECINSTR)


@dataclass
class Hit:
    value: int
    section: str
    file_offset: int
    vaddr: int


ARM_LDR_LITERAL_MASK = 0x0F7F0000
ARM_LDR_LITERAL_VALUE = 0x051F0000  # cond 01 P=1 U B=0 W=0 L=1 Rn=PC


def _u16(data: bytes, off: int, endian: str) -> int:
    return struct.unpack_from(endian + "H", data, off)[0]


def _u32(data: bytes, off: int, endian: str) -> int:
    return struct.unpack_from(endian + "I", data, off)[0]


def _u64(data: bytes, off: int, endian: str) -> int:
    return struct.unpack_from(endian + "Q", data, off)[0]


def parse_sections(data: bytes) -> tuple[list[Section], bool, str]:
    """Return (sections, is_64bit, endian).  Empty list if no section table."""
    if len(data) < 52 or data[:4] != ELF_MAGIC:
        raise ValueError("not an ELF file")

    ei_class = data[4]
    ei_data = data[5]
    if ei_class not in (1, 2):
        raise ValueError(f"unsupported ELF class {ei_class}")
    if ei_data not in (1, 2):
        raise ValueError(f"unsupported ELF data encoding {ei_data}")

    is_64 = ei_class == 2
    endian = "<" if ei_data == 1 else ">"

    if is_64:
        if len(data) < 64:
            raise ValueError("truncated ELF64 header")
        e_shoff = _u64(data, 0x28, endian)
        e_shentsize = _u16(data, 0x3A, endian)
        e_shnum = _u16(data, 0x3C, endian)
        e_shstrndx = _u16(data, 0x3E, endian)
        sh_fmt = endian + "IIQQQQIIQQ"
    else:
        e_shoff = _u32(data, 0x20, endian)
        e_shentsize = _u16(data, 0x2E, endian)
        e_shnum = _u16(data, 0x30, endian)
        e_shstrndx = _u16(data, 0x32, endian)
        sh_fmt = endian + "IIIIIIIIII"

    if e_shoff == 0 or e_shnum == 0:
        return [], is_64, endian

    sh_size = struct.calcsize(sh_fmt)
    if e_shentsize < sh_size:
        raise ValueError("ELF section header entry is too small")
    if e_shoff + e_shentsize * e_shnum > len(data):
        raise ValueError("section header table runs past end of file")
    if e_shstrndx >= e_shnum:
        raise ValueError("invalid section-name string table index")

    def header(index: int) -> tuple[int, ...]:
        off = e_shoff + index * e_shentsize
        return struct.unpack_from(sh_fmt, data, off)

    str_hdr = header(e_shstrndx)
    str_off = str_hdr[4]
    str_size = str_hdr[5]
    if str_off + str_size > len(data):
        raise ValueError("section-name string table runs past end of file")
    strtab = data[str_off:str_off + str_size]

    sections: list[Section] = []
    for i in range(e_shnum):
        fields = header(i)
        sh_name, sh_type, sh_flags = fields[0], fields[1], fields[2]
        sh_addr, sh_offset, sh_size = fields[3], fields[4], fields[5]
        end = strtab.find(b"\0", sh_name)
        name = strtab[sh_name:end if end >= 0 else len(strtab)]
        sections.append(
            Section(
                name=name.decode("ascii", "replace"),
                addr=sh_addr,
                offset=sh_offset,
                size=sh_size,
                flags=sh_flags,
                sh_type=sh_type,
            )
        )
    return sections, is_64, endian


def parse_segments(data: bytes, is_64: bool, endian: str) -> list[Section]:
    """Fallback when a stripped module has no section table."""
    if is_64:
        if len(data) < 64:
            return []
        e_phoff = _u64(data, 0x20, endian)
        e_phentsize = _u16(data, 0x36, endian)
        e_phnum = _u16(data, 0x38, endian)
        ph_fmt = endian + "IIQQQQQQ"
    else:
        e_phoff = _u32(data, 0x1C, endian)
        e_phentsize = _u16(data, 0x2A, endian)
        e_phnum = _u16(data, 0x2C, endian)
        ph_fmt = endian + "IIIIIIII"

    if e_phoff == 0 or e_phnum == 0:
        return []
    ph_size = struct.calcsize(ph_fmt)
    if e_phentsize < ph_size or e_phoff + e_phentsize * e_phnum > len(data):
        return []

    out: list[Section] = []
    for i in range(e_phnum):
        fields = struct.unpack_from(ph_fmt, data, e_phoff + i * e_phentsize)
        p_type = fields[0]
        if p_type != PT_LOAD:
            continue
        if is_64:
            p_flags, p_offset, p_vaddr, p_filesz = fields[1], fields[2], fields[3], fields[5]
        else:
            p_offset, p_vaddr, p_filesz, p_flags = fields[1], fields[2], fields[4], fields[6]
        out.append(
            Section(
                name=f"PT_LOAD[{i}]",
                addr=p_vaddr,
                offset=p_offset,
                size=p_filesz,
                flags=p_flags,
                sh_type=0,
            )
        )
    return out


def parse_ranges(values: Iterable[str]) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    for item in values:
        text = item.strip().lower().replace("_", "")
        if "-" in text:
            lo_text, hi_text = text.split("-", 1)
            lo = int(lo_text, 0)
            hi = int(hi_text, 0)
        else:
            base = int(text, 0)
            lo, hi = base, base + 0xFFFFF
        if hi < lo:
            raise ValueError(f"range end is below start: {item}")
        ranges.append((lo, hi))
    return ranges


def in_ranges(value: int, ranges: list[tuple[int, int]]) -> bool:
    return any(lo <= value <= hi for lo, hi in ranges)


def nearest_string(data: bytes, offset: int, radius: int = 64, min_len: int = 4) -> str | None:
    start = max(0, offset - radius)
    window = data[start:offset + radius]
    best = b""
    run = bytearray()
    for byte in window:
        if 0x20 <= byte < 0x7F:
            run.append(byte)
        else:
            if len(run) > len(best):
                best = bytes(run)
            run.clear()
    if len(run) > len(best):
        best = bytes(run)
    if len(best) < min_len:
        return None
    return best.decode("ascii", "replace")


def scan(data: bytes, sections: list[Section], ranges: list[tuple[int, int]],
         unaligned: bool = False, include_text: bool = False) -> list[Hit]:
    hits: list[Hit] = []
    step = 1 if unaligned else 4
    for sec in sections:
        if sec.sh_type == SHT_NOBITS or not (sec.flags & SHF_ALLOC):
            continue
        if sec.executable and not include_text:
            continue
        if sec.size < 4 or sec.offset >= len(data):
            continue
        end = min(sec.offset + sec.size, len(data))
        start = sec.offset
        # Align the first word to the section's virtual address, not the file
        # offset: .text is normally both 4-byte aligned, but be explicit.
        if not unaligned:
            start += (-(sec.addr + start)) % 4
        for off in range(start, end - 3, step):
            value = struct.unpack_from("<I", data, off)[0]
            if in_ranges(value, ranges):
                hits.append(
                    Hit(
                        value=value,
                        section=sec.name,
                        file_offset=off,
                        vaddr=sec.addr + (off - sec.offset),
                    )
                )
    return hits


def page_of(value: int) -> int:
    return value & 0xFFFFF000


def vaddr_to_file_offset(sections: list[Section], vaddr: int,
                         length: int = 4) -> tuple[int, Section] | None:
    for sec in sections:
        if sec.sh_type == SHT_NOBITS:
            continue
        if sec.addr <= vaddr and vaddr + length <= sec.addr + sec.size:
            return sec.offset + (vaddr - sec.addr), sec
    return None


def scan_arm_literals(data: bytes, sections: list[Section],
                      ranges: list[tuple[int, int]]) -> list[Hit]:
    """Resolve ARM-mode `ldr rX, [pc, #imm]` literal-pool loads.

    Raw words in .text are mostly ARM opcodes, many of which fall inside the
    peripheral window.  Only report values that an actual PC-relative load
    reaches, which is how a driver gets a register base into a register.
    Thumb-2 loads are not decoded yet; use --include-text for those modules.
    """
    hits: list[Hit] = []
    seen: set[tuple[int, int]] = set()
    for sec in sections:
        if not sec.executable or sec.size < 4 or sec.offset >= len(data):
            continue
        end = min(sec.offset + sec.size, len(data))
        for off in range(sec.offset, end - 3, 4):
            word = struct.unpack_from("<I", data, off)[0]
            if (word & ARM_LDR_LITERAL_MASK) != ARM_LDR_LITERAL_VALUE:
                continue
            if (word >> 28) == 0xF:  # NV condition, not a real load
                continue
            imm = word & 0xFFF
            instr_vaddr = sec.addr + (off - sec.offset)
            base = (instr_vaddr + 8) & ~3
            target = base + imm if word & (1 << 23) else base - imm
            loc = vaddr_to_file_offset(sections, target)
            if loc is None:
                continue
            lit_off, _ = loc
            value = struct.unpack_from("<I", data, lit_off)[0]
            if not in_ranges(value, ranges):
                continue
            key = (lit_off, value)
            if key in seen:
                continue
            seen.add(key)
            hits.append(
                Hit(
                    value=value,
                    section=f"{sec.name}@0x{instr_vaddr:x}",
                    file_offset=lit_off,
                    vaddr=target,
                )
            )
    return hits


def _self_test() -> int:
    """Build a tiny ELF32 LE file and check section/segment parsing."""
    # Layout: ELF header (52) + .text (16) + .rodata (8) + shstrtab (17)
    # + section headers (5 * 40).
    ehsize = 52
    text_off = ehsize
    text = struct.pack("<IIII", 0xE59F0000, 0xE1A01001, 0xE0A00000, 0xE3101000)
    rodata_off = text_off + len(text)
    rodata = struct.pack("<II", 0xE0B00000, 0xE0B00004)
    names = b"\0.text\0.rodata\0.shstrtab\0"
    names_off = rodata_off + len(rodata)
    shoff = (names_off + len(names) + 3) & ~3
    shnum = 4
    shstrndx = 3

    header = bytearray(ehsize)
    header[0:4] = ELF_MAGIC
    header[4] = 1  # ELFCLASS32
    header[5] = 1  # little endian
    header[6] = 1  # version
    struct.pack_into("<HHIIIIIHHHHHH", header, 16,
                     2, 40, 1, 0, 0, shoff, 0, ehsize, 0, 0, 40, shnum, shstrndx)

    def sh(name, sh_type, flags, addr, off, size):
        return struct.pack("<IIIIIIIIII", name, sh_type, flags, addr, off, size, 0, 0, 4, 0)

    shdrs = [
        sh(0, 0, 0, 0, 0, 0),
        sh(1, 1, SHF_ALLOC | 0x4, 0xE0000000, text_off, len(text)),
        sh(7, 1, SHF_ALLOC, 0xE0001000, rodata_off, len(rodata)),
        sh(15, 3, 0, 0, names_off, len(names)),
    ]
    blob = bytes(header) + text + rodata + names
    blob += b"\0" * (shoff - len(blob)) + b"".join(shdrs)

    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "fake.elf"
        path.write_bytes(blob)
        sections, is_64, endian = parse_sections(path.read_bytes())
        assert not is_64 and endian == "<"
        assert [s.name for s in sections] == ["", ".text", ".rodata", ".shstrtab"]
        assert parse_segments(path.read_bytes(), is_64, endian) == []

        hits = scan(path.read_bytes(), sections, parse_ranges(["0xE0000000-0xE3FFFFFF"]),
                    include_text=True)
        values = [h.value for h in hits]
        assert values == [0xE1A01001, 0xE0A00000, 0xE3101000,
                          0xE0B00000, 0xE0B00004], values
        assert hits[0].vaddr == 0xE0000004
        assert hits[3].vaddr == 0xE0001000

        literal_hits = scan_arm_literals(
            path.read_bytes(), sections, parse_ranges(["0xE0000000-0xE3FFFFFF"]))
        assert [h.value for h in literal_hits] == [0xE0A00000], literal_hits
        assert literal_hits[0].vaddr == 0xE0000008
        assert literal_hits[0].section == ".text@0xe0000000"

        default_hits = scan(path.read_bytes(), sections,
                            parse_ranges(["0xE0000000-0xE3FFFFFF"]))
        default_hits += literal_hits
        assert sorted(h.value for h in default_hits) == [
            0xE0A00000, 0xE0B00000, 0xE0B00004], default_hits
        assert nearest_string(b"\0\0hello\0\0", 2, min_len=3) == "hello"

    print("self-test ok")
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("elf", nargs="?", help="decrypted module (.elf)")
    parser.add_argument(
        "--range", action="append", dest="ranges", metavar="LO-HI",
        help="hex address range (repeatable; default 0xE0000000-0xE3FFFFFF)",
    )
    parser.add_argument("--unaligned", action="store_true",
                        help="scan every byte instead of every fourth byte")
    parser.add_argument("--include-text", action="store_true",
                        help="also scan executable sections (noisy: ARM opcodes look like addresses)")
    parser.add_argument("--no-literals", action="store_true",
                        help="skip ARM ldr rX, [pc, #imm] literal-pool resolution")
    parser.add_argument("--json", action="store_true", help="emit JSON")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return _self_test()
    if not args.elf:
        parser.error("ELF path is required unless --self-test is used")

    ranges = parse_ranges(args.ranges or ["0xE0000000-0xE3FFFFFF"])
    data = Path(args.elf).read_bytes()
    try:
        sections, is_64, endian = parse_sections(data)
    except ValueError as exc:
        print(f"scan-mmio: {exc}", file=sys.stderr)
        return 2
    if not sections:
        sections = parse_segments(data, is_64, endian)
        if not sections:
            print("scan-mmio: no sections or loadable segments", file=sys.stderr)
            return 2

    hits = scan(data, sections, ranges, unaligned=args.unaligned,
                include_text=args.include_text)
    if not args.no_literals and endian == "<":
        hits += scan_arm_literals(data, sections, ranges)

    deduped: dict[tuple[int, int], Hit] = {}
    for hit in hits:
        deduped.setdefault((hit.value, hit.file_offset), hit)
    hits = list(deduped.values())
    hits.sort(key=lambda h: (h.value, h.section, h.vaddr))

    pages: dict[int, list[Hit]] = {}
    for hit in hits:
        pages.setdefault(page_of(hit.value), []).append(hit)

    if args.json:
        print(json.dumps(
            {
                "elf": str(args.elf),
                "ranges": [f"0x{lo:08x}-0x{hi:08x}" for lo, hi in ranges],
                "hits": [asdict(h) for h in hits],
                "pages": [
                    {"page": page, "hits": len(items)}
                    for page, items in sorted(pages.items(), key=lambda kv: (-len(kv[1]), kv[0]))
                ],
            },
            indent=2,
        ))
        return 0

    print(f"file: {args.elf}")
    print(f"ranges: {', '.join(f'0x{lo:08x}-0x{hi:08x}' for lo, hi in ranges)}")
    print(f"hits: {len(hits)} in {len(pages)} pages")
    print()
    print("pages (hit count, page base):")
    for page, items in sorted(pages.items(), key=lambda kv: (-len(kv[1]), kv[0])):
        print(f"  {len(items):4d}  0x{page:08x}")
    print()
    print("candidates:")
    for hit in hits:
        text = nearest_string(data, hit.file_offset)
        suffix = f"  ; near: {text}" if text else ""
        print(
            f"  0x{hit.value:08x}  {hit.section:<14} "
            f"vaddr=0x{hit.vaddr:08x} file=0x{hit.file_offset:x}{suffix}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
