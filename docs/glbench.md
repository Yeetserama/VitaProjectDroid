# Software GLES2 performance gate (`glbench`)

Status: build automation ready, waiting for a first run on hardware.

This is the first v0.2 milestone. Before porting Mesa to bionic or writing a
SurfaceFlinger presenter, we need one number from the real console: **how fast
is a software rasterizer at 960x544 on the Vita's Cortex-A9?**

`glbench` is a self-contained armhf program that renders a few UI-like scenes
with Mesa's softpipe driver and, optionally, copies the result to
`/dev/graphics/fb0`. It does not depend on Android libraries and does not touch
the verified v0.1 boot kit.

## What is in the bundle

| Component | Purpose |
| --- | --- |
| Mesa 24.0.9, `gallium-drivers=swrast`, `llvm=disabled` | softpipe EGL/GLES2, no LLVM loaded |
| EGL surfaceless platform | headless rendering, no X11/Wayland/DRM |
| `glbench` | the benchmark itself |
| glibc loader + libs | the Vita's Android userspace is bionic; the bundle carries its own glibc |
| libdrm | Mesa's loader links it even when no DRM device exists |

Mesa is built without glvnd, zlib, expat, or a shader cache. The bundle is
about 10-20 MB uncompressed and lives entirely under `/data`.

## Scenes and the gate

All scenes render into a 960x544 RGBA8 FBO. The default is 120 frames for the
synthetic scenes and 60 for `ui`; one warm-up frame is rendered before each
timed run.

| Scene | What it does | What it tells us |
| --- | --- | --- |
| `clear` | `glClear` only | memory bandwidth / clear rate |
| `fill` | one full-screen quad, cheap gradient shader | fragment fill rate |
| `tex` | one full-screen textured quad, bilinear | texture sampling rate |
| `ui` | 160 alpha-blended, textured quads, one draw call each | launcher-like draw-call and blending cost |
| present | `glReadPixels` + row flip + RGBA row copy into fb0 | cost of the only presentation path available |

The verdict uses **effective fps**:

```
effective = 1 / (ui_frame_time + present_time)
```

`present` is measured five times after the scene runs. If `/dev/graphics/fb0`
cannot be opened, the benchmark still reports render numbers and skips the
verdict's present term.

The gate is **20 fps effective**. That is the minimum for a responsive-feeling
launcher at native resolution. 30 fps is the comfort target.

### Framebuffer byte order

The Vita DTB advertises `format = "a8b8g8r8"`. `simplefb` maps that string to
red at bit 0, green at bit 8, blue at bit 16, alpha at bit 24
(`DRM_FORMAT_ABGR8888`). On little-endian ARM the bytes in memory are therefore
**R, G, B, A**, and `glbench` copies the `GL_RGBA` readback straight into fb0
after flipping the rows (`--fb-format rgba`, the default).

If the on-screen test pattern shows red and blue exchanged, the panel path is
wired the other way: rerun with `--fb-format bgra`. The benchmark numbers are
unaffected; only the visible colors change.

## Build

The build runs in CI because this PC has no armhf cross toolchain. The
workflow is [`.github/workflows/glbench.yml`](../.github/workflows/glbench.yml)
and triggers on pushes to `feat/glbench` and manual dispatch.

It installs the Ubuntu armhf cross compiler, Meson/Ninja, and QEMU, then
`scripts/build-glbench.sh`:

1. downloads Mesa 24.0.9 and libdrm 2.4.120,
2. cross-builds libdrm (no udev, no GPU-specific helpers),
3. cross-builds Mesa with `-Dplatforms= -Dllvm=disabled -Dzlib=disabled`,
4. cross-compiles `glbench/glbench.c`,
5. copies the glibc runtime and every `DT_NEEDED` library into the bundle,
6. packages `build/glbench/glbench-vita-armhf.tar.gz` and `SHA256SUMS`.

The workflow then runs a 64x64 smoke test under `qemu-arm-static` and publishes
the tarball to the `glbench-latest` prerelease. Nothing is pushed to `main` and
the kernel workflow is not triggered.

Local build (on a Linux machine with the same packages):

```sh
bash scripts/build-glbench.sh
```

## Install on the Vita

Extract the artifact on the PC, then push the directory. Do not rely on `tar`
being present in the Android 5.1 image.

```sh
tar xzf glbench-vita-armhf.tar.gz
# Windows: ren bundle glbench
adb push glbench /data/
adb shell chmod 755 /data/glbench/run.sh /data/glbench/glbench \
    /data/glbench/lib/ld-linux-armhf.so.3
```

`/data` is mounted with `noatime,nosuid,nodev`, not `noexec`, so the bundle can
run from there. `run.sh` invokes the bundled loader explicitly, so the Android
linker is not involved.

## Run

```sh
adb shell
cd /data/glbench
sh run.sh
```

Quick check first (a few seconds):

```sh
sh run.sh --scene fill --frames 10 --no-present
sh run.sh --scene ui --ui-frames 20
```

Full run:

```sh
sh run.sh
```

The full run takes roughly 10-30 seconds depending on the result. Paste the
whole output; it includes the renderer string, per-scene fps, present cost, and
the verdict.

Useful options:

| Option | Meaning |
| --- | --- |
| `--width N`, `--height N` | change the render size (default 960x544) |
| `--frames N` | frames for clear/fill/tex |
| `--ui-frames N` | frames for the ui scene |
| `--scene NAME` | run only `clear`, `fill`, `tex`, or `ui` |
| `--fb-format F` | `rgba` (default, matches the DTB) or `bgra` |
| `--no-present` | skip `/dev/graphics/fb0` entirely |
| `--hold N` | keep the last presented frame on screen for N seconds |
| `--gate N` | verdict threshold (default 20 fps) |
| `--json` | machine-readable output |
| `--fb PATH` | alternate framebuffer device |

Expected shape of the output:

```
glbench: EGL 1.5 Mesa 24.0.9
glbench: GL_RENDERER softpipe
glbench: GL_VERSION  OpenGL ES 2.0 Mesa 24.0.9
glbench: fb0 960x544, 32 bpp, stride 3840, smem 2088960
glbench: present readback 3.014 ms, copy 4.502 ms

glbench: softpipe, 960x544
  clear   210.44 fps     4.752 ms/frame  (120 frames)
  fill     52.18 fps    19.164 ms/frame  (120 frames)
  tex      34.77 fps    28.758 ms/frame  (120 frames)
  ui       25.00 fps    40.000 ms/frame  (60 frames)
  present                 7.516 ms  (readback + RGBA copy)

gate: 20 fps -> PASS (effective 21.05 fps, render + present)
```

The numbers above are illustrative, not a prediction.

## Reading the result

- **`GL_RENDERER` must say `softpipe`.** If it says `llvmpipe`, this bundle
  was built with LLVM and the measurement is a different experiment.
- **`clear`** is the bandwidth ceiling. It should be far above the other
  scenes; if it is not, the bottleneck is not the rasterizer.
- **`fill` vs `tex`** shows the cost of texture sampling.
- **`ui`** is the scene that matters. It approximates a launcher with
  overlapping translucent cards.
- **`present`** is unavoidable with the current single-frame fbdev carveout.
  It is a CPU readback plus a row copy into fb0. A real Android presenter
  would do the same copy through gralloc or a custom HWC.
- **The gate** is intentionally conservative. Passing at 20 fps effective means
  a minimal UI is viable; failing means native-resolution software rendering is
  not, and the next experiment should change the renderer or the resolution,
  not the framework.

If the gate fails, the fallback order is:

1. **llvmpipe** (NEON, LLVM) instead of softpipe. It uses more memory and needs
   an LLVM runtime on the device, but it is typically faster.
2. **SwiftShader** (JIT) with the same EGL/GLES2 surface.
3. **Lower the internal resolution** (for example 640x368) and upscale in the
   presenter.
4. **Native fbdev shell** without the Android framework, which is a different
   product direction but avoids SurfaceFlinger entirely.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| `missing .../lib/ld-linux-armhf.so.3` | incomplete push; re-push the whole directory |
| `DRI2: failed to load driver` | `dri/swrast_dri.so` missing or `LIBGL_DRIVERS_PATH` wrong; run with `EGL_LOG_LEVEL=info` |
| `symbol lookup error` | mixed host/device libraries; always use `run.sh`, do not set `LD_LIBRARY_PATH` to system paths |
| `cannot open /dev/graphics/fb0` | fb0 absent or already owned; use `--no-present` |
| killed during the run | low memory; stop `zygote`/`surfaceflinger` or use a smaller size |
| very low fps | check the CPU frequency/governor and that no other service is busy |

Cleanup is just `rm -rf /data/glbench`. No boot files, kernel modules, or
system images are modified.

## What this proves, and what it does not

It proves the **rasterizer speed** of a software GLES2 implementation on the
real SoC, including the fb0 presentation copy. It does not prove:

- that Mesa can be built against bionic (the next porting step),
- that SurfaceFlinger can use it (needs EGL/gralloc/HWC glue),
- that Android apps render correctly,
- that the SGX GPU is usable (see
  [`gpu-research.md`](gpu-research.md) for why that is a separate research
  track).

Those are only worth doing if this gate passes.
