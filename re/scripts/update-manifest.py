#!/usr/bin/env python3
"""
Update manifest.md with actual dump SHA-256 hashes.
Usage: python3 update-manifest.py --firmware 3.65 --console PCH-1000
"""

import hashlib
import sys
import re
from pathlib import Path
from datetime import datetime

REPO_ROOT = Path(__file__).parent.parent.parent
MANIFEST = REPO_ROOT / "re" / "notes" / "manifest.md"
DUMPS_DIR = REPO_ROOT / "re" / "dumps"
FW_DIR = REPO_ROOT / "psvitaosmainfw"

EXPECTED_DECRYPTED = {
    "audio.skprx.elf": "os0:kd/audio.skprx",
    "gpu_es4.skprx.elf": "os0:kd/gpu_es4.skprx",
    "gpuinit_es4.skprx.elf": "os0:kd/gpuinit_es4.skprx",
    "libgpu_es4.suprx.elf": "vs0:sys/external/libgpu_es4.suprx",
}

EXPECTED_ENCRYPTED = {
    "audio.skprx": "os0:kd/audio.skprx",
    "gpu_es4.skprx": "os0:kd/gpu_es4.skprx",
    "gpuinit_es4.skprx": "os0:kd/gpuinit_es4.skprx",
    "libgpu_es4.suprx": "vs0:sys/external/libgpu_es4.suprx",
}

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()

def find_file(filename: str, search_dirs: list[Path]) -> Path | None:
    for d in search_dirs:
        for ext in ["", ".skprx", ".suprx", ".elf"]:
            p = d / (filename + ext)
            if p.exists():
                return p
        # Also search recursively
        for p in d.rglob(filename):
            if p.is_file():
                return p
    return None

def read_manifest() -> str:
    return MANIFEST.read_text() if MANIFEST.exists() else ""

def write_manifest(content: str) -> None:
    MANIFEST.write_text(content)

def update_table_row(content: str, filename: str, sha256_hash: str, size: int) -> str:
    """Update or insert a row in the manifest table."""
    pattern = rf"(\| `{re.escape(filename)}` \| [^|]* \| [^|]* \|) \d+ (\| [a-f0-9]{{64}} \|)"
    replacement = rf"\1 {size} \2"
    new_content = re.sub(pattern, replacement, content)
    
    if new_content == content:
        # Row not found, need to insert
        # Find the table header and insert after it
        lines = content.splitlines()
        for i, line in enumerate(lines):
            if "| File | VitaOS path | Module | Bytes | SHA-256 | Magic | Notes |" in line:
                # Insert after separator line
                insert_idx = i + 2
                if insert_idx < len(lines):
                    vita_path = EXPECTED_DECRYPTED.get(filename, EXPECTED_ENCRYPTED.get(filename, "unknown"))
                    new_row = f"| `{filename}` | `{vita_path}` | TODO | {size} | {sha256_hash} | ELF | |"
                    lines.insert(insert_idx, new_row)
                    return "\n".join(lines) + "\n"
    return new_content

def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--firmware", default="3.65")
    parser.add_argument("--console", default="PCH-1000")
    parser.add_argument("--method", default="FAGDec on console")
    args = parser.parse_args()
    
    content = read_manifest()
    updated = False
    
    print(f"=== Updating manifest: firmware={args.firmware}, console={args.console} ===\n")
    
    # Update header info
    content = re.sub(r"Firmware: .*", f"Firmware: {args.firmware} (HENkaku/Enso), {args.console}.", content)
    content = re.sub(r"Capture method: .*", f"Capture method: {args.method}, output ELF.", content)
    content = re.sub(r"Status: .*", f"Status: Updated {datetime.utcnow().isoformat()}Z", content)
    
    # Process decrypted ELF files
    for filename, vita_path in EXPECTED_DECRYPTED.items():
        fpath = find_file(filename, [DUMPS_DIR])
        if fpath:
            h = sha256(fpath)
            size = fpath.stat().st_size
            print(f"  [OK] {filename:30s}  {h}  ({size} bytes)")
            content = update_table_row(content, filename, h, size)
            updated = True
        else:
            print(f"  [MISSING] {filename:30s}  NOT FOUND in {DUMPS_DIR}")
    
    # Process encrypted source files
    print("\nEncrypted source files:")
    for filename, vita_path in EXPECTED_ENCRYPTED.items():
        fpath = find_file(filename, [FW_DIR])
        if fpath:
            h = sha256(fpath)
            size = fpath.stat().st_size
            print(f"  [OK] {filename:30s}  {h}  ({size} bytes)")
            # Update SELF hashes table
            pattern = rf"(\| `{re.escape(filename)}` \|) \d+ (\| [a-f0-9]{{64}} \|)"
            replacement = rf"\1 {size} \2"
            content = re.sub(pattern, replacement, content)
            updated = True
        else:
            print(f"  [MISSING] {filename:30s}  NOT FOUND in firmware dump")
    
    if updated:
        write_manifest(content)
        print(f"\n[OK] Manifest updated: {MANIFEST}")
    else:
        print("\n[WARN] No updates made")

if __name__ == "__main__":
    main()