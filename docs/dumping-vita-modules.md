# Dumping the Vita audio and GPU modules

This is the capture step for the research plan in
[`reverse-engineering.md`](reverse-engineering.md). It produces decrypted
ELFs from the user's own console for Ghidra.

Read the legal rules first: dump only from a console you own, keep the
binaries out of git, and do not share them.

## What to capture

| File | VitaOS path | Module | Libraries |
| --- | --- | --- | --- |
| `audio.skprx` | `os0:kd/audio.skprx` | `SceAudio` | `SceAudio` (user), `SceAudioForDriver` (kernel) |
| `gpu_es4.skprx` | `os0:kd/gpu_es4.skprx` | `SceGpuEs4` | `SceGpuEs4ForUser`, `SceGpuEs4ForDriver` |
| `gpuinit_es4.skprx` | `os0:kd/gpuinit_es4.skprx` | GPU init helper | optional; capture if present |
| `libgpu_es4.suprx` | `vs0:sys/external/libgpu_es4.suprx` | user-side GPU library | optional |

`audio.skprx` is the important one for Phase 1. `gpu_es4.skprx` and
`gpuinit_es4.skprx` are for the read-only GPU probe. The optional files make
the GPU picture more complete but are not required for the first audio step.

Do **not** copy `os0:` or `vs0:` wholesale. Four files is enough.

## Tooling

Use **FAGDec**, a GPLv3 Vita homebrew that decrypts system modules with the
console's own SBL. It does not need Sony keys on the PC.

- Source and prebuilt VPK:
  <https://github.com/TeamFAPS/PSVita-RE-tools/tree/master/FAGDec>
- The VPK is also in this workspace at `re/tmp/FAGDec.vpk` (gitignored).
- SHA-256 of that copy:
  `18e7e474fd88fd93407df80e60dcd630a869028c64e285089fc3fd6b0cfc4b4d`

FAGDec is from the 3.60/3.65 era. It is expected to work on 3.65 for system
modules. If it fails, stop and capture the error; do not try random plugins.

## 1. Prepare the console

You need HENkaku/Enso, "Enable Unsafe Homebrew", and VitaShell (or
molecularShell). The console should be on the same network as the PC if you
want to use FTP.

Install FAGDec:

1. Copy `FAGDec.vpk` to `ux0:` (VitaShell FTP or USB).
2. In VitaShell, highlight the VPK and press Cross.
3. Confirm the install, then launch FAGDec from the LiveArea.

## 2. Copy the encrypted modules to a safe folder

FAGDec can scan `os0:` directly, but copying the four files first is safer and
gives a stable input path.

In VitaShell:

1. Create `ux0:os0_em/kd/`.
   - Open `ux0:`, press Triangle, choose **New folder**, name it `os0_em`.
   - Open `os0_em`, create `kd` the same way.
2. Browse to `os0:kd/`.
3. Copy these files into `ux0:os0_em/kd/`:
   - `audio.skprx`
   - `gpu_es4.skprx`
   - `gpuinit_es4.skprx` if it exists

   Highlight a file, press Triangle, choose **Copy**, navigate to
   `ux0:os0_em/kd/`, press Triangle, choose **Paste**.
4. For the optional user library, create `ux0:vs0_em/sys/external/` and copy
   `vs0:sys/external/libgpu_es4.suprx` into it.

If a file is missing, note it and continue with what exists. Do not write to
`os0:` or `vs0:`; they are read-only system partitions.

## 3. Decrypt with FAGDec

1. Launch FAGDec. The left pane lists `os0:`, `vs0:`, `ux0:/os0_em`,
   `ux0:/vs0_em`, and installed games.
2. Select `ux0:/os0_em` with Cross.
3. The right pane lists every SELF under that folder. Select
   `kd/audio.skprx` and `kd/gpu_es4.skprx` with Cross. Each selection is added
   to the "Modules to be decrypted" list.
4. Press **Start** to open the decrypt menu.
5. Choose **START DECRYPT(ELF)** with Cross and wait for `Module done`.
6. Repeat for `ux0:/vs0_em` if you copied `libgpu_es4.suprx`.

Output lands under `ux0:/FAGDec/`, preserving the input path. For example:

```
ux0:/FAGDec/os0_em/kd/audio.skprx.elf
ux0:/FAGDec/os0_em/kd/gpu_es4.skprx.elf
```

The `.elf` is the file for Ghidra. FAGDec may also leave `.auth` or `.sha256`
files; keep them with the dump but do not commit them.

## 4. Transfer to the PC

1. Exit FAGDec.
2. In VitaShell, press Select to start the FTP server (or use USB).
3. On the PC, copy the whole `ux0:/FAGDec/` tree into
   `re/dumps/fw365/` in this repository.
4. Rename the top-level folder to match the console, for example
   `re/dumps/fw365/os0_em/kd/audio.skprx.elf`.

`re/dumps/` is gitignored. Verify with:

```powershell
git status --short
```

Nothing under `re/dumps/` should appear.

## 5. Record the hashes

Run the helper from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/hash-vita-dumps.ps1
```

It prints a Markdown table with size, SHA-256 and whether each file starts
with an ELF or SELF magic. Paste the rows into
[`../re/notes/manifest.md`](../re/notes/manifest.md). Keep the binaries out of
git.

A quick sanity check:

- `audio.skprx.elf` should be a 32-bit ARM ELF and contain strings such as
  `SceAudio`, `SceAudioForDriver` and codec/clock references.
- `gpu_es4.skprx.elf` should contain `PVRSRV`, `DevInitSGX`, and SGX/MMU
  strings.
- `gpuinit_es4.skprx.elf` is worth reading first for clock, reset and MMU
  initialization constants.

## 6. If FAGDec fails

Capture the exact error code and the file it was processing. Common outcomes:

- **`80010002` / invalid ELF**: the module needs a newer SBL context. Note it
  and stop; a memory dump is the fallback.
- **FAGDec does not list the file**: confirm the SCE header is present and the
  file really is a SELF. Try FAGDec's direct `os0:` entry instead of
  `ux0:/os0_em`.
- **Kernel plugin fails to start**: reboot, make sure HENkaku/Enso is active,
  and try again without other kernel plugins loaded.

Do not install random "decryption" plugins or download keys. If FAGDec cannot
do it, the next step is a small taiHEN kernel plugin that reads the loaded
module with `taiGetModuleInfoForKernel()` + `ksceKernelGetModuleInfo()` and
writes its segments to `ux0:`. That avoids SELF decryption entirely and is the
more reliable long-term method, but it needs a VitaSDK build.

## 7. What happens next

With `audio.skprx.elf` in Ghidra:

1. Import the NIDs from `wiki.henkaku.xyz/vita/SceAudio`.
2. Find the MMIO base(s) and the register writes around
   `sceAudioOutOpenPort` / `sceAudioOutOutput`.
3. Identify the I2S format, FIFO thresholds, DMA descriptor and interrupt.
4. Write the findings to `re/notes/audio-registers.md`.

Do not commit the ELF, the Ghidra project, or any disassembly listing. Commit
only original notes and original driver code.
