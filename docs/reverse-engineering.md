# Reverse-engineering the Vita GPU and audio controller

Status: research plan of record. Separate from the v0.2 UI work.
Target: PCH-1000, firmware 3.65, LinuxOnVita kernel 6.12.

This document is the plan for turning the Vita's own audio and GPU hardware
into Linux drivers. It is deliberately staged: audio first, GPU as a
read-only probe until two hard gates are passed.

## 1. What "reverse-engineer the Vita OS" means here

VitaOS is FreeBSD-based. Its `SceAudio` and `SceGpuEs4` modules are Sony
binaries built for that kernel; they cannot be loaded into Linux. What we can
take from them is **knowledge**:

- MMIO base addresses, register layouts and initialization sequences
- clock, reset and power-sequencing bits
- DMA descriptor formats and interrupt behaviour
- firmware blob layout and load protocol
- the PVRSRV bridge ABI used by `SceGpuEs4`
- codec quirks that differ from the generic driver

We then write **original Linux drivers** that implement the same hardware
behaviour. No Sony code, firmware or keys go into this repository.

Two evidence sources are used together:

1. **Static**: dump the modules from the user's own console and disassemble
   them in Ghidra. Good for finding constants, sequences and structure layouts.
2. **Dynamic**: instrument the running VitaOS with a taiHEN plugin and use the
   console as an oracle (register dumps, call traces, before/after state).
   Often faster than static analysis for "which register changes when audio
   starts?" questions.

## 2. Priority and expectations

| Track | Difficulty | First usable result | Main blocker |
| --- | --- | --- | --- |
| Audio playback | Medium | Speaker/headphone tone in Linux, then Android | SoC I2S/DMA controller is undocumented |
| GPU 3D | Very high | A read-only probe in dmesg | SGX firmware and a GLSL/shader compiler |

**Audio is the first track.** The codec is identified and mainline-supported,
the audio clock is already solved, and the remaining unknown is a bounded
register/DMA target. Success is plausible in weeks, not months.

**GPU is a research track.** It must not block v0.2. The first milestone is
only "power the block, read a sane SGX ID, do not hang the bus". If the
firmware gate fails, the track stops there.

## 3. What is already known

### Audio

| Fact | Value | Confidence |
| --- | --- | --- |
| Codec | WM1803E, WM8962-compatible | Verified on hardware |
| Codec I2C address | `0x1A` (7-bit), chip ID `0x6243` | Verified on hardware |
| Codec clock | AudioClk from P1P40167 clockgen | Verified on hardware |
| Clockgen | I2C bus 0, address `0x69`, register 1 | From LinuxOnVita `vita-audio` |
| AudioClk enable | register 1, bit 4 | From LinuxOnVita `vita-audio` |
| Rate select | register 1, bit 0: 0 = 44.1 kHz, 1 = 48 kHz | From LinuxOnVita `vita-audio` |
| MCLK | 22.5792 MHz (44.1k family) or 24.576 MHz (48k family) | From LinuxOnVita `vita-audio` |
| I2C0 base | `0xE0500000` (size 0x1000) | From vita-linux-port HARDWARE.md |
| I2C1 base | `0xE0510000` (size 0x1000) | From vita-linux-port HARDWARE.md |
| **I2S0 base** | `0xE0420000` (size 0x1000) | From shoui520/psp2 `audio_regs.h` |
| **I2S1 base** | `0xE0430000` (size 0x1000) | From shoui520/psp2 `audio_regs.h` |
| **I2S2 base** | `0xE0440000` (size 0x1000) | From shoui520/psp2 `audio_regs.h` |
| **I2S3 base** | `0xE0450000` (size 0x1000) | From shoui520/psp2 `audio_regs.h` |
| **I2S4 base** | `0xE0470000` (size 0x1000) | From shoui520/psp2 `audio_regs.h` |
| **I2S5 base** | `0xE0460000` (size 0x1000) | From shoui520/psp2 `audio_regs.h` |
| **I2S7 base** | `0xE0490000` (size 0x1000) | From shoui520/psp2 `audio_regs.h` |
| **SRCMIX0 base** | `0xE04A0000` (stride 0x10000) | From shoui520/psp2 `audio_regs.h` |
| **SPDIF base** | `0xE04D0000` (size 0x4000) | From shoui520/psp2 `audio_regs.h` |
| **ACLKGEN base** | `0xE04DC000` (size 0x1000) | From shoui520/psp2 `audio_regs.h` |
| DMAC4 base | `0xE0400000` (16 channels) | From shoui520/psp2 `dmac_regs.h` |
| DMAC5 base | `0xE0410000` (2 channels) | From shoui520/psp2 `dmac_regs.h` |
| DMAC0-3, DMAC6 | `0xE3000000` / `0xE3010000` / `0xE5000000` / `0xE5010000` / `0xE50C0000` | From shoui520/psp2 `dmac_regs.h` |
| Pin mux | Possibly K5/K7/G5/B4, clocks B5/B6 | Unverified |

**I2S register layout** (from shoui520/psp2 `audio_regs.h` / `i2s.c`):
- `control` at offset `0x04`, `formatControl` at `0x10`, `interruptStatus` at `0x14`
- `clockControl` at `0x18`, `transferMode` at `0x20`, `transferEnable` at `0x24`
- `data` (DMA port) at offset `0x100`
- Init: `formatControl = 0x2810`, `interruptControl = 0`, `control = 0x100`, then
  `clockControl = 1`, `transferEnable = 1`, `transferMode = 2`, `control = 1`
- I2S interrupts: port 0 = IRQ 193, port 1 = IRQ 194, port 2 = IRQ 196, ports 3-7 = 0
- I2S clock indices: [0, 1, 2, 3, 5, 4, 255, 7] for ports [0-7]
- DMA channel mapping: I2S port 0 → DMA state 2 transfer 1, port 1 → DMA state 2 transfer 0

A1 (codec bring-up) is implemented on `re/audio` as two tracked kernel patches
(syscon AudioClk provider + WM8962 DTS node). A2 (SoC I2S/DMA) now has confirmed
register bases from shoui520/psp2 — the block is no longer on `audio.skprx`.

### GPU

| Fact | Value | Confidence |
| --- | --- | --- |
| GPU | PowerVR SGX543MP4+ (Series5XT) | High |
| SoC | Kermit, Toshiba stacked SoC | High |
| VitaOS module | `SceGpuEs4`, kernel, version 1.69 | Wiki |
| Driver family | PVR Services (`PVRSRV*` exports) | High |
| Exported probes | `sceGpuGetRegisterDump`, `sceGpuMmuMapMemory`, `SceGpuInfo` | Wiki |
| MMU heaps | General `0x7000000 -> 0x40000000`, TA `0x7000001 -> 0x50000000` | Wiki, unverified |
| **GPU MMIO base** | `0xE8400000` (size 0x20000) | From shoui520/psp2 `sgx_regs.h` |
| **GPU IRQ** | `0x34` (52) | From shoui520/psp2 `register-map.c` |
| **GPU per-core bank** | `(core + 2) << 14` (0x4000 per core) | From shoui520/psp2 `sgx_regs.h` |
| **GPU counter regs** | `0x70` value, `0x78` enable, `0x90` select1, `0x94` select2 | From shoui520/psp2 `sgx_regs.h` |
| GPU clock/reset | Pervasive `gpuClock`@`0xE3103010`, reset@`0xE3101010`, gate@`0xE3102010`, bus 0 | From shoui520/psp2 `pervasive_regs.h`/`pervasive.h` |
| Firmware | Inside VitaOS, encrypted/signed | High |
| Userspace UMD / GLSL compiler | Sony `libgxm`/SceGpu, not reusable | High |
| Framebuffer | `0x20000000`, 960x544x4 (CDRAM) | From vita-linux-port HARDWARE.md |
| DRAM start | `0x40200000` | From vita-linux-port HARDWARE.md |

**GPU register map** (from shoui520/psp2 `sgx_regs.h` / `register-map.c`):
- Master bank at `0xE8400000`, size `0x20000`
- Core `n` bank at `0xE8400000 + (n + 2) * 0x4000`
- SGX soft reset via `EUR_CR_MASTER_SOFT_RESET` (BIF + IPF + DPM + MCI + VDM + PTLA + SLC + all 4 cores)
- Clock gating via `EUR_CR_CLKGATECTL` / `EUR_CR_CLKGATECTL2`
- Reset sequence: assert soft reset → wait clocks → configure BIF/SLC → deassert reset → init clocks → setup BIF contexts

`SceGpuEs4` exports the same `PVRSRVEnumerateDevicesKM`,
`PVRSRVAllocDeviceMemKM`, `PVRSRVGetMiscInfoKM` and bridge-dispatch surface as
the open-source SGX5 kernel drivers collected by `openpvrsgx`. That makes a
port structurally plausible; it does not solve firmware or userspace.

### Confirmed peripheral map (from vita-linux-port HARDWARE.md)

| Peripheral | Address | Size | Status |
| --- | --- | --- | --- |
| SCU | `0x1A000000` | — | Working (SMP) |
| GIC Distributor | `0x1A001000` | — | Working |
| PL310 L2 Cache | `0x1A002000` | — | Working |
| ARM Global Timer | `0x1A000200` | — | Working (144 MHz clocksource) |
| SPI0 (Syscon) | `0xE0A00000` | 0x1000 | Working |
| DMAC0 | `0xE3000000` | — | Not started |
| DMAC1 | `0xE3010000` | — | Not started |
| DMAC2 | `0xE5000000` | — | Not started |
| DMAC3 | `0xE5010000` | — | Not started |
| DMAC4 | `0xE0400000` | — | Not started |
| DMAC5 | `0xE0410000` | — | Not started |
| MSIF (Memory Card) | `0xE0900000` | — | Not started — requires auth |
| SDIF0 (eMMC) | `0xE0B00000` | 0x1000 | Working |
| SDIF1 (GameCard) | `0xE0C00000` | 0x1000 | Driver works, init fails |
| SDIF2 (WLAN/BT) | `0xE0C10000` | 0x1000 | Working (mwifiex + btmrvl) |
| SDIF3 (microSD) | `0xE0C20000` | 0x1000 | No card |
| UART0 | `0xE2030000` | 0x10000 | Working |
| GPIO | `0xE20A0000` | 0x1000 | Working |
| Pervasive Reset | `0xE3101000` | 0x1000 | Working |
| Pervasive Gate | `0xE3102000` | 0x1000 | — |
| Pervasive BaseClk | `0xE3103000` | 0x1000 | — |
| Pervasive Vid | `0xE3104000` | 0x1000 | — |

Pervasive reset/gate offsets (relative to `0xE3101000`/`0xE3102000`):
SDIF(bus) `0xA0+bus*4`, I2C(bus) `0x110+bus*4`, SPI(bus) `0x104+bus*4`,
UART(bus) `0x120+bus*4`, GPIO `0x100`, MSIF `0xB0`.

I2C0 devices: `0x1A` WM1803E codec, `0x4A` unknown, `0x69` P1P40167 clockgen.

### MSIF (Memory Card) — not started

| Register | Offset | Purpose |
| --- | --- | --- |
| Command | `0x30` | |
| Data | `0x34` | |
| Status | `0x38` | |
| System | `0x3C` | bit 15 = reset |

Base: `0xE0900000` (from vita-linux-port HARDWARE.md).
MSIF requires authentication (3DES-CBC-CTS + ECDSA-224) before data access.
The baremetal linux-loader already handles this using a hardcoded key.
A Linux driver would need to re-authenticate or preserve state from the loader.

Static scan of `msif.skprx` (in fwdump/kd/) returned no MMIO candidates —
the base address `0xE0900000` is not referenced as a literal in the module.

## 4. Workspace and rules

```
re/
  README.md        tracked: rules and layout
  notes/           tracked: original findings, register maps, manifests
  scripts/         tracked: parsers and helpers
  dumps/           gitignored: user's own decrypted modules
  ghidra/          gitignored: Ghidra project databases
  tmp/             gitignored: scratch output
```

Rules:

- Dump only from the user's own console. Never download someone else's
  firmware, keys or decrypted modules.
- `re/dumps/` and `re/ghidra/` never enter git. Record SHA-256, firmware
  version and module path in `re/notes/manifest.md` instead.
- Notes may contain addresses, register values and short pseudocode. Do not
  paste large disassembly listings or Sony code into the repository.
- Hardware experiments run on `re/*` branches. They are never merged into the
  boot kit unless they pass their gate.
- A probe that writes unknown registers can hang the memory bus. GPU work stays
  read-only until clock/reset is proven.

## 5. Phase 0 - capture and tooling

Blocked on the console being available; desk research can continue offline.

1. Capture the four files listed in
   [`dumping-vita-modules.md`](dumping-vita-modules.md): the kernel modules
   live under `os0:kd/`, the optional user GPU library under
   `vs0:sys/external/`.
2. Decrypt them with FAGDec on the console. If a module cannot be decrypted,
   dump the loaded module from memory with a small taiHEN helper instead.
3. Record firmware version, module version, size and SHA-256 in
   `re/notes/manifest.md`. Keep the binaries in `re/dumps/`.
4. Create a Ghidra project, import the ELFs, and apply the NID/name list from
   `wiki.henkaku.xyz/vita/SceAudio` and `/SceGpuEs4`.
5. Add a `re/scripts/` helper that verifies hashes and prints the manifest, so
   a dump can be checked without shipping it.

Gate: modules imported, functions named, manifest complete. No hardware risk.

## 6. Phase 1 - audio

### A1. Codec bring-up (no RE required)

Implemented on `re/audio` as two tracked kernel patches:

- `kernel/patches/0001-...`: `vita-syscon.c` gains an AudioClk set helper and
  registers a `vita-audio-clk` provider. The clock's prepare/unprepare ops
  drive clockgen register 1 (bit 4 enable, bit 0 rate family).
- `kernel/patches/0002-...`: the codec node is
  `wlf,wm8962` at `0x1A` on i2c0, with `clocks = <&syscon>`; the syscon node
  gets `#clock-cells`. The property `vita,audio-clk-always-on` keeps MCLK on.
- `kernel/config/vita-android.fragment` enables `CONFIG_SOUND`, `CONFIG_SND`,
  `CONFIG_SND_SOC` and `CONFIG_SND_SOC_WM8962`.

The clock provider is what makes probe ordering deterministic: the codec
defers until the provider exists, and the provider is only registered after
the clockgen I2C adapter is available.

Gate: `wm8962` binds, reads ID `0x6243`, and register access is stable.
Bring-up notes: [`re/notes/audio-a1.md`](../re/notes/audio-a1.md).

### A2. Find the SoC I2S/DMA controller

**Register bases confirmed** from shoui520/psp2 `audio_regs.h` / `dmac_regs.h`:

| Block | Base | Size | Channels |
| --- | --- | --- | --- |
| I2S0 | `0xE0420000` | 0x1000 | — |
| I2S1 | `0xE0430000` | 0x1000 | — |
| I2S2 | `0xE0440000` | 0x1000 | — |
| I2S3 | `0xE0450000` | 0x1000 | — |
| I2S4 | `0xE0470000` | 0x1000 | — |
| I2S5 | `0xE0460000` | 0x1000 | — |
| I2S7 | `0xE0490000` | 0x1000 | — |
| SRCMIX0-2 | `0xE04A0000` + n*0x10000 | 0x1000 each | — |
| SPDIF | `0xE04D0000` | 0x4000 | — |
| ACLKGEN | `0xE04DC000` | 0x1000 | — |
| DMAC4 | `0xE0400000` | 0x1000 | 16 |
| DMAC5 | `0xE0410000` | 0x1000 | 2 |

I2S register layout: `control`@0x04, `formatControl`@0x10, `interruptStatus`@0x14,
`clockControl`@0x18, `transferMode`@0x20, `transferEnable`@0x24, `data`(DMA)@0x100.
Init: `formatControl = 0x2810`, `interruptControl = 0`, `control = 0x100`, then
`clockControl = 1`, `transferEnable = 1`, `transferMode = 2`, `control = 1`.

I2S interrupts: port 0 = IRQ 193, port 1 = IRQ 194, port 2 = IRQ 196, ports 3-7 = 0.
I2S clock indices: [0,1,2,3,5,4,255,7] for ports [0-7].

**Next step:** probe one I2S port read-only from Linux (map `0xE0420000` size 0x1000,
read registers, compare against shoui520/psp2 init values). No writes beyond clock
enable via pervasive block.

**DMAC register layout** (from shoui520/psp2 `dmac_regs.h`):
- Channel regs (0x80 bytes each): src@0x00, dst@0x04, len@0x08, cmd@0x0c,
  ctrl@0x1c, stat@0x24, intrStatus@0x28
- DMAC4: 16 channels, interrupt gates at 0x900 (IRQs 0x78-0x7b)
- DMAC0-3,5,6: 2 channels each

Deliverable: `re/notes/audio-registers.md` with base, register map, DMA
descriptor layout, IRQ and init sequence.

Gate: a register map that explains playback start, periodic DMA and stop.

### A3. Machine driver

- ASoC CPU DAI + DMA driver for the SoC side, playback only.
- WM8962 as codec; 48 kHz, stereo, 16-bit first.
- Headphone and speaker routes; jack detection only if the hardware exposes it.

Gate: `aplay` of a sine wave is audible on the real console, with no
under-runs over several minutes.

### A4. Android audio HAL

- tinyalsa-based HAL, one output stream, speaker/headphone routing.
- Audio policy stub until the framework needs more.

Gate: Android boot sound or `tinyplay` works with the stock framework.

### Stop conditions

- No I2S/DMA block can be identified after static and dynamic tracing.
- The DMA engine cannot be made reliable without undocumented firmware.
- Pin mux cannot be set from Linux without breaking another peripheral.

If any of these hold, keep the audio HAL stubbed and move on.

## 7. Phase 2 - GPU

### G0. Desk comparison

**Complete (2026-11-13).** Identified the DDK generation and register/MMU model
from shoui520/psp2 clean-room reimplementation:

| Item | Value | Source |
| --- | --- | --- |
| DDK | IMG SGX543, PVR Services bridge | `shoui520/psp2` `sgx_regs.h` |
| MMIO base | `0xE8400000`, size `0x20000` | `sgx_regs.h` |
| IRQ | `0x34` (52) | `register-map.c` |
| Per-core bank | `(core + 2) << 14` | `sgx_regs.h` |
| Counter regs | `0x70` value, `0x78` enable | `sgx_regs.h` |
| Reset regs | `EUR_CR_MASTER_SOFT_RESET`, `EUR_CR_CLKGATECTL` | `sgxreset_sce.c` |

Gate passed: named DDK generation (IMG SGX543 v1.164) and likely register/MMU
model.

### G1. Extract the hardware configuration

**Complete (2026-11-13).** From shoui520/psp2 clean-room reimplementation:

| Item | Value | Evidence |
| --- | --- | --- |
| CPU base | `0xE8400000` | `sgx_regs.h` SGX_REG_BASE |
| IRQ | `0x34` (52) | `register-map.c` |
| Reset | `EUR_CR_MASTER_SOFT_RESET` at base+0x0000 | `sgxreset_sce.c` |
| Clock | Pervasive `gpuClock` at `0xE3103010` (core << 16 \| mp) | `pervasive_regs.h` |
| Reset gate | Pervasive `gpu` at `0xE3101010` (reset) / `0xE3102010` (gate) | `pervasive_regs.h` |
| GPU bus | Pervasive bus 0 (`ScePervasiveForDriver_84EAB5BC` on, `_E8656ADF` off) | `pervasive.h` |
| GPU isolation | Misc `gpuIsolation` at `0xE3100034` | `pervasive_regs.h` |
| MMU heaps | General `0x7000000->0x40000000`, TA `0x7000001->0x50000000` | Wiki, unverified |
| Firmware header | Unknown — need `gpu_es4.skprx` (not on this console) | |

Gate passed: base/IRQ/clock/reset all have evidence.

### G2. Read-only probe

**Ready to implement.** Plan:

1. Map `0xE3101010` (Pervasive Reset GPU) and `0xE3102010` (Pervasive Gate GPU)
2. Deassert GPU reset: clear the GPU bit in the reset page
3. Enable GPU clock: set the GPU bit in the gate page
4. Map `0xE8400000` size `0x20000` (SGX543 register block)
5. Read ID/revision/status registers, print to dmesg
6. Submit no jobs; write no GPU registers beyond reset/clock

Per-core banks: core `n` at `0xE8400000 + (n + 2) * 0x4000` (4 cores, banks at
`0xE8480000`, `0xE84C0000`, `0xE8500000`, `0xE8540000`).

Counter regs (per bank): `0x70` value, `0x78` enable, `0x90` select1, `0x94` select2.

Gate: a plausible SGX core ID/revision and no bus hang. If the block cannot
be powered safely, stop.

### G3. Firmware gate

- Locate the SGX firmware image and determine its container, encryption and
  load protocol.
- Decide whether an open `pvrsrvkm` can load it. A Sony-specific signed
  container that only `SceGpuEs4` can authenticate is a hard stop.

Gate: firmware can be loaded legally and technically by our driver.

### G4. Minimal submission (only if G3 passes)

- Port the `openpvrsgx` kernel driver with Vita glue: MMU, IRQ, power, queue.
- NOP job, then clear, then a triangle.
- A userspace UMD and GLSL compiler remain a separate project. Without them
  there is no Android GLES, even with a working kernel driver.

This phase is measured in months and may end at G2 or G3. Do not gate v0.2 on
it.

## 8. Immediate sequence

1. **Audio A2** — probe I2S0 read-only from `0xE0420000` via a kernel module
   or devmem; verify registers match shoui520/psp2 init values
   ([`re/notes/audio-a2.md`](../re/notes/audio-a2.md)).
2. **GPU G2** — implement read-only SGX probe: deassert GPU reset at
   `0xE3101010`, enable clock at `0xE3102010`, map `0xE8400000`,
   read ID/revision counters
   ([`re/notes/gpu-g2.md`](../re/notes/gpu-g2.md)).
3. **MSIF** — Memory Card at `0xE0900000`; requires 3DES-CBC-CTS +
   ECDSA-224 auth before data access (baremetal loader has the key)
   ([`docs/reverse-engineering.md`](#msif-memory-card---not-started)).
4. When the console is on: capture the dump and run the pending stock-framework
   UI test (`scripts/try-android-ui.ps1`).
5. Write findings to `re/notes/` as they are confirmed; keep this plan and
   `docs/gpu-research.md` in sync.

## 9. Sources

- Vita wiki, `SceAudio`: https://wiki.henkaku.xyz/vita/SceAudio
- Vita wiki, `SceGpuEs4`: https://wiki.henkaku.xyz/vita/SceGpuEs4
- Vita wiki, `GPU`: https://wiki.henkaku.xyz/vita/GPU
- Vita wiki, `Kermit`: https://wiki.henkaku.xyz/vita/Kermit
- LinuxOnVita `vita-audio` branch: https://github.com/incognitojam/linux_vita/tree/vita-audio
- openpvrsgx SGX5 kernel drivers: https://github.com/openpvrsgx-devgroup/linux_openpvrsgx
- Vita3K SGX ISA notes: https://github.com/Vita3K/Vita3K
- shoui520/psp2 (clean-room SceAudio/SceGpuEs4 reimplementation): https://github.com/shoui520/psp2
- shoui520/psp2-cex-gpues4-prof (GPU profiling): https://github.com/shoui520/psp2-cex-gpues4-prof
- WM8962 codec driver: `sound/soc/codecs/wm8962.c` in mainline Linux
- P1P40167 clockgen programming: `drivers/mfd/vita-syscon.c` in LinuxOnVita
