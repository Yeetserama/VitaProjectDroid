#!/usr/bin/env python3
"""
Extract SGX firmware from decrypted SceGpuEs4 ELF.
Usage: python3 extract-firmware.py re/dumps/gpu_es4.skprx.elf [output_dir]

The SGX firmware is embedded in the kernel module as a binary blob.
This script locates it by searching for the "PVR" magic header.
"""

import sys
import struct
from pathlib import Path

PVR_MAGIC = b"PVR"
PVR_MAGIC_LE = struct.unpack("<I", PVR_MAGIC + b"\x00")[0]

def find_firmware_blobs(elf_path: Path, min_size: int = 0x1000):
    """Find potential firmware blobs in ELF by scanning for PVR magic."""
    data = elf_path.read_bytes()
    hits = []
    
    for i in range(len(data) - 4):
        if data[i:i+4] == PVR_MAGIC + b"\x00" or data[i:i+3] == PVR_MAGIC:
            # Found potential header, try to parse
            if i + 0x40 <= len(data):
                header = data[i:i+0x40]
                hits.append((i, header))
    
    return hits

def parse_pvr_header(header: bytes, offset: int):
    """Parse PVR firmware header (based on openpvrsgx format)."""
    if len(header) < 0x40:
        return None
    
    # Common PVR firmware header structure
    magic, version, build, size = struct.unpack("<4sIII", header[:16])
    
    if magic[:3] != PVR_MAGIC:
        return None
    
    return {
        "offset": offset,
        "magic": magic.decode('ascii', errors='ignore'),
        "version": version,
        "build": build,
        "size": size,
    }

def extract_segments(data: bytes, start: int, fw_size: int):
    """Extract firmware segments (code, data, bss)."""
    # This is simplified — real parser needs to match openpvrsgx
    # See drivers/gpu/drm/pvr/pvr_firmware.c
    return data[start:start+fw_size]

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 extract-firmware.py <decrypted_gpu_es4.elf> [output_dir]")
        sys.exit(1)
    
    elf_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else elf_path.parent / "firmware"
    
    if not elf_path.exists():
        print(f"ERROR: File not found: {elf_path}")
        sys.exit(1)
    
    out_dir.mkdir(parents=True, exist_ok=True)
    
    print(f"Scanning {elf_path} for SGX firmware...")
    hits = find_firmware_blobs(elf_path)
    
    if not hits:
        print("[ERROR] No PVR magic found. Firmware may be:")
        print("  - Encrypted within the module")
        print("  - Loaded from separate partition at runtime")
        print("  - In gpuinit_es4.skprx instead")
        sys.exit(1)
    
    print(f"[OK] Found {len(hits)} potential firmware header(s):")
    
    data = elf_path.read_bytes()
    for i, (offset, header) in enumerate(hits):
        parsed = parse_pvr_header(header, offset)
        if parsed:
            print(f"  [{i}] Offset 0x{offset:08x}: {parsed}")
            
            # Extract firmware blob
            fw_size = parsed["size"]
            if fw_size > 0 and fw_size < len(data) - offset:
                fw_data = extract_segments(data, offset, fw_size)
                out_path = out_dir / f"sgx_fw_{i}_0x{offset:08x}.img"
                out_path.write_bytes(fw_data)
                print(f"      → Extracted {len(fw_data)} bytes to {out_path}")
            else:
                print(f"      → Invalid size: {fw_size}")
        else:
            print(f"  [{i}] Offset 0x{offset:08x}: PVR magic but invalid header")
    
    # Also search for known SGX firmware strings
    strings_to_find = [b"SGX", b"Series5", b"PVRSRV", b"IMG ", b"PowerVR"]
    print("\nSearching for firmware strings...")
    for s in strings_to_find:
        pos = data.find(s)
        if pos >= 0:
            print(f"  Found '{s.decode()}' at offset 0x{pos:08x}")
            # Dump surrounding context
            ctx_start = max(0, pos - 0x100)
            ctx_end = min(len(data), pos + 0x100)
            ctx_file = out_dir / f"context_{s.decode()}_0x{pos:08x}.bin"
            ctx_file.write_bytes(data[ctx_start:ctx_end])

if __name__ == "__main__":
    main()