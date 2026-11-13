# Vita GPU Driver Research - re/gpu-driver

Status: research track (G4). Not a production driver.

Target: PCH-1000, PowerVR SGX543MP4+ (Series5XT), Kermit SoC, Linux 6.12.

## What exists

| Item | Status | Location |
|---|---|---|
| G0 desk research | Complete | `docs/reverse-engineering.md` |
| G1 hardware config | Complete | `docs/reverse-engineering.md` |
| G2 read-only probe | Built, CI-verified | `android-4.2.2` commit `8186de9`, `kernel/drivers/gpu/vita/vita-gpu-probe.c` |
| G3 firmware gate | **Blocked** | SGX firmware is inside encrypted VitaOS (`gpu_es4.skprx` not on this console) |
| G4 kernel driver | This branch | Skeletons below |

## Hardware facts (confirmed)

| Item | Value | Source |
|---|---|---|
| GPU MMIO base | `0xE8400000` (size `0x20000`) | shoui520/psp2 `sgx_regs.h` |
| GPU IRQ | `0x34` (52) | shoui520/psp2 `register-map.c` |
| Per-core bank n | `0xE8400000 + (n+2) * 0x4000` | shoui520/psp2 `sgx_regs.h` |
| Core 0 bank | `0xE8480000` | computed |
| Core 1 bank | `0xE84C0000` | computed |
| Core 2 bank | `0xE8500000` | computed |
| Core 3 bank | `0xE8540000` | computed |
| SGX_ID register | base + 0x0000 | openpvrsgx + Vita3K |
| SGX_STATUS | base + 0x0004 | openpvrsgx + Vita3K |
| SGX_CONFIG | base + 0x0008 | openpvrsgx + Vita3K |
| SGX_CLOCK_GATE | base + 0x0010 | openpvrsgx + Vita3K |
| SGX_RESET | base + 0x0014 | openpvrsgx + Vita3K |
| Pervasive reset GPU | `0xE3101010` | shoui520/psp2 `pervasive_regs.h` |
| Pervasive gate GPU | `0xE3102010` | shoui520/psp2 `pervasive_regs.h` |
| Pervasive GPU clock | `0xE3103010` | shoui520/psp2 `pervasive_regs.h` |

## What a real driver needs (G4)

1. **Kernel driver** — DRM/GEM, command ring, GPU MMU programming, IRQ handler, power domains, clocks, resets, firmware loader (~30k lines for an SGX5 DDK port)
2. **Firmware** — SGX microcode for TA/3D pipelines (G3 gate — not available)
3. **Userspace UMD** — GLES2 entry points + GLSL → SGX ISA compiler (not available)
4. **Android glue** — EGL, gralloc, HWC, presenter (not available)

## What this branch provides

- `kernel/drivers/gpu/vita/vita-gpu-driver.c` — platform driver skeleton:
  - Maps pervasive reset/gate/clock pages
  - Powers GPU on (deassert reset, enable clock)
  - Maps SGX MMIO master bank + all 4 core banks
  - Reads ID/revision/status registers
  - Sets up command-ring infrastructure placeholders
  - IRQ handler stub
  - Powers GPU off on removal
- `kernel/drivers/gpu/vita/Kconfig` — `CONFIG_VITA_GPU_DRIVER` (depends on `ARCH_VITA`, off by default)
- `kernel/drivers/gpu/vita/Makefile` — build rule
- `kernel/config/vita-gpu-driver.fragment` — config fragment (off by default)
- `docs/gpu-driver.md` — this file

## Testing

The driver is **off by default** (`CONFIG_VITA_GPU_DRIVER=n`). To test:

1. Enable `CONFIG_VITA_GPU_DRIVER=y` in a kernel config
2. Build and flash to `ux0:/linux/zImage`
3. Boot with CROSS
4. `adb shell dmesg | grep vita-gpu-driver`

Expected output:
```
vita-gpu-driver: Vita SGX543MP4+ driver probe starting
vita-gpu-driver: Mapped pervasive misc at 0xE3100000
vita-gpu-driver: Mapped pervasive reset at 0xE3101000
vita-gpu-driver: Mapped pervasive gate at 0xE3102000
vita-gpu-driver: Mapped pervasive baseclk at 0xE3103000
vita-gpu-driver: Deasserting GPU reset
vita-gpu-driver: Enabling GPU clock
vita-gpu-driver: Mapped SGX master at 0xE8400000
vita-gpu-driver: SGX master ID: 0x5430xxxx (SGX543 core)
vita-gpu-driver: Command ring base: 0x...
vita-gpu-driver: IRQ 52 registered
```

## Blocked items (G3 / G4)

| Blocker | Why | Needed to proceed |
|---|---|---|
| SGX firmware | Encrypted inside `gpu_es4.skprx`, not on this console | G3 pass: firmware extraction + load protocol |
| GLSL compiler | Not available for SGX5 | Userspace UMD project |
| GLES UMD | Sony `libgxm`, incompatible with Linux | Userspace UMD project |
| EGL/gralloc/HWC | Not written | Android userspace project |

## Do not

- Do not write unknown SGX registers beyond clock/reset/command-ring setup
- Do not attempt firmware download without G3 pass
- Do not merge into boot-kit branches unless a triangle renders

## Related

- `docs/reverse-engineering.md` — overall RE plan
- `re/notes/gpu-g0.md` — G0 desk research
- `re/notes/gpu-g2.md` — G2 probe plan
- `android-4.2.2` — G2 probe commit `8186de9`
- openpvrsgx: https://github.com/openpvrsgx-devgroup/linux_openpvrsgx
