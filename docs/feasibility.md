# Feasibility

## 1. The hardware we are targeting

| Component | PlayStation Vita |
| --- | --- |
| SoC | Sony CXD5315GG (custom) |
| CPU | 4x ARM Cortex-A9 MPCore, 333 MHz default, 444 MHz max in the Linux port |
| GPU | PowerVR SGX543MP4+ (4 cores) |
| RAM | 512 MB LPDDR2 + 128 MB VRAM (CDRAM) |
| Display | 960x544, 5-inch OLED (PCH-1000) or LCD (PCH-2000) |
| Storage | Vita memory card, SD2Vita (microSD in the game-card slot), 1 GB eMMC on PCH-2000 |
| Wi-Fi | Marvell SD8787 via SDIO (mwifiex) |
| Bluetooth | 2.1 + EDR (same Marvell combo, not enabled in the Linux port) |
| Input | Touchscreen, rear touchpad, buttons, two analog sticks, six-axis sensors |
| Cameras | 0.3 MP front and rear |
| 3G | PCH-1100 only |
| USB | Device port, disabled in the current Linux kernel config |

The Linux port exposes 510 MB at `0x40200000` and a simple framebuffer at
`0x20000000` (960x544, 32 bpp, ARGB). The display controller is initialized by
the bare-metal loader; Linux does not have a DRM/KMS driver for it.

## 2. The software stack

### VitaOS (the stock system)

VitaOS is based on FreeBSD. Its drivers are Sony kernel modules (`.skprx`) and
userland libraries built for that kernel. They are signed, encrypted, and
completely unrelated to Linux or Android. This is the key fact behind the
"firmware drivers" question:

> Sony's Vita drivers **cannot** be loaded into Android. Android uses a Linux
> kernel; VitaOS uses a FreeBSD-derived kernel. Different kernel ABI, different
> driver model, no source, and no permission to redistribute.

### Modding layer

- **HENkaku / HENlo** provide userland and kernel code execution on supported
  firmware.
- **Enso** makes the exploit persistent at boot on 3.60/3.65.
- **taiHEN** loads kernel plugins.
- **LinuxOnVita's bare-metal loader** is a taiHEN plugin. It stages a Linux
  `zImage` and DTB in RAM and reboots the console into bare-metal Linux. The
  VitaOS installation is not erased; rebooting returns to VitaOS.

This is a "soft dual boot": Linux/Android runs with full hardware control until
the next reboot, then VitaOS comes back. There is no unlocked bootloader and no
need to modify the VitaOS partition.

### LinuxOnVita

LinuxOnVita is the current community Linux distribution for the Vita:

- Linux 6.12 kernel port (`incognitojam/linux_vita`, `vita-port-6.12.0`).
- Quad-core SMP, CPU frequency scaling (111-444 MHz), ZRAM.
- Display via `simple-framebuffer`; touchscreen and buttons via Syscon; SDHCI
  storage (eMMC, SD2Vita, SDIO Wi-Fi); mwifiex Wi-Fi; RTC; LEDs.
- Alpine Linux rootfs, not Android.

It is GPL-3.0 (the kernel is GPL-2.0). It is the correct base for Android.

## 3. Why the existing Android game ports are not a precedent

Projects such as `so-loader`, `yoyoloader_vita`, and the many "Android game on
Vita" loaders:

- run on **VitaOS**, not on Linux;
- load an Android game's native ARM `.so` with a custom ELF loader;
- implement a subset of bionic/JNI;
- use **vitaGL**, which translates OpenGL ES calls to Sony's SceGxm API and the
  Vita's native GPU driver.

That works because VitaOS still owns the GPU. A Linux/Android port does not:
the Vita's GPU driver is a FreeBSD module, and there is no Linux equivalent.
Ironically, the "whole Android" project will have **worse** graphics than the
existing game ports until someone writes an SGX543 Linux driver.

## 4. What Android needs, layer by layer

| Layer | What it needs | Status on Vita |
| --- | --- | --- |
| Boot | Load kernel + DTB + ramdisk, jump to Linux | Available (LinuxOnVita loader) |
| Kernel core | ARMv7, SMP, MMU, timers, GIC, VFP/NEON | Works |
| IPC | binder, ashmem | binder is in Linux 6.12; ashmem needs a backport |
| Storage | ext4, loop, ur0 exFAT | Works (kernel modules built in) |
| Display | framebuffer, HWC, software GLES | Framebuffer works; HWC and GLES need work |
| Input | evdev touch + buttons, keylayout | Drivers work; Android mapping needs work |
| Wi-Fi | cfg80211/mwifiex, wpa_supplicant, netd | Driver works; Android glue needs work |
| Audio | ALSA codec + audio HAL | Missing |
| GPU | SGX543 Linux driver + EGL/GLES | Missing; no public driver |
| Camera | V4L2 sensor drivers + camera HAL | Missing |
| Sensors | IIO input drivers + sensor HAL | Missing |
| Bluetooth | hciattach + BlueZ/Bluedroid | Missing |
| Battery | power_supply driver + healthd | Missing |
| Suspend | PM/suspend support | Missing |
| USB/ADB | USB gadget or Wi-Fi ADB | USB disabled; Wi-Fi ADB is the path |

## 5. The three real blockers

### 5.1 Graphics

Android's SurfaceFlinger and HardwareRenderer require OpenGL ES 2.0. The Vita
has a capable GPU, but no Linux driver. Options:

1. **Port a software GLES 2.0 implementation.** SwiftShader 2.x had GLES 2.0;
   modern SwiftShader is Vulkan-only and would need ANGLE on top. Mesa's
   softpipe/llvmpipe is another route. All are significant ports to Android
   5.1/ARMv7.
2. **Write a 2D-only path.** A custom HWC that composites layers on the CPU,
   plus framework/apps that use software canvases instead of HWUI. This can
   produce a minimal UI but breaks most apps.
3. **Reverse-engineer the SGX543.** This is a multi-year driver project with no
   public documentation. LinuxOnVita lists SGX543 acceleration as long-term
   research, not a near-term feature.

Until one of these works, "Android boots to a black screen or a very slow
software-rendered UI" is the expected outcome. The prebuilt Android 5.1.1 SDK
image used for bring-up has no `hwcomposer.*` module and only a software GLES
1.1 renderer, so neither the display presentation path nor GLES 2.0 is a drop-in.

### 5.2 Memory

512 MB is the hard limit. Android 5.1 can be configured for low-RAM devices, but
a modern Android runtime plus SurfaceFlinger plus a software renderer is tight.
Mitigations:

- `ro.config.low_ram=true`
- ZRAM swap (LinuxOnVita already supports it)
- no Google apps, no Play Store
- a minimal launcher and a small system image
- ART `interpret-only` or `quicken` filters to reduce compile time and memory
- kill background apps aggressively

Even with all of that, expect a 2013-class experience, not a 2026 one.

### 5.3 Storage and risk

Android needs a system image (hundreds of MB) and a writable data image. This
project's bring-up target is **ur0-only, with no SD2Vita**: the official Sony
card holds the boot files, and the Android images live on the internal ~2.5 GB
`ur0:` partition. The Sony card cannot hold the images because Linux has no MSIF
driver, and the loader cannot read ur0 before Linux starts.

ur0 is shared with VitaOS and formatted as exFAT, which is not journaled.
Bare-metal Linux bypasses VitaOS's storage safeguards, and a crash or power loss
during a write can corrupt the partition. A full console backup exists; keep it
current, keep 300-500 MB free, and use clean shutdowns. SD2Vita remains a
fallback if ur0 proves too small, but it is not part of the current design.

## 6. What the result will feel like

If the project succeeds at the "usable" milestone:

- Android 5.1 boots to a launcher on the Vita screen.
- Touch, buttons, and analog sticks work.
- Wi-Fi works.
- Simple 2D apps and old APKs run, slowly.
- No 3D games, no YouTube at a useful frame rate, no audio, no camera.
- Battery status may be unknown; suspend may not work.

That is still a genuinely interesting device: a 2011 handheld running a
period-correct Android with a great screen and real controls. It is not a modern
Android tablet.

## 7. Effort estimate

For an experienced embedded Linux/Android developer with the hardware on the
desk:

- Boot Android init on the Vita: weeks to a few months.
- Display + software GLES + launcher: months, with high uncertainty.
- Wi-Fi/input/power polish: weeks after the core works.
- GPU/audio: research projects, not schedule items.

For a hobbyist working evenings: multiply by 3-5, and treat the graphics spike
as a go/no-go gate.

## 8. Go/no-go gates

1. **Kernel gate:** Android init starts and binder/ashmem work on the Vita.
2. **Display gate:** a minimal HWC/fbdev presenter can copy a frame to
   `/dev/graphics/fb0` (the SDK image ships no HWC).
3. **GLES gate:** a trivial GLES 2.0 app renders at a usable frame rate.
4. **Memory gate:** launcher + settings survive 30 minutes without OOM.

If gate 3 cannot be passed, pivot to the 2D-only path or to the existing
so-loader approach instead of sinking time into a full Android port.
