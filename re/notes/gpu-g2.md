# GPU G2 - Read-only probe plan

Status: G0 and G1 complete; G2 probe not yet implemented.

## Confirmed hardware configuration (from shoui520/psp2 clean-room reimplementation)

| Item | Value | Source |
| --- | --- | --- |
| GPU | PowerVR SGX543MP4+ (Series5XT) | shoui520/psp2 |
| MMIO base | `0xE8400000`, size `0x20000` | `sgx_regs.h` SGX_REG_BASE |
| IRQ | `0x34` (52) | `register-map.c` |
| Per-core bank | `(core + 2) << 14` (0x4000 per core) | `sgx_regs.h` |
| Core 0 bank | `0xE8480000` | computed |
| Core 1 bank | `0xE84C0000` | computed |
| Core 2 bank | `0xE8500000` | computed |
| Core 3 bank | `0xE8540000` | computed |
| Counter regs (per bank) | `0x70` value, `0x78` enable, `0x90` select1, `0x94` select2 | `sgx_regs.h` |
| Reset reg | `EUR_CR_MASTER_SOFT_RESET` at base+0x0000 | `sgxreset_sce.c` |
| Pervasive Reset GPU | `0xE3101010` (bit mask) | `pervasive_regs.h` `gpu` field |
| Pervasive Gate GPU | `0xE3102010` (bit mask) | `pervasive_regs.h` `gpu` field |
| Pervasive GPU Clock | `0xE3103010` (core << 16 \| mp) | `pervasive_regs.h` `gpuClock` field |
| Pervasive GPU Bus | bus 0: `ScePervasiveForDriver_84EAB5BC` on / `_E8656ADF` off | `pervasive.h` |
| GPU Isolation | Misc `gpuIsolation` at `0xE3100034` | `pervasive_regs.h` |

## Pervasive register layout (from shoui520/psp2 `pervasive_regs.h`)

Reset page `0xE3101000` and Gate page `0xE3102000` share the same structure:

| Offset | Field | Device |
| --- | --- | --- |
| 0x00 | arm | gate only |
| 0x04 | armDebugger | |
| 0x0C | reserved1[2] | |
| 0x10 | gpu | **GPU reset/gate** |
| 0x20 | allegrex | |
| 0x24 | sonyRegbus | |
| 0x28 | compatRam | |
| 0x30 | venezia | |
| 0x34 | vip | |
| 0x40 | sdio[2] | |
| 0x50 | dmac[7] | |
| 0xC0 | i2s[8] | |
| 0xE0 | srcMix[3] | |
| 0xF0 | spdif | |

Misc page `0xE3100000`:
- `0x00` revision (SoC revision)
- `0x34` gpuIsolation (set while GPU powered off)

BaseClk page `0xE3103000`:
- `0x10` gpuClock (core << 16 \| mp)

## Probe sequence (read-only, no GPU register writes beyond reset/clock)

1. Map `0xE3100000` (misc), `0xE3101000` (reset), `0xE3102000` (gate), `0xE3103000` (baseclk)
2. Read SoC revision from misc+0x00
3. Read GPU isolation from misc+0x34
4. Read GPU reset state from reset+0x10
5. Read GPU gate state from gate+0x10
6. Read GPU clock from baseclk+0x10
7. Deassert GPU reset: clear GPU bit in reset page (write 0 to clear)
8. Enable GPU clock: set GPU bit in gate page
9. Map `0xE8400000` size `0x20000` (SGX master register block)
10. Read master bank ID/revision/status registers
11. Read per-core counter regs from each core bank
12. Print all values to dmesg
13. Leave GPU in powered state (do not reset again)

## What to look for in dmesg

- SGX core ID/revision (should be a plausible SGX543 value)
- Counter register values (nonzero = clock running)
- No bus hang after reset deassert + clock enable

## Gate

A plausible SGX core ID/revision and no bus hang. If the block cannot be
powered safely (bus hang, oops, reset), stop.

## What is NOT needed

- `gpu_es4.skprx` — NOT required. All register bases, reset/clock offsets, and
  SGX543 register map come from shoui520/psp2 clean-room reimplementation.
- Firmware — NOT needed for G2 (read-only probe only).
- Userspace UMD/GLSL — NOT needed for G2.

## Related files

- `docs/reverse-engineering.md` — G1/G2 sections
- `re/scripts/scan-mmio.py` — MMIO scanner (not needed here; addresses confirmed)
- shoui520/psp2 — `sys/internal/include/gpu/sgx_regs.h`, `sys/internal/include/lowio/pervasive_regs.h`, `sys/internal/include/lowio/pervasive.h`, `sys/internal/driver/src/gpu_es4/srvkm/sgxreset_sce.c`
