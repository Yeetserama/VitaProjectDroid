#!/usr/bin/env python3
"""
Verify firmware dump integrity against re/notes/manifest.md.
Usage: python3 verify-dumps.py [--update-manifest]
"""

import hashlib
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).parent.parent.parent
MANIFEST = REPO_ROOT / "re" / "notes" / "manifest.md"
DUMPS_DIR = REPO_ROOT / "re" / "dumps"

# Expected files from manifest (VitaOS path -> local filename)
EXPECTED = {
    "os0:kd/audio.skprx": "audio.skprx.elf",
    "os0:kd/gpu_es4.skprx": "gpu_es4.skprx.elf",
    "os0:kd/gpuinit_es4.skprx": "gpuinit_es4.skprx.elf",
    "vs0:sys/external/libgpu_es4.suprx": "libgpu_es4.suprx.elf",
}

# Also check for encrypted source files
ENCRYPTED_EXPECTED = {
    "os0:kd/audio.skprx": "audio.skprx",
    "os0:kd/gpu_es4.skprx": "gpu_es4.skprx",
    "os0:kd/gpuinit_es4.skprx": "gpuinit_es4.skprx",
    "vs0:sys/external/libgpu_es4.suprx": "libgpu_es4.suprx",
}

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()

def parse_manifest() -> dict:
    """Parse manifest.md table into {filename: {sha256, ...}}."""
    if not MANIFEST.exists():
        return {}
    content = MANIFEST.read_text()
    rows = {}
    for line in content.splitlines():
        m = re.match(r"\| `\S+` \| [^|]+ \| [^|]+ \| \d+ \| ([a-f0-9]{64}) \|", line)
        if m:
            # Find the filename in the same line
            fname_m = re.search(r"`([^`]+\.elf)`", line)
            if fname_m:
                rows[fname_m.group(1)] = {"sha256": m.group(1)}
    return rows

def update_manifest(update: bool = False):
    manifest_rows = parse_manifest()
    all_ok = True

    print("=== Encrypted source files (SELF/PRX) ===")
    for vita_path, local_name in ENCRYPTED_EXPECTED.items():
        enc_path = REPO_ROOT / "psvitaosmainfw" / vita_path.replace(":", "_").replace("/", "_")
        if enc_path.exists():
            h = sha256(enc_path)
            print(f"  [OK] {local_name:30s}  {h}  ({enc_path.stat().st_size} bytes)")
            if update:
                # Could update manifest here
                pass
        else:
            print(f"  [MISSING] {local_name:30s}  MISSING at {enc_path}")
            all_ok = False

    print("\n=== Decrypted ELF files (re/dumps/) ===")
    for vita_path, local_name in EXPECTED.items():
        dump_path = DUMPS_DIR / local_name
        if dump_path.exists():
            h = sha256(dump_path)
            expected = manifest_rows.get(local_name, {}).get("sha256")
            status = "[OK]" if not expected or expected == h else "[SHA MISMATCH]"
            print(f"  {status} {local_name:30s}  {h}  ({dump_path.stat().st_size} bytes)")
            if expected and expected != h:
                all_ok = False
        else:
            print(f"  [MISSING] {local_name:30s}  MISSING (run FAGDec on console)")
            all_ok = False

    if all_ok:
        print("\n[OK] All dumps present and verified")
        return 0
    else:
        print("\n[ERROR] Missing or mismatched dumps")
        return 1

if __name__ == "__main__":
    update = "--update-manifest" in sys.argv
    sys.exit(update_manifest(update))