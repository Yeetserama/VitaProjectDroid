# Decisions and remaining unknowns

The bring-up decisions are now fixed. The unresolved items are mostly graphics,
on-device validation, and long-term distribution choices.

## Current decisions (2026-11-08/09)

- **Android target:** Android 5.1.1 (API 22), 32-bit ARMv7. Bring-up uses the
  prebuilt SDK image `system-images;android-22;default;armeabi-v7a` (LMY48X),
  not an AOSP build. Android 4.4.4 is the fallback if 5.1 is unusably slow.
- **Hardware:** PCH-1000 (OLED). Firmware **3.65 with HENkaku + Enso**;
  "Enable Unsafe Homebrew" is on; a full console backup has been taken.
- **Linux base:** LinuxOnVita 6.12 (`incognitojam/linux_vita`), booted from
  `ux0:/linux/` on the official 16 GB Sony card. No SD2Vita adapter.
- **Storage:** boot files on `ux0:/linux/`; Android images on `ur0:/android/`
  (`/dev/mmcblk0p12`, ~2.5 GB). `system.img` is 750 MB read-only,
  `userdata.img` is 550 MB writable, `/cache` is a 64 MB tmpfs. Keep 300-500 MB
  free on ur0. See [ur0-storage-plan.md](ur0-storage-plan.md).
- **Stage-1 init:** static busybox `sh` as `/init.vita`, kernel
  `rdinit=/init.vita`; it mounts ur0, sets up loop0/loop1 and zram, then
  `exec /init`. Android's `/init` is untouched.
- **Initramfs:** stock Android 5.1.1 `ramdisk.img` plus the project overlay and
  the glibc Wi-Fi userspace. Embedded directly as a raw newc cpio with
  `CONFIG_INITRAMFS_SOURCE="vita-initramfs.cpio"`.
- **SELinux:** permissive during bring-up (`androidboot.selinux=permissive`);
  a second DTB uses `androidboot.selinux=disabled` as a fallback. Policy version
  26 is within Linux 6.12's supported range (15-33).
- **First boot mode:** headless. `zygote`, `surfaceflinger`, `media`, `drm`, and
  `ril-daemon` stay stopped. `adbd` runs on TCP port 5555. The framework is
  enabled explicitly with `setprop vita.ui 1` or the `ur0:/android/ui` marker.
- **Wi-Fi:** reuse LinuxOnVita 1.5.0's glibc `wpa_supplicant` and its
  dependency closure under `/vita-linux`, launched through
  `ld-linux-armhf.so.3`. Android's `dhcpcd` obtains the lease. Config lives at
  `ur0:/android/wpa_supplicant.conf`.
- **Input:** keylayout/IDC files live in the initramfs under `/usr/` and are
  symlinked into `/data/system/devices/` by `init.vita.rc`.
- **Build:** GitHub Actions on Ubuntu 24.04 runs `bash scripts/build.sh`; the
  kernel is fetched at a pinned commit. No AOSP build host is required for the
  first boot.
- **Distribution:** commit build scripts, the prebuilt Android ramdisk (hash
  pinned), static busybox, and redistributable Wi-Fi firmware. Do not ship Sony
  firmware/keys or Android user images that the user must obtain from Google.

## Remaining open questions

### 1. Software GLES 2.0 path

SurfaceFlinger and HWUI need OpenGL ES 2.0. The SDK image has only
`gralloc.default` and a software GLES 1.1 renderer; there is no GLES 2.0
software driver.

Candidates:

1. Mesa softpipe/llvmpipe with an Android EGL/GLES wrapper.
2. SwiftShader 2.x (the last version with a GLES 2.0 frontend) ported to
   Android 5.1/ARMv7.
3. A 2D-only path: custom HWC + software-canvas framework/apps, no GLES apps.

Decision: **pending.** This is the make-or-break gate. Do not invest in UI
polish before a standalone GLES 2.0 triangle renders at a usable frame rate.

### 2. Display HAL / presenter

The prebuilt image has no `hwcomposer.*`. `gralloc.default` can open
`/dev/fb0`, but `simple-framebuffer` is a single buffer with no page flip or
vsync.

Options:

- a minimal `hwcomposer.vita` that marks all layers client-composed and copies
  the client target into `/dev/graphics/fb0`;
- an fbdev presenter inside a custom SurfaceFlinger build;
- a DRM/KMS driver for the Vita display controller (long-term, separate
  project).

Decision: **pending.** Start with the minimal HWC/fbdev copy after headless
Android is stable.

### 3. SELinux policy load

The policy header is compatible, but the kernel has not loaded it on hardware.
The first boot may fail at policy load or at a domain transition.

Decision: test with the permissive DTB first; if it fails, try the disabled
DTB, then inspect `dmesg` and `logcat`. Do not switch to enforcing until the
system boots reliably.

### 4. netd and `xt_qtaguid`

Android 5.1's netd expects the out-of-tree `xt_qtaguid` match. Mainline 6.12
does not have it.

Options:

- accept netd errors and use `ip`/`dhcpcd` directly for bring-up;
- backport a minimal `xt_qtaguid` stub;
- patch netd to skip the qtaguid setup.

Decision: **accept errors for the first boot.** Revisit only if netd blocks
adbd or basic networking.

### 5. Wi-Fi on hardware

The glibc supplicant path is designed but untested. Possible failure points:
firmware load, `mlan0` naming, control-socket path, netlink permissions, DHCP
hooks.

Decision: **test after adbd is reachable.** Keep `wpa_supplicant.log` and
`vita-net.log` on `/data` and `ur0:`.

### 6. Low-RAM tuning and read-only `/system`

The SDK `system.img` is read-only and has generic build properties. Low-RAM
properties (`ro.config.low_ram`, heap sizes, dex2oat filters) normally live in
`/system/build.prop`.

Options:

- put `persist.*`/`ro.*` overrides in the initramfs `default.prop` where
  possible;
- remount `/system` read-write and edit `build.prop` for experiments;
- build a small AOSP image later with the right product config.

Decision: **defer until the first boot.** Measure memory and dex2oat behavior
before changing the image.

### 7. `/data` size and ART

`userdata.img` is 550 MB. The first ART boot writes dex2oat output and app data.
If it fills, Android may fail or loop.

Options: grow `userdata.img` (ur0 has ~1.2 GB free), reduce the system image
first, or use a smaller dex2oat filter. Decision: **measure after first boot.**

### 8. UART access

UART0 is the most reliable early console but requires soldering to test points.

Decision: **not required for the first boot.** Use `console=tty0` + fbcon, and
add UART if the kernel hangs before the framebuffer console initializes.

### 9. App target and success criteria

- Period-correct APKs (2012-2016) only?
- A specific set of apps/games?
- Is "boots headless, adb works, Wi-Fi works" enough for v0.1?
- Or is a usable launcher required for v0.1?

Decision: **v0.1 = headless Android + adb + Wi-Fi + persistent `/data`.** The
launcher/UI is v0.2 and depends on the GLES/HWC work.

### 10. Project name and license

- Working name: AndroidOnVita.
- Kernel patches must be GPL-2.0-compatible. LinuxOnVita is GPL-3.0.
- Original userspace pieces can be Apache-2.0 or GPL-3.0.

Decision: **pending.** Keep the kernel patches GPL-2.0 and document the license
of each component before publishing binaries.

### 11. Long-term image source

The SDK image is the fastest route to a boot. A custom AOSP build would allow:

- a real `device/sony/vita` product with low-RAM defaults;
- a smaller system image;
- a built-in HWC/GLES path;
- control over SELinux policy and properties.

Decision: **only after the first boot proves the kernel/userspace handoff.**
Supporting two image sources at once would slow down the port.
