# GPU / GLES2 research notes

Status: feasibility assessment, not a plan of record.
Target: PCH-1000, LinuxOnVita kernel 6.12 (`a33b819`), Android 5.1.1 API 22.

The staged reverse-engineering plan and its go/no-go gates are in
[`reverse-engineering.md`](reverse-engineering.md).

## Verdict

There is no GLES2 renderer that can be dropped into this image. A GLES2
`.so` is the API dispatch layer, not a renderer. The actual pixels come from a
vendor userspace driver plus a kernel driver plus GPU firmware plus a shader
compiler. For the Vita's PowerVR SGX543MP4+ none of those exist for Linux.

Consequently there are only two real options:

1. **Software GLES2** (SwiftShader or Mesa softpipe/llvmpipe) with a custom
   EGL/gralloc path to `/dev/fb0`. Tractable; performance is the risk.
2. **Reverse-engineer / port an SGX5 driver**. Open-ended research; no public
   documentation, no vendor kernel drop for the Vita, firmware and shader
   compiler proprietary. Not a v0.2 path.

## Why "just take a GLES2 .so" does not work

Android's GLES2 stack is layered:

```
app
  -> libGLESv2.so            API dispatch (shim, ~38 KB here)
  -> libGLESv2_<vendor>.so   vendor UMD: GLES2 frontend + GLSL compiler
  -> libEGL / gralloc        window/buffer plumbing
  -> kernel driver           pvrsrvkm or DRM: memory, command queue, power
  -> GPU firmware            SGX microcode (TA/3D), loaded by the driver
  -> SGX543MP4+ hardware
```

A `.so` copied from another device only covers layers 2-3. It is built for a
specific GPU revision, firmware version, kernel UAPI and libc. Imagination
itself says the SGX driver "has to be built for a specific GPU, the version of
the firmware, userspace driver(s) & kernel driver have to exactly match ...
we don't have 'universal' userspace code ourselves." (dri-devel, 2023)

The Vita makes this worse: it is a custom SoC. There is no vendor kernel source
drop and no known-good `pvrsrvkm` snapshot for it. The community driver project
(openpvrsgx) explicitly collects **kernel** drivers only and does not provide
firmware or userspace libraries.

## Evidence collected for this port

### SDK image (system.img)

| file | size | meaning |
| --- | --- | --- |
| `lib/libGLESv2.so` | 37,920 | dispatch shim only |
| `lib/libGLESv1_CM.so` | 21,536 | GLES1 dispatch |
| `lib/egl/libGLES_android.so` | 79,196 | software GLES1 renderer (no matching `libEGL_android.so` in the image) |
| `lib/egl/libGLESv2_emulation.so` | 38,124 | QEMU/goldfish pipe, not Vita hardware |
| `lib/egl/libEGL_emulation.so` | 58,720 | QEMU/goldfish EGL |
| `lib/hw/gralloc.default.so` | 9,816 | fbdev gralloc |
| `lib/hw/hwcomposer.*` | absent | no HWC |

### Kernel and device tree (6c60068)

- `CONFIG_DRM` is **not set**. The built kernel has fbcon/simplefb only; there
  is no DRM subsystem, GEM, dma-buf GPU path, or GPU driver.
- `vita.dtb` has 37 nodes and **no** GPU/SGX/PowerVR node. Display is a single
  `simple-framebuffer` at `0x20000000`, size `960*544*4` = exactly one frame
  (`0x1FE000`). There is no room in that carveout for double buffering.
- `/soc/reset@e3101000` is a generic bit-per-ID controller; the DTS uses IDs
  64, 65, 68, 69 only (GPIO, SPI, I2C0/1). No GPU reset/clock/IRQ/MMU entry.
- `drivers/reset/reset-vita.c` is a bare readl/writel bit set/clear; it has no
  knowledge of what the bits mean.

### Upstream and community status

- **Imagination's open-source driver** (`drivers/gpu/drm/imagination`, Mesa
  `powervr`) targets Rogue (Series6) and newer, Vulkan-only. The Mesa docs
  state that GPUs prior to Series6 cannot be supported; Imagination said in
  2023 it has no plans to support SGX.
- **openpvrsgx/linux_openpvrsgx**: shapes existing GPL SGX5 kernel drivers for
  mainline; "NOT about reverse engineering or user-space components (firmware
  and libs)". Glue exists for OMAP, Sunxi, JZ4780. In issue #17 the Vita is
  described as a fully custom SoC with no released kernel source; xerpi
  offered reverse-engineering help in 2025, but no driver exists.
- **LinuxOnVita's own compatibility table** lists 3D GPU acceleration as
  "Software Render -- PowerVR SGX543MP4+ lacks open-source Linux kernel
  drivers."
- **Vita3K** has SGX ISA/USSE shader disassembly and translation, which is
  useful documentation for a driver, but it is an emulator, not a Linux
  driver, and it does not provide firmware or a kernel interface.

## What a real SGX5 driver would require

1. **Kernel driver**: DRM/GEM, command ring, GPU MMU programming, IRQ, power
   domains, clocks, resets, firmware loader. The Rogue mainline driver is
   ~32k lines; an SGX5 DDK port is comparable.
2. **Firmware**: SGX microcode for the TA/3D pipelines. The Vita's copy lives
   inside VitaOS (`SceGpuEs4`) and is encrypted/signed; it is matched to Sony's
   driver interface, not to `pvrsrvkm`.
3. **Userspace UMD**: GLES2 entry points and a GLSL -> SGX ISA compiler. On
   VitaOS this is `libgxm`/SceGpu, unavailable and incompatible with Linux.
4. **Android glue**: EGL, gralloc, HWC, and a presenter. The single-frame
   framebuffer means either tearing or a back-buffer memcpy at vblank.
5. **Hardware knowledge**: base address, IRQ, clock, reset bit, GPU MMU format,
   power sequencing. None of it is in the DTB; all of it needs RE.

### The only plausible hardware shortcut

Port `pvrsrvkm` from openpvrsgx for an SGX543/544 vendor DDK (OMAP5 or
Allwinner-class), write Vita glue, and match a firmware/UMD pair. This is
still a multi-month project with:
- unknown SGX543MP4+ core revision and firmware compatibility,
- Vita-specific MMU, power, clock and reset integration,
- binary-blob licensing/redistribution problems,
- no guarantee the VitaOS firmware is loadable or that an Android DDK's UMD
  ABI matches the ported kernel driver.

It is a research track, not an implementation task.

## Go/no-go probe (if the research track is pursued)

The purpose is to fail fast, before any large effort.

**Step 0 - desk research**
- Collect GPU base/IRQ/reset/clock/MMU facts from the Vita wiki, psdevwiki,
  Vita3K, SceGpuEs4 reverse engineering and openpvrsgx.
- Identify whether any Linux driver already owns the GPU power/reset bits.

**Step 1 - read-only probe**
- Add a built-in `vita-gpu-probe` (CONFIG_MODULES is off) that:
  - asserts/deasserts the candidate GPU reset and enables its clock,
  - ioremaps the candidate register block,
  - reads ID/status registers and prints them to dmesg,
  - performs no writes to the GPU beyond reset/clock.
- Gate: a sane SGX ID and no bus hang. If the block cannot be powered or
  clocked, stop. Reading an unclocked peripheral can hang the bus, so this is
  a real risk, not a formality.

**Step 2 - firmware gate**
- Determine whether a matching SGX firmware image can be obtained and loaded
  legally and technically. If not, stop: no firmware means no shaders.

**Step 3 - minimal submission**
- Only after Steps 1-2: a NOP/clear job, then a triangle, then a UMD port.

Success odds are low and the timeline is months. Do not block v0.2 on it.

## Recommendation

- **v0.2 UI path**: software GLES2. The first step is the performance gate in
  [`glbench.md`](glbench.md): a self-contained armhf Mesa softpipe benchmark at
  960x544 on the real console. If it passes, port Mesa to bionic and wire
  EGL/gralloc/`fb_post`; if it fails, try llvmpipe/SwiftShader or a lower
  internal resolution before committing to the framework.
- **Fallback**: native fbdev shell + touch (no framework). LinuxOnVita already
  proves fb0, touch and input work; this gives a usable UI without Android
  apps.
- **Research track**: SGX probe on a separate branch; never merged into the
  boot kit unless it actually reaches a triangle.

## Sources

- Mesa PowerVR driver docs: https://docs.mesa3d.org/drivers/powervr.html
- Imagination open-source driver: https://developer.imaginationtech.com/solutions/open-source-gpu-driver/
- dri-devel SGX discussion: https://lists.freedesktop.org/archives/dri-devel/2023-June/411000.html
- openpvrsgx: https://github.com/openpvrsgx-devgroup/linux_openpvrsgx
- openpvrsgx issue #17: https://github.com/openpvrsgx-devgroup/linux_openpvrsgx/issues/17
- LinuxOnVita hardware support: https://linuxonvita.com/
- Vita GPU wiki: https://wiki.henkaku.xyz/vita/GPU
- Vita graphics wiki: https://www.psdevwiki.com/vita/Graphics
