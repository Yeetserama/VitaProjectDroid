# GPU G3 - Firmware Gate Research Notes

Status: in progress. `gpu_es4.skprx.elf` captured from boot image decryption.

Target: PCH-1000, PowerVR SGX543MP4+ (Series5XT), Linux 6.12.

## Captured firmware

| File | SHA-256 | Size | Location |
|---|---|---|---|
| `gpu_es4.skprx.elf` | d11047f57598463c14df1e508bd92ff23551b6ac781f94e92d06f19259830732 | 71,924 | `fwdump/kd/gpu_es4.skprx.elf` (gitignored) |

**Do not commit the .elf file** — `fwdump/` is gitignored. Record only the hash in `re/notes/manifest.md`.

## What we know from shoui520/psp2 reimplementation

The `gpu_es4.skprx` module (SceGpuEs4) is the VitaOS kernel driver for the SGX543MP4+. From the clean-room reimplementation in `shoui520/psp2/sys/internal/driver/src/gpu_es4/srvkm/`:

### Module entry points (from gpu_es4.c)
- `module_start()` — initializes GPU driver, registers process event handlers
- `PVRSRVOpen()` — connects process to GPU services
- `PVRSRVRelease()` — disconnects process

### Init sequence (from sgxinit_sce.c / gpuinit_es4/devices/sgx/sgxinit.c)

1. Allocate kernel CCB (command control block) memory
2. Allocate kernel CCB control memory
3. Allocate kernel CCB event kicker memory
4. Allocate TA 3D control memory (SGXMKIF_TA3D_CTL)
5. Allocate host control memory (SGXMKIF_HOST_CTL)
6. Allocate TA signature buffer (0x418 bytes)
7. Allocate 3D signature buffer (0x724 bytes)
8. Allocate microkernel program memory (program0-program10)
9. Build init script part 1 (enable cores, clocks)
10. Build init script part 2 (events, code bases, PDS programs)
11. Build deinit script
12. Map peripherals (Perfmon, bookmark)
13. Call `DevInitSGXPart2KM()` to complete init

### Microkernel programs (from gpuinit_es4/programs.S and pds/)
- `program0` — master core microkernel (16,030 bytes)
- `program1` — slave core microkernel (2,826 bytes)
- `program2`..`program10` — PDS programs, clear clip workaround, etc.

### Register scripts (from sgxinit_sce.c SGXRunScript)
The init script is a sequence of `SGX_INIT_OP_WRITE_HW_REG` commands:
- Write register offset + value
- `SGX_INIT_OP_HALT` to terminate

### Firmware loading (from sgxinit_sce.c)
The SGX init loads microkernel programs into GPU memory via `SGXRunScript()` which writes registers directly. The firmware is not a separate binary blob — it's the microkernel programs embedded in `gpuinit_es4.skprx`.

## G3 gate analysis

To determine if SGX firmware can be legally and technically loaded by our driver:

### Technical feasibility
- ✅ `gpu_es4.skprx.elf` captured from own console
- ✅ Init sequence documented in shoui520/psp2 reimplementation
- ✅ Microkernel programs available (program0-program10)
- ✅ Register write script format known (SGX_INIT_OP_WRITE_HW_REG)
- ❓ Firmware container format unknown (is it a plain ELF or custom?)
- ❓ Load addresses for microkernel programs need verification
- ❓ PDS program format needs documentation

### Legal feasibility
- ✅ Firmware captured from user's own console
- ✅ Reverse engineering for interoperability is generally permitted
- ❓ Redistribution of firmware binary not permitted (fwdump/ is gitignored)
- ❓ Driver must extract firmware at runtime from user's console, not ship it

## Next steps for G3

1. Analyze `gpu_es4.skprx.elf` structure (ELF headers, sections, symbols)
2. Compare with `gpuinit_es4.skprx.elf` (also captured) to understand init/firmware split
3. Determine microkernel program load addresses from `sgxinit.c`
4. Document the register script format for our driver
5. Decide if firmware can be extracted at runtime from user's console

## Related

- `re/notes/gpu-g2.md` — G2 read-only probe (prerequisite)
- `re/notes/gpu-g0.md` — G0 desk research
- `docs/gpu-driver.md` — G4 driver skeleton
- `shoui520/psp2/sys/internal/driver/src/gpu_es4/srvkm/` — reference implementation
- `shoui520/psp2/sys/internal/driver/src/gpuinit_es4/` — init server implementation
- `fwdump/kd/gpu_es4.skprx.elf` — captured firmware (gitignored)
