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
| SoC I2S/DMA | Unknown; no driver exists | Blocking unknown |
| Pin mux | Possibly K5/K7/G5/B4, clocks B5/B6 | Unverified |

The upstream `vita-audio` branch only enables `CONFIG_SND` and adds the
clockgen helper. It contains **no** codec node, I2S driver, DMA driver or
machine driver. The codec has been probed successfully from Linux, which
proves the I2C path and clock work.

### GPU

| Fact | Value | Confidence |
| --- | --- | --- |
| GPU | PowerVR SGX543MP4+ (Series5XT) | High |
| SoC | Kermit, Toshiba stacked SoC | High |
| VitaOS module | `SceGpuEs4`, kernel, version 1.69 | Wiki |
| Driver family | PVR Services (`PVRSRV*` exports) | High |
| Exported probes | `sceGpuGetRegisterDump`, `sceGpuMmuMapMemory`, `SceGpuInfo` | Wiki |
| MMU heaps | General `0x7000000 -> 0x40000000`, TA `0x7000001 -> 0x50000000` | Wiki, unverified |
| CPU base / IRQ / clock / reset | Unknown | Blocking unknown |
| Firmware | Inside VitaOS, encrypted/signed | High |
| Userspace UMD / GLSL compiler | Sony `libgxm`/SceGpu, not reusable | High |

`SceGpuEs4` exports the same `PVRSRVEnumerateDevicesKM`,
`PVRSRVAllocDeviceMemKM`, `PVRSRVGetMiscInfoKM` and bridge-dispatch surface as
the open-source SGX5 kernel drivers collected by `openpvrsgx`. That makes a
port structurally plausible; it does not solve firmware or userspace.

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

- Add a `wm8962` I2C node at `0x1A` on the bus used by the codec.
- Enable `CONFIG_SND_SOC_WM8962` and the simple-card/ASoC core.
- Feed MCLK from the P1P40167 helper (24.576 MHz for 48 kHz).
- Probe and read the chip ID.

Gate: `wm8962` binds, reads ID `0x6243`, and register access is stable.

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
  `SceGpuEs4`.
- Compare its bridge ABI with the SGX5 drivers in `openpvrsgx` (OMAP5,
  Allwinner, JZ4780).
- Collect SGX543 register maps and MMU formats from those drivers and from
  Vita3K's SGX ISA work.

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
