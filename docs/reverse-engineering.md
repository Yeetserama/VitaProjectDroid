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
| DMAC4 base | `0xE0400000` | Not started — candidate for audio DMA |
| DMAC5 base | `0xE0410000` | Not started — candidate for audio DMA |
| SoC I2S/DMA | Unknown; no driver exists | Blocking unknown — DMAC4/5 are candidates |
| Pin mux | Possibly K5/K7/G5/B4, clocks B5/B6 | Unverified |

A1 (codec bring-up) is implemented on `re/audio` as two tracked kernel patches
(syscon AudioClk provider + WM8962 DTS node). A2 (SoC I2S/DMA) needs
`audio.skprx`, which is not in this console's dump — schedule a VitaShell/taiHEN
re-dump of `os0:kd/audio.skprx`.

### GPU

| Fact | Value | Confidence |
| --- | --- | --- |
| GPU | PowerVR SGX543MP4+ (Series5XT) | High |
| SoC | Kermit, Toshiba stacked SoC | High |
| VitaOS module | `SceGpuEs4`, kernel, version 1.69 | Wiki |
| Driver family | PVR Services (`PVRSRV*` exports) | High |
| Exported probes | `sceGpuGetRegisterDump`, `sceGpuMmuMapMemory`, `SceGpuInfo` | Wiki |
| MMU heaps | General `0x7000000 -> 0x40000000`, TA `0x7000001 -> 0x50000000` | Wiki, unverified |
| GPU MMIO base | Unknown | Blocking unknown — not in HARDWARE.md table |
| GPU IRQ | Unknown | Blocking unknown |
| GPU clock/reset | Unknown | Pervasive offsets documented but no GPU entry |
| Firmware | Inside VitaOS, encrypted/signed | High |
| Userspace UMD / GLSL compiler | Sony `libgxm`/SceGpu, not reusable | High |
| Framebuffer | `0x20000000`, 960x544x4 (CDRAM) | From vita-linux-port HARDWARE.md |
| DRAM start | `0x40200000` | From vita-linux-port HARDWARE.md |

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

This is the real RE step. Work from both ends:

- Static: find MMIO accesses in `SceAudio`/`SceAudioForDriver`; identify the
  register block, FIFO thresholds, DMA descriptor format, interrupt mask and
  the start/stop sequence.
- Dynamic: instrument the running VitaOS, play a known tone, and diff
  register/clock state before and after. Cross-check the Kermit pin notes and
  the clockgen registers.
- Linux: once a candidate base is known, map it read-only and compare reset
  values against the VitaOS dump before writing anything.

**Known DMA candidates from vita-linux-port HARDWARE.md:**
- DMAC4 at `0xE0400000`, DMAC5 at `0xE0410000` — not started, possible audio DMA
- DMAC0-3 at `0xE3000000`/`0xE3010000`/`0xE5000000`/`0xE5010000` — not started

The I2S controller address is not yet documented anywhere public.

Static first pass: [`re/scripts/scan-mmio.py`](../re/scripts/scan-mmio.py)
looks for 4-byte values in the Vita peripheral window
(`0xE0000000-0xE3FFFFFF`) and resolves ARM-mode `ldr rX, [pc, #imm]`
literal-pool loads. It scans non-executable sections by default and groups
hits by page:

```sh
python re/scripts/scan-mmio.py re/dumps/fw365/audio.skprx.elf
python re/scripts/scan-mmio.py --include-text audio.skprx.elf
python re/scripts/scan-mmio.py --self-test
```

A hit is only a candidate; trace it to a function in Ghidra before treating
it as a register base. `--include-text` adds raw code words and is noisy
because ARM opcodes share the `0xE...` encoding space. The scanner also
accepts `--range` for other windows and `--json` for scripted comparison
between modules.

**Note:** The console's dump does not include `audio.skprx`. The dumps we do
have (`dmacmgr`, `display`, `sdif`, `syscon`, etc.) were scanned with
`scan-mmio.py` and returned no MMIO candidates in the default range — the
addresses may be computed at runtime or stored outside the scanned sections.
Schedule a VitaShell/taiHEN re-dump of `os0:kd/audio.skprx` for A2 static analysis.

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

- Identify the DDK/PVR Services version from strings and structure layouts in
  `SceGpuEs4` (not yet captured — needs VitaShell/taiHEN re-dump of
  `os0:kd/gpu_es4.skprx`).
- Compare its bridge ABI with the SGX5 drivers in `openpvrsgx` (OMAP5,
  Allwinner, JZ4780).
- Collect SGX543 register maps and MMU formats from those drivers and from
  Vita3K's SGX ISA work.

**Hardware context:** The GPU's MMIO base, IRQ, clock and reset are not in the
confirmed peripheral map. The framebuffer sits at `0x20000000` in CDRAM. The
Pervasive block at `0xE310xxxx` controls clocks/resets for all peripherals, but
no GPU entry exists in the offset table — those bits need to be found in
`gpucoredump_es4.skprx` or `gpu_es4.skprx` via RE.

Gate: a named DDK generation and a likely register/MMU model.

### G1. Extract the hardware configuration

From `SceGpuEs4`: CPU base, IRQ, clock and reset bits, power sequence, MMU
heap list, firmware header and load addresses. The wiki's `SceGpuInfo` gives
the clock-frequency fields; the register-dump export is a useful oracle.

Gate: a candidate base/IRQ/clock/reset set with evidence, not guesses.

### G2. Read-only probe

- Add a built-in `vita-gpu-probe` (modules are off in this kernel).
- Enable the clock and deassert reset using the recovered sequence.
- `ioremap` the block, read ID/revision/status, print to dmesg.
- Submit no jobs and write no GPU registers beyond clock/reset.

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

1. Confirm where the firmware dump is, or schedule a VitaShell/taiHEN dump.
2. When the console is on: capture the dump and run the pending stock-framework
   UI test (`scripts/try-android-ui.ps1`).
3. Start A1 (codec node) on an `re/audio` branch; no hardware writes beyond
   the already-proven codec/clock path.
4. Continue G0 desk research offline; it needs no console.
5. Write findings to `re/notes/` as they are confirmed; keep the plan and
   `docs/gpu-research.md` in sync.

## 9. Sources

- Vita wiki, `SceAudio`: https://wiki.henkaku.xyz/vita/SceAudio
- Vita wiki, `SceGpuEs4`: https://wiki.henkaku.xyz/vita/SceGpuEs4
- Vita wiki, `GPU`: https://wiki.henkaku.xyz/vita/GPU
- Vita wiki, `Kermit`: https://wiki.henkaku.xyz/vita/Kermit
- LinuxOnVita `vita-audio` branch: https://github.com/incognitojam/linux_vita/tree/vita-audio
- openpvrsgx SGX5 kernel drivers: https://github.com/openpvrsgx-devgroup/linux_openpvrsgx
- Vita3K SGX ISA notes: https://github.com/Vita3K/Vita3K
- WM8962 codec driver: `sound/soc/codecs/wm8962.c` in mainline Linux
- P1P40167 clockgen programming: `drivers/mfd/vita-syscon.c` in LinuxOnVita
