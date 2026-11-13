# AndroidOnVita

Run a full Android system on a modded PlayStation Vita.

**Status:** v0.1 complete. Headless Android 5.1.1 boots on a real PCH-1000
with the LinuxOnVita 6.12 kernel, joins 2.4 GHz Wi-Fi, gets a DHCP lease, and
is reachable over `adb connect <ip>:5555`. This was verified on hardware over
both a hotspot and a mobile-data network. Two fixes got there: the kernel
selects the 32-bit binder ABI (protocol 7) that Android 5.1's userspace
requires, and v6 restores the `lookup main` policy rule that Android 5.1's
netd flushes (its replacement rule is rejected with `EINVAL` on mainline
kernels). The software GLES2 gate also passed on hardware: the `glbench`
bundle rendered a launcher-like scene at about **30 fps effective** at
960x544 with Mesa softpipe, including the fb0 present copy. Next: v0.2, the
launcher/UI integration.
Target:
**Android 5.1.1 (API 22) on a PCH-1000 (OLED)**, using the LinuxOnVita 6.12
kernel, with **no SD2Vita adapter**. Firmware is **3.65 with HENkaku + Enso**;
"Enable Unsafe Homebrew" is on and a full console backup has been taken. This is
a research project, not a consumer ROM.

## Version status

Each Android version is maintained on its own branch. "Boots" means on real
PCH-1000 hardware, not only in a build log.

| Branch | Version | State |
| --- | --- | --- |
| `main` | 5.1.1 (API 22) | UI and Wi-Fi work, but app launches take an unusually long time; not pleasant to use yet. |
| `android-4.2.2` | 4.2.2 (API 17) | UI starts automatically, navigation bar and Wi-Fi work; current verification branch. |
| `android-gingerbread` | 2.3.7 (API 10) | Headless boot and adb work. Enabling the UI makes `zygote` exit and init then kills/restarts `media`; crash capture is in place, cause still open. |
| planned | Honeycomb / 6 / 7 | Not started; only worth a branch if a generic ARMv7 SDK image boots with the existing kernel work. |

The Gingerbread failure is the same symptom class as the earlier 5.1.1 zygote
crash, not a separate media bug: Gingerbread's stock `init.rc` restarts `media`
whenever `zygote` exits, so both names appear in the log. The 5.1.1 slow app
start is a separate performance issue. Details:
[docs/android-2.3.md](docs/android-2.3.md),
[docs/android-4.2.md](docs/android-4.2.md).

Not affiliated with Sony or Google. You need your own console, your own legally
obtained firmware, and you must be comfortable with the risk of data loss.

---

## Verdict up front

A full Android OS on the Vita is technically possible, but not in the way most
people imagine:

- The Vita is a 2011 ARMv7 device: 4x Cortex-A9 at 444 MHz, 512 MB RAM plus
  128 MB VRAM, a PowerVR SGX543MP4+ GPU, and a 960x544 display.
- The existing "Android games on Vita" ports are not Android. They load individual
  Android native libraries (`.so`) on top of VitaOS and use vitaGL/SceGxm for
  graphics. They prove that Android *binaries* can run, not that Android *the OS*
  can run.
- A whole Android system needs a Linux kernel. The community already has one:
  [LinuxOnVita](https://github.com/devwithzachary/LinuxOnVita) boots Linux 6.12
  bare-metal on 3.60/3.65 Enso consoles and provides display, touch, buttons,
  Wi-Fi, storage, RTC, and quad-core SMP.
- There is **no public Linux/Android driver for the SGX543MP4+**. There is also
  no audio, camera, motion-sensor, Bluetooth, battery, or suspend driver in the
  current Linux port. Android will therefore run with software rendering, no
  sound, and a limited feature set until those are reverse-engineered.

**Realistic target:** Android 5.1.1 (Lollipop, API 22), 32-bit ARMv7, low-RAM
configuration, no Google apps. Android 4.4.4 (KitKat) is the fallback if 5.1 is
too heavy. Android 8+ is not realistic on this hardware.

The hard part is not "getting Android." It is **graphics and memory**: a 512 MB
device with no GPU driver needs a software GLES path before a normal Android UI
can appear.

---

## Design at a glance

| Piece | Decision |
| --- | --- |
| Kernel | LinuxOnVita / `incognitojam/linux_vita` 6.12, plus ashmem and an Android config fragment |
| Android userspace | Prebuilt SDK image `system-images;android-22;default;armeabi-v7a` (LMY48X). No AOSP build for bring-up |
| Boot files | `ux0:/linux/zImage` + `vita.dtb`, read by the VitaOS-side loader from the official Sony card |
| Android images | `ur0:/android/system.img` (750 MB, ext4, read-only) and `userdata.img` (550 MB, ext4, writable) |
| `/cache` | 64 MB tmpfs from `fstab.vita`; no cache image on ur0 |
| Stage-1 init | Static busybox `sh` as `/init.vita` (`rdinit=/init.vita`), then `exec /init` |
| Headless first | zygote, SurfaceFlinger, media, drm, and RIL stay stopped; `adbd` runs on TCP port 5555 |
| UI | Optional: `adb shell setprop vita.ui 1`, or create `ur0:/android/ui` before boot |
| SELinux | Permissive for bring-up; a second DTB with `androidboot.selinux=disabled` is also built |
| Wi-Fi | LinuxOnVita 1.5.0 glibc `wpa_supplicant` + dependency closure under `/vita-linux` in the initramfs |
| Input | Keylayout/IDC files in the initramfs, symlinked into `/data/system/devices` |
| Build | GitHub Actions on Ubuntu 24.04 (`bash scripts/build.sh`); no 150 GB AOSP host needed |

### Boot chain

```
VitaOS + Enso
  -> baremetal-loader.skprx (taiHEN plugin)
     -> payload.bin
        -> loads ux0:/linux/zImage + ux0:/linux/vita.dtb
           -> Linux 6.12
              -> /init.vita (busybox stage 1)
                 -> mounts devtmpfs/proc/sys and ur0 read-write
                 -> losetup /dev/loop0 system.img, /dev/loop1 userdata.img
                 -> enables 128 MiB zram swap
                 -> exec /init (Android 5.1.1 init)
                    -> imports /init.vita.rc
                    -> mount_all /fstab.vita
                    -> headless services + adbd over TCP
```

The VPK's **CROSS** button boots the files already in `ux0:/linux/`. Do not use
the installer's reinstall/TRIANGLE path unless you want to overwrite
`zImage`/`vita.dtb` with LinuxOnVita's desktop kernel.

### Why ur0 and not the Sony card

The loader can read the official Sony memory card from VitaOS, but Linux cannot:
the card uses Sony's proprietary MSIF protocol and there is no Linux driver.
Once the SoC reboots into bare metal, VitaOS and its MSIF driver are gone. Boot
files therefore stay on the Sony card (`ux0:`), and the Android images live on
the internal `ur0:` partition (`/dev/mmcblk0p12`), which Linux can mount as
exFAT. See [docs/ur0-storage-plan.md](docs/ur0-storage-plan.md).

---

## What already exists

| Piece | What it gives us |
| --- | --- |
| [LinuxOnVita](https://github.com/devwithzachary/LinuxOnVita) | Linux 6.12, bare-metal loader, display/touch/buttons/Wi-Fi/storage, Alpine rootfs, build system |
| [incognitojam/linux_vita](https://github.com/incognitojam/linux_vita) | The 6.12 Vita kernel port (DTS, SDHCI, I2C, SPI, GPIO, Syscon input, mwifiex, cpufreq) |
| [xerpi/vita-baremetal-linux-loader](https://github.com/xerpi/vita-baremetal-linux-loader) | Payload that loads `zImage` + DTB and jumps to Linux |
| [xerpi/vita-baremetal-loader](https://github.com/xerpi/vita-baremetal-loader) | taiHEN kernel plugin that stages the payload and reboots into bare metal |
| [Android SDK system images](https://dl.google.com/android/repository/sys-img/android/) | Prebuilt Android 5.1.1 generic ARMv7 `system.img` + `userdata.img` |
| [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git) | Marvell SD8787 Wi-Fi firmware and regulatory database |

## What still needs work

1. **First headless boot:** reach `adbd` over TCP. The first attempt reached
   Android `late_start` and Wi-Fi but reset when `servicemanager` rejected the
   kernel's binder protocol; the next build selects the 32-bit ABI and adds a
   synced `ur0:/android/boot.log`.
2. **Software GLES 2.0:** the SDK image contains only `gralloc.default` and a
   software GLES 1.1 renderer; there is no GLES 2.0 software driver and no
   `hwcomposer.*` HAL. SurfaceFlinger needs GLES 2.0 before the UI can start.
3. **Display HAL:** `gralloc.default` can open `/dev/fb0`, but the
   `simple-framebuffer` is a single buffer with no page flip or vsync. A small
   HWC/fbdev presenter is likely needed.
4. **SELinux:** the policy header is compatible with Linux 6.12 (version 26),
   but loading the policy on the Vita is unverified. Keep the permissive and
   disabled DTBs available.
5. **netd:** Android 5.1's netd expects the out-of-tree `xt_qtaguid` module.
   Expect errors; it should not block adbd.
6. **Wi-Fi:** the glibc supplicant starts and its control socket comes up, but
   DHCP and adb over TCP have not completed a full on-device boot yet.
7. **Input, audio, battery, sensors:** input drivers exist; everything else is a
   stub or missing.

## Build

Requirements on the build host: bash, Python 3, make, an ARM cross compiler
(`arm-linux-gnueabihf-gcc`), bc, bison, flex, libssl-dev, libelf-dev, and
`device-tree-compiler` (`fdtput`). The CI workflow installs these on Ubuntu
24.04.

```sh
bash scripts/build.sh
```

The script fetches the pinned LinuxOnVita kernel tree (unless `KERNEL_SRC` is
set), unpacks the prebuilt Android 5.1.1 ramdisk, adds the Vita overlay, busybox,
glibc Wi-Fi userspace and firmware, merges the Android kernel fragment, builds
`zImage` + `vita1000.dtb`, patches the DTB bootargs, and writes:

```
build/out/zImage
build/out/vita.dtb                     permissive SELinux
build/out/vita-selinux-disabled.dtb    fallback
build/out/vita1000.dtb                 unpatched, for reference
build/out/initramfs.cpio
build/out/SHA256SUMS
build/out/build-info.txt
```

The repository CI workflow runs the same command and uploads those files as the
`android-on-vita-boot` artifact. A local build is also possible on Linux, WSL2,
or Docker.

## Install and first boot

1. Back up the current `ux0:/linux/` directory. Keep LinuxOnVita's working
   `zImage`/`vita.dtb` pair so you can switch back.
2. Copy the new `zImage` and `vita.dtb` to `ux0:/linux/`. Keep
   `baremetal-loader.skprx` and `payload.bin` from LinuxOnVita.
3. Copy `system.img` and `userdata.img` to `ur0:/android/`.
4. Optional: copy a `wpa_supplicant.conf` to `ur0:/android/wpa_supplicant.conf`
   (template in
   [docs/getting-started.md](docs/getting-started.md#3-copy-files-to-the-vita)).
5. Launch LinuxOnVita from LiveArea and press **CROSS** to boot the existing
   files.
6. Watch the framebuffer console. When Android init starts adbd, connect from
   the PC:

```sh
adb connect <vita-ip>:5555
adb shell
```

7. To try the framework/UI later:

```sh
adb shell setprop vita.ui 1
```

or create `ur0:/android/ui` before boot.

If the permissive DTB fails at SELinux policy load, retry with
`vita-selinux-disabled.dtb` renamed to `vita.dtb`. To return to VitaOS, reboot
the console; hold **L** during power-on if a taiHEN plugin misbehaves.

## Documentation

- [docs/getting-started.md](docs/getting-started.md) - build, copy, boot, and
  debug the first Android image.
- [docs/feasibility.md](docs/feasibility.md) - hardware, software stack, what
  works, what does not, and the honest verdict.
- [docs/android-version.md](docs/android-version.md) - version comparison, the
  prebuilt-image decision, and low-RAM tuning.
- [docs/drivers-and-firmware.md](docs/drivers-and-firmware.md) - why Vita
  firmware cannot be reused, what drivers exist, and the firmware blobs Android
  actually needs.
- [docs/boot-and-storage.md](docs/boot-and-storage.md) - boot chain, kernel
  changes, initramfs, loop-mounted images, display, input, and Wi-Fi paths.
- [docs/glbench.md](docs/glbench.md) - the software GLES2 performance gate:
  what it measures, how to run it on the Vita, and how to read the result.
- [docs/gpu-research.md](docs/gpu-research.md) - why a PowerVR SGX driver is
  not a v0.2 path, and what a real probe would have to prove.
- [docs/roadmap.md](docs/roadmap.md) - phased plan with exit criteria.
- [docs/sources.md](docs/sources.md) - links to the projects and references.
- [docs/open-questions.md](docs/open-questions.md) - decisions made and the
  remaining unknowns.
- [docs/ur0-storage-plan.md](docs/ur0-storage-plan.md) - ur0-only image layout,
  capacity budget, risks, and fallbacks (no SD2Vita).
- [scripts/vita-inventory.sh](scripts/vita-inventory.sh) - read-only hardware
  inventory for a running LinuxOnVita install.

## Safety

Booting bare-metal Linux bypasses VitaOS storage drivers. A full console backup
has already been taken for this unit; keep it current. Keep at least 300-500 MB
free on ur0 for VitaOS and exFAT metadata, and use clean shutdowns. A hung boot
can corrupt an exFAT partition; if that happens, restore from the backup rather
than repeatedly power-cycling.
