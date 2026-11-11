# Firmware dump manifest

Record one row per captured file. Do not commit the module or ELF itself.

Firmware: 3.65 (HENkaku/Enso), PCH-1000.
Capture method: FAGDec on console, output ELF.
Steps: [`docs/dumping-vita-modules.md`](../../docs/dumping-vita-modules.md).

| File | VitaOS path | Module | Bytes | SHA-256 | Magic | Notes |
| --- | --- | --- | ---: | --- | --- | --- |
| `libgpu_es4.suprx.elf` | `vs0:sys/external/libgpu_es4.suprx` | TODO | 10672 | 18588a4ff539434ac2f35d4a5c4fedc67f6c3eab3b9ba2073efebf2a149a2bb1 | ELF | |
| `audio.skprx.elf` | `os0:kd/audio.skprx` | `SceAudio` | | | | libraries `SceAudio`, `SceAudioForDriver` |
| `gpu_es4.skprx.elf` | `os0:kd/gpu_es4.skprx` | `SceGpuEs4` | | | | libraries `SceGpuEs4ForUser`, `SceGpuEs4ForDriver` |
| `gpuinit_es4.skprx.elf` | `os0:kd/gpuinit_es4.skprx` | GPU init helper | | | | optional; capture if present |
| `libgpu_es4.suprx.elf` | `vs0:sys/external/libgpu_es4.suprx` | user GPU library | | | | optional |

Status: Updated 2026-11-11T22:23:14.636647Z

## Raw SELF hashes (optional but useful)

If the encrypted files are kept, record them here too. They are not needed for
Ghidra but confirm that the same input produced the ELF.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `audio.skprx` | | |
| `gpu_es4.skprx` | | |
| `gpuinit_es4.skprx` | | |
| `libgpu_es4.suprx` | | |
