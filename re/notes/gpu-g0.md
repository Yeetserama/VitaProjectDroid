# GPU G0 - Desk research notes

Status: in progress. No hardware writes yet — read-only desk research.

Target: PCH-1000, firmware 3.65, LinuxOnVita kernel 6.12 (`a33b819`).
GPU: PowerVR SGX543MP4+ (Series5XT), Kermit SoC.

## Confirmed hardware facts

From `incognitojam/vita-linux-port` HARDWARE.md:

| Item | Value | Source |
| --- | --- | --- |
| GPU | PowerVR SGX543MP4+ | HARDWARE.md |
| Framebuffer | `0x20000000`, 960x544x4 (CDRAM, 0x1FE000 bytes) | HARDWARE.md |
| DRAM start | `0x40200000` (510 MB) | HARDWARE.md |
| Pervasive block | `0xE3100000`–`0xE310FFFF` | HARDWARE.md |
| Pervasive Reset | `0xE3101000` | Working (reset controller) |
| Pervasive Gate | `0xE3102000` | Not started |
| Pervasive BaseClk | `0xE3103000` | Not started |
| Pervasive Vid | `0xE3104000` | Not started |

Pervasive reset/gate offsets (relative to `0xE3101000`/`0xE3102000`):

| Peripheral | Offset formula |
| --- | --- |
| SDIF(bus) | `0xA0 + bus*4` |
| I2C(bus) | `0x110 + bus*4` |
| SPI(bus) | `0x104 + bus*4` |
| UART(bus) | `0x120 + bus*4` |
| GPIO | `0x100` |
| MSIF | `0xB0` |
| DSI(bus) | `0x80 + bus*4` |
| UDC(bus) | `0x90 + bus*4` |

**No GPU entry exists in the pervasive offset table.** The GPU clock/reset/power
bits must be found via RE of `gpucoredump_es4.skprx` or `gpu_es4.skprx`.

## Not yet captured

| File | VitaOS path | Why needed |
| --- | --- | --- |
| `gpu_es4.skprx.elf` | `os0:kd/gpu_es4.skprx` | GPU driver — MMIO base, IRQ, clock/reset, firmware load |
| `gpucoredump_es4.skprx.elf` | `os0:kd/gpucoredump_es4.skprx` | **In dump** — GPU crash register dump, may reveal MMIO base |
| `gpuinit_es4.skprx.elf` | `os0:kd/gpuinit_es4.skprx` | GPU init helper — optional |

`gpucoredump_es4.skprx.elf` IS in `fwdump/kd/` (40,112 bytes, SHA-256
`1ff53a820d60257f162dff556d90208e79fcb10ad904864e7bd52d77e0522f41`) but
`scan-mmio.py` found no MMIO candidates in the default peripheral window
(`0xE0000000-0xE3FFFFFF`). The addresses may be computed at runtime.

## G0 desk research steps

### Step 1 — Identify the DDK/PVR Services version

Look at `libgpu_es4.suprx.elf` (10,672 bytes, in dump) and `libgxm_es4.suprx.elf`
(149,212 bytes, in dump) for version strings and structure layouts.

The Vita wiki documents `SceGpuEs4` version 1.69. Compare its exported symbols
(`sceGpuGetRegisterDump`, `sceGpuMmuMapMemory`, `SceGpuInfo`, and the
`PVRSRV*` bridge functions) against `openpvrsgx` drivers:

- https://github.com/openpvrsgx-devgroup/linux_openpvrsgx
- OMAP5, Allwinner, JZ4780 SGX5 drivers

### Step 2 — Collect SGX543 register maps

From `openpvrsgx` and Vita3K:

- https://github.com/Vita3K/Vita3K (SGX ISA/USSE disassembly notes)
- SGX543 register map from openpvrsgx vendor drivers

Goal: a likely MMU format, register layout, and memory map.

### Step 3 — Find GPU MMIO base via scan-mmio

Run `re/scripts/scan-mmio.py` on `gpucoredump_es4.skprx.elf` with different
ranges. The default `0xE0000000-0xE3FFFFFF` returned no hits. Try:

```sh
python re/scripts/scan-mmio.py fwdump/kd/gpucoredump_es4.skprx.elf \
    --range 0xE0000000-0xE3FFFFFF --include-text
python re/scripts/scan-mmio.py fwdump/kd/gpucoredump_es4.skprx.elf \
    --range 0xE4000000-0xEFFFFFFF
python re/scripts/scan-mmio.py fwdump/kd/display.skprx.elf \
    --range 0xE0000000-0xE3FFFFFF --include-text
```

### Step 4 — Cross-check with Vita3K

Vita3K has reverse-engineered SGX register access from emulating VitaOS games.
Its GPU source may reveal the MMIO base used by `SceGpuEs4`.

## Gate

A named DDK generation and a likely register/MMU model — with evidence, not
guesses. If no MMIO base can be found after scanning all available dumps and
cross-checking Vita3K/openpvrsgx, G0 stops and GPU remains software-rendered.

## What we do NOT do in G0

- No writes to GPU registers beyond clock/reset (G2 only, after base is known)
- No firmware download attempts
- No kernel driver port
- No userspace GLES/EGL work

## Related

- [`docs/gpu-research.md`](../../docs/gpu-research.md) — feasibility assessment
- [`docs/reverse-engineering.md`](../../docs/reverse-engineering.md) — overall plan
- `re/scripts/scan-mmio.py` — MMIO candidate scanner
- `fwdump/kd/gpucoredump_es4.skprx.elf` — in dump, ready to scan
