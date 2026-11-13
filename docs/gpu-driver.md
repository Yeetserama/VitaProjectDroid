# Vita GPU Driver Research - re/gpu-driver

Status: research track (G4). Not a production driver.

Target: PCH-1000, PowerVR SGX543MP4+ (Series5XT), Kermit SoC, Linux 6.12.

## What exists

| Item | Status | Location |
|---|---|---|
| G0 desk research | Complete | `docs/reverse-engineering.md` |
| G1 hardware config | Complete | `docs/reverse-engineering.md` |
| G2 read-only probe | Built, CI-verified | `android-4.2.2` commit `8186de9`, `kernel/drivers/gpu/vita/vita-gpu-probe.c` |
| G3 firmware gate | **Unblocked** | `gpu_es4.skprx.elf` captured from boot image (`fwdump/kd/gpu_es4.skprx.elf`) |
| G4 kernel driver | In progress | `kernel/drivers/gpu/vita/vita-gpu-driver.c` |

## Captured firmware (from your console's boot image)

| File | SHA-256 | Location |
|---|---|---|
| `gpu_es4.skprx.elf` | d11047f57598463c14df1e508bd92ff23551b6ac781f94e92d06f19259830732 | `fwdump/kd/gpu_es4.skprx.elf` |
| `audio.skprx.elf` | c5e3e2c0fc468da994838ce9f72c5d9f8f942af4e725f4619acc56821f2b8b22 | `fwdump/kd/audio.skprx.elf` |
| `gpucoredump_es4.skprx.elf` | 1ff53a820d60257f162dff556d90208e79fcb10ad904864e7bd52d77e0522f41 | `fwdump/kd/gpucoredump_es4.skprx.elf` |

**Do not commit the .elf files** — they are in `fwdump/` which is gitignored. Record only hashes in `re/notes/manifest.md`.

## Hardware facts (confirmed from shoui520/psp2 clean-room reimplementation)

### MMIO register map

| Item | Address | Size | Source |
|---|---|---|---|
| SGX MMIO base | `0xE8400000` | `0x20000` | `sgx_regs.h` SGX_REG_BASE |
| Per-core bank n | `0xE8400000 + (n+2) * 0x4000` | `0x4000` | `sgx_regs.h` |
| Core 0 bank | `0xE8480000` | | computed |
| Core 1 bank | `0xE84C0000` | | computed |
| Core 2 bank | `0xE8500000` | | computed |
| Core 3 bank | `0xE8540000` | | computed |

### Pervasive block (clock/reset/power)

| Item | Address | Offset | Source |
|---|---|---|---|
| Pervasive Misc | `0xE3100000` | | `pervasive_regs.h` |
| Pervasive Reset | `0xE3101000` | | `pervasive_regs.h` |
| Pervasive Gate | `0xE3102000` | | `pervasive_regs.h` |
| Pervasive BaseClk | `0xE3103000` | | `pervasive_regs.h` |
| GPU reset/gate bit | Reset+0x10 / Gate+0x10 | bit mask | `pervasive_regs.h` `gpu` field |
| GPU clock | BaseClk+0x10 | core << 16 \| mp | `pervasive_regs.h` `gpuClock` |
| GPU isolation | Misc+0x34 | | `pervasive_regs.h` `gpuIsolation` |
| GPU bus | Pervasive bus 0 | | `pervasive.h` `ScePervasiveForDriver_84EAB5BC` on |

### SGX register offsets (from sgx543_v1.164defs.h)

Master bank (base `0xE8400000`):

| Offset | Register | Description |
|---|---|---|
| 0x0000 | EUR_CR_CLKGATECTL | Clock gating control |
| 0x0004 | EUR_CR_CLKGATECTL2 | Clock gating control 2 |
| 0x0008 | EUR_CR_CLKGATESTATUS | Clock gate status |
| 0x000C | EUR_CR_CLKGATECTLOVR | Clock gate override |
| 0x001C | EUR_CR_POWER | Power domain control |
| 0x0020 | EUR_CR_CORE_ID | Core ID |
| 0x0024 | EUR_CR_CORE_REVISION | Core revision |
| 0x0080 | EUR_CR_SOFT_RESET | Soft reset control |
| 0x0110 | EUR_CR_EVENT_HOST_ENABLE2 | IRQ enable 2 |
| 0x012C | EUR_CR_EVENT_STATUS | IRQ status |
| 0x0130 | EUR_CR_EVENT_HOST_ENABLE | IRQ enable |
| 0x0134 | EUR_CR_EVENT_HOST_CLEAR | IRQ clear |
| 0x0144 | EUR_CR_TIMER | Timer |
| 0x0AB0 | EUR_CR_EVENT_KICK1 | Kick 1 |
| 0x0AC0 | EUR_CR_EVENT_KICK2 | Kick 2 |
| 0x0AC4 | EUR_CR_EVENT_KICKER | Kicker address |
| 0x0AC8 | EUR_CR_EVENT_KICK | Kick |
| 0x0ACC | EUR_CR_EVENT_TIMER | Event timer |
| 0x4000 | EUR_CR_MASTER_CORE | Master core control |
| 0x4004 | (core enable mask) | Core enable |
| 0x4010 | EUR_CR_MASTER_CORE_ID | Master core ID |
| 0x4014 | EUR_CR_MASTER_CORE_REVISION | Master core revision |
| 0x4080 | EUR_CR_MASTER_SOFT_RESET | Master soft reset |
| 0x4C00 | EUR_CR_MASTER_BIF_CTRL | BIF control |
| 0x4CD0 | EUR_CR_MASTER_BIF_MMU_CTRL | MMU control |
| 0x4D00 | EUR_CR_MASTER_SLC_CTRL | SLC control |
| 0x4D04 | EUR_CR_MASTER_SLC_CTRL_BYPASS | SLC bypass |

Per-core bank (base + (core+2)*0x4000): same register offsets as master bank.

### SGX soft reset mask (from sgxreset_sce.c)

```
EUR_CR_MASTER_SOFT_RESET_BIF_RESET_MASK |
EUR_CR_MASTER_SOFT_RESET_IPF_RESET_MASK |
EUR_CR_MASTER_SOFT_RESET_DPM_RESET_MASK |
EUR_CR_MASTER_SOFT_RESET_MCI_RESET_MASK |
EUR_CR_MASTER_SOFT_RESET_VDM_RESET_MASK |
EUR_CR_MASTER_SOFT_RESET_PTLA_RESET_MASK |
EUR_CR_MASTER_SOFT_RESET_SLC_RESET_MASK |
EUR_CR_MASTER_SOFT_RESET_CORE_RESET_MASK(0) |
... CORE_RESET_MASK(1..3)
```

### IRQ handling (from sgxinit_sce.c SGX_ISRHandler)

- Read `EUR_CR_EVENT_STATUS` and `EUR_CR_EVENT_STATUS2`
- Mask with `EUR_CR_EVENT_HOST_ENABLE` / `EUR_CR_EVENT_HOST_ENABLE2`
- Clear with `EUR_CR_EVENT_HOST_CLEAR` / `EUR_CR_EVENT_HOST_CLEAR2`
- Master interrupt bit: `EUR_CR_EVENT_STATUS_MASTER_INTERRUPT_MASK` (bit 31)

### GPU MMU (from mmu.c)

- Page size: 4 KiB (SGX_MMU_PAGE_SIZE)
- Page directory: 1024 entries, 4 KiB each
- Page table: 1024 entries, 4 KiB each
- Page table entries: valid bit (0x1), write-only (0x2), read-only (0x4), cache-consistent (0x8), EDM-protect (0x10)
- Page directory entries: valid (0x1), page sizes: 4K/16K/64K/256K/1M/4M
- Two-level page table: directory -> page table -> physical page

### MMU heaps (from sgxinit_sce.c)

22 heaps defined in `SGXRegisterDevice`:

| ID | Base | Size | Attribs | Type |
|---|---|---|---|---|
| 0x07000000 | 0x40000000 | 0x0ffff000 | 0x14200 | 0 |
| 0x07000001 | 0x50000000 | 0x0cfff000 | 0x24200 | 0 |
| 0x07000002 | 0x14000000 | 0x0007f000 | 0x24200 | 3 |
| 0x07000003 | 0x1c000000 | 0x03fff000 | 0x24200 | 3 |
| 0x07000004 | 0x30000000 | 0x02000000 | 0x14200 | 0 |
| 0x07000005 | 0x34000000 | 0x02000000 | 0x14200 | 0 |
| 0x07000006 | 0x10000000 | 0x01fff000 | 0x14200 | 0 |
| 0x07000007 | 0x18000000 | 0x01bff000 | 0x14200 | 0 |
| 0x07000008 | 0x0c000000 | 0x00fff000 | 0x24200 | 3 |
| 0x07000009 | 0x28000000 | 0x04000000 | 0x24200 | 3 |
| 0x0700000A | 0x20000000 | 0x04000000 | 0x14200 | 0 |
| 0x0700000C | 0x04000000 | 0x03eff000 | 0x14200 | 3 |
| 0x0700000E | 0x80000000 | 0x80000000 | 0x14200 | 0 |
| 0x0700000F | 0x60000000 | 0x08000000 | 0x14200 | 0 |
| 0x07000010 | 0x24000000 | 0x04000000 | 0x114200 | 0 |
| 0x07000011 | 0x2c000000 | 0x04000000 | 0x124200 | 3 |
| 0x07000012 | 0x38000000 | 0x00800000 | 0x14200 | 0 |
| 0x07000013 | 0x3c000000 | 0x00800000 | 0x14200 | 0 |
| 0x07000014 | 0x68000000 | 0x18000000 | 0x14200 | 0 |
| 0x07000015 | 0x08000000 | 0x00002000 | 0x24200 | 3 |

### Init script (from gpuinit_es4/devices/sgx/sgxinit.c)

Part 1 (enable cores and clocks):
- `EUR_CR_MASTER_CORE` = 3
- `0x4020` = 0x2a9 (core clock config)
- Core enable mask = 0xa2 (cores 0,2,4,6 -> bits 1,3,5,7)
- `0x4004` = coreMask

Part 2 (events, code bases, microkernel PDS):
- `EUR_CR_EVENT_HOST_ENABLE` = 0x4000
- `EUR_CR_EVENT_HOST_ENABLE2` = 0xc00
- `0x13c` = 0
- `0xa7c` = 0, `0xa80` = 0, `0xa74` = 0x208200
- `0xabc` = 0x40, `0xab8` = 0
- `0x800` = 0x10, `0x818` = 0
- `0x804` = 0x5e0, `0x814` = 0xffff
- `0xa00` = 0x7c000
- USE_CODE_BASE_4..7 = pixelShader base + i*0x800000 >> 6 | DM(1)
- USE_CODE_BASE_0..3 = vertexShader base + i*0x800000 >> 6
- USE_CODE_BASE_9 = useCodeBase9 >> 6 | DM(1)
- USE_CODE_BASE_8 = useCodeBase8 >> 6
- USE_CODE_BASE_10..14 = 0
- USE_CODE_BASE_15 = kernelCode >> 6 | DM(3)
- `0xb30` = 0x100
- `EUR_CR_EVENT_KICKER` = CCB event kicker address
- `0xa68` = pixelPDSExec, `0xa6c` = 3, `0xa70` = 0x12001
- `0x8a68` = corePDSExec, `0x8a6c` = 1, `0x8a70` = 0x12001
- `0x8a58` = 0x2200c000
- `0x4a58` = 0x804300f
- `ui32CacheControl` = 6
- `0x4808` = 4
- `0x4144` = 0x103e8
- `0x414c` = 0
- `0xcba0` = corePDSExec (core 0)
- `0x10ba0` = corePDSExec (core 1)
- `0x14ba0` = corePDSExec (core 2)
- `0x8ba0` = corePDSExec (core 3)
- `0xbb4` = 0

Deinit script:
- `0xa58` = 0, `0xa68` = 0, `0xa6c` = 0, `0xa70` = 0
- `EUR_CR_USE_CODE_BASE_15` = 0
- `EUR_CR_EVENT_HOST_ENABLE` = 0
- `EUR_CR_EVENT_HOST_ENABLE2` = 0
- `EUR_CR_CLKGATECTL` = 0
- `EUR_CR_CLKGATECTL2` = 0

## What a real driver needs (G4)

1. **Kernel driver** — DRM/GEM, command ring, GPU MMU programming, IRQ handler, power domains, clocks, resets, firmware loader
2. **Firmware** — `gpu_es4.skprx.elf` now captured from boot image; need to determine container/encryption/load protocol
3. **Userspace UMD** — GLES2 entry points + GLSL → SGX ISA compiler (not available)
4. **Android glue** — EGL, gralloc, HWC, presenter (not available)

## What this branch provides

- `kernel/drivers/gpu/vita/vita-gpu-driver.c` — platform driver skeleton:
  - Maps pervasive reset/gate/clock/misc pages
  - Powers GPU on (deassert reset, enable clock)
  - Runs SGX init script (core enable, clock config, event setup)
  - Maps SGX MMIO master bank + all 4 core banks
  - Reads ID/revision/status registers
  - Sets up command-ring infrastructure (DMA-coherent, double-buffered)
  - IRQ handler stub (reads EUR_CR_EVENT_STATUS, clears via EUR_CR_EVENT_HOST_CLEAR)
  - Powers GPU off on removal
- `kernel/drivers/gpu/vita/Kconfig` — `CONFIG_VITA_GPU_DRIVER` (depends on `ARCH_VITA`, off by default)
- `kernel/drivers/gpu/vita/Makefile` — build rule
- `kernel/config/vita-android-4.2.fragment` — `CONFIG_VITA_GPU_DRIVER=y` (ON for CI testing)
- `docs/gpu-driver.md` — this file
- `re/notes/gpu-g2.md` — G2 probe plan
- `re/notes/gpu-g0.md` — G0 desk research

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
vita-gpu-driver: Command ring 0: cpu=0x... dma=0x... size=0x4000
vita-gpu-driver: Command ring 1: cpu=0x... dma=0x... size=0x4000
vita-gpu-driver: Registered IRQ 52
vita-gpu-driver: GPU driver probe completed (skeleton — no job submission)
```

## Blocked items (G3 / G4)

| Blocker | Why | Needed to proceed |
|---|---|---|
| SGX firmware load protocol | `gpu_es4.skprx.elf` captured but container/encryption unknown | Analyze ELF, determine firmware load address and protocol |
| GLSL compiler | Not available for SGX5 | Userspace UMD project |
| GLES UMD | Sony `libgxm`, incompatible with Linux | Userspace UMD project |
| EGL/gralloc/HWC | Not written | Android userspace project |

## Do not

- Do not write unknown SGX registers beyond clock/reset/command-ring setup
- Do not attempt firmware download without determining load protocol from `gpu_es4.skprx.elf`
- Do not merge into boot-kit branches unless a triangle renders

## Related

- `docs/reverse-engineering.md` — overall RE plan
- `re/notes/gpu-g0.md` — G0 desk research
- `re/notes/gpu-g2.md` — G2 probe plan
- `re/notes/manifest.md` — firmware dump hashes
- `fwdump/kd/gpu_es4.skprx.elf` — captured firmware (gitignored)
- `shoui520/psp2` — clean-room reimplementation source (reference only)
- openpvrsgx: https://github.com/openpvrsgx-devgroup/linux_openpvrsgx
