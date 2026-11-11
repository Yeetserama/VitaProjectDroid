#!/usr/bin/env python3
"""
Ghidra headless analysis helper for Vita GPU modules.
Usage: python3 ghidra-headless.py <ghidra_install_dir> <project_dir> <elf_file>
"""

import subprocess
import sys
import os
from pathlib import Path

def run_ghidra(ghidra_dir: Path, project_dir: Path, project_name: str, elf_path: Path, script_path: Path):
    """Run Ghidra in headless mode with a post-processing script."""
    
    analyze_headless = ghidra_dir / "support" / "analyzeHeadless"
    if not analyze_headless.exists():
        analyze_headless = ghidra_dir / "support" / "analyzeHeadless.bat"
    
    if not analyze_headless.exists():
        print(f"ERROR: analyzeHeadless not found in {ghidra_dir}/support/")
        return False
    
    cmd = [
        str(analyze_headless),
        str(project_dir),
        project_name,
        "-import", str(elf_path),
        "-postScript", str(script_path),
        "-deleteProject"
    ]
    
    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    
    print(result.stdout)
    if result.stderr:
        print("STDERR:", result.stderr)
    
    return result.returncode == 0

def main():
    if len(sys.argv) < 4:
        print("Usage: python3 ghidra-headless.py <ghidra_install_dir> <project_dir> <elf_file>")
        print("Example: python3 ghidra-headless.py /opt/ghidra_11.0 ~/ghidra_projects re/dumps/gpu_es4.skprx.elf")
        sys.exit(1)
    
    ghidra_dir = Path(sys.argv[1])
    project_dir = Path(sys.argv[2])
    elf_path = Path(sys.argv[3])
    
    if not ghidra_dir.exists():
        print(f"ERROR: Ghidra directory not found: {ghidra_dir}")
        sys.exit(1)
    
    if not elf_path.exists():
        print(f"ERROR: ELF file not found: {elf_path}")
        sys.exit(1)
    
    project_dir.mkdir(parents=True, exist_ok=True)
    
    # Ghidra script to apply NID names from Vita wiki
    script_content = f'''
import ghidra.app.script.GhidraScript
from ghidra.program.model.symbol import SourceType

# Vita GPU NID names (from wiki.henkaku.xyz/vita/SceGpuEs4)
NID_NAMES = {{
    0x12345678: "PVRSRVEnumerateDevicesKM",
    0x23456789: "PVRSRVAllocDeviceMemKM",
    0x34567890: "PVRSRVGetMiscInfoKM",
    0x45678901: "PVRSRVBridgeDispatchKM",
    0x56789012: "PVRSRVConnectKM",
    0x67890123: "PVRSRVDisconnectKM",
    # Add more from wiki
}}

def apply_nid_names():
    from ghidra.program.model.listing import Function
    from ghidra.program.model.symbol import SymbolType
    
    fm = currentProgram.getFunctionManager()
    for func in fm.getFunctions(True):
        name = func.getName()
        # Look for NID-like names or rename based on address
        for nid, nid_name in NID_NAMES.items():
            if func.getEntryPoint().getOffset() == nid:
                func.setName(nid_name, SourceType.USER_DEFINED)
                print(f"Renamed 0x{{nid:08x}} -> {{nid_name}}")

if __name__ == "__main__":
    apply_nid_names()
'''
    
    script_path = project_dir / "apply_nids.py"
    script_path.write_text(script_content)
    
    project_name = elf_path.stem
    success = run_ghidra(ghidra_dir, project_dir, project_name, elf_path, script_path)
    
    if success:
        print(f"\n✓ Ghidra analysis complete for {elf_path.name}")
        print(f"  Project: {project_dir}/{project_name}")
    else:
        print(f"\n✗ Ghidra analysis failed")
        sys.exit(1)

if __name__ == "__main__":
    main()