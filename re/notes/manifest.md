# Firmware dump manifest

Record one row per captured file. Do not commit the module or ELF itself.

Firmware: 3.65 (HENkaku/Enso), PCH-1000.
Capture method: FAGDec on console, output ELF.
Steps: [`docs/dumping-vita-modules.md`](../../docs/dumping-vita-modules.md).

| File | VitaOS path | Module | Bytes | SHA-256 | Magic | Notes |
| --- | --- | --- | ---: | --- | --- | --- |
| `libgpu_es4.suprx.elf` | `vs0:sys/external/libgpu_es4.suprx` | TODO | 10672 | 18588a4ff539434ac2f35d4a5c4fedc67f6c3eab3b9ba2073efebf2a149a2bb1 | ELF | |
| `audio.skprx.elf` | `os0:kd/audio.skprx` | `SceAudio` | 55488 | c5e3e2c0fc468da994838ce9f72c5d9f8f942af4e725f4619acc56821f2b8b22 | ELF | libraries `SceAudio`, `SceAudioForDriver`; captured from boot image decryption |
| `gpu_es4.skprx.elf` | `os0:kd/gpu_es4.skprx` | `SceGpuEs4` | 71924 | d11047f57598463c14df1e508bd92ff23551b6ac781f94e92d06f19259830732 | ELF | libraries `SceGpuEs4ForUser`, `SceGpuEs4ForDriver`; captured from boot image decryption |
| `gpuinit_es4.skprx.elf` | `os0:kd/gpuinit_es4.skprx` | GPU init helper | | | | optional; capture if present |
| `libgpu_es4.suprx.elf` | `vs0:sys/external/libgpu_es4.suprx` | user GPU library | | | | optional |

Status: Updated 2026-11-11T22:23:14.636647Z

## Raw SELF hashes (optional but useful)

If the encrypted files are kept, record them here too. They are not needed for
Ghidra but confirm that the same input produced the ELF.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `audio.skprx` | 55488 | c5e3e2c0fc468da994838ce9f72c5d9f8f942af4e725f4619acc56821f2b8b22 |
| `gpu_es4.skprx` | 71924 | d11047f57598463c14df1e508bd92ff23551b6ac781f94e92d06f19259830732 |
| `gpuinit_es4.skprx` | | |
| `libgpu_es4.suprx` | | |
