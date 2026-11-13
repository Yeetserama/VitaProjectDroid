# Android 6.0 (Marshmallow, API 23) on PS Vita

## Status: Experimental, headless bring-up

Branch: `android-6.0`, based on the verified 5.1.1 swap boot
(`feat/ui-software-renderer` at `ed79205`).

Android 6.0 uses the official SDK image
`system-images;android-23;default;armeabi-v7a` (build `MASTER 3079352`,
`userdebug`, `armeabi-v7a`). It is a 32-bit ARMv7 userspace and therefore the
last SDK generation that can run on the Vita's Cortex-A9. API 24 (7.0) still
ships `armeabi-v7a`; API 25 and newer ship only `arm64-v8a`.

The kernel and boot chain are the 5.1.1 ones. Android 6.0 still uses userspace
`logd` and `lmkd`, so the 5.1 kernel fragment is sufficient: 32-bit binder
(protocol 7), ashmem, ext4, exFAT, loop, zram, swap, mwifiex, and SELinux.

> This is a bring-up port. The UI is disabled on boot until the headless boot
> has been verified on hardware. Do not expect 6.0 to be faster or more usable
> than 5.1.1; it has a larger framework, ART, and a higher memory floor.

## What differs from 5.1.1

| Component | 5.1.1 | 6.0 |
| --- | --- | --- |
| SDK image | API 22 | API 23 |
| Ramdisk | `prebuilt/ramdisk.img` | `prebuilt/ramdisk-6.0.img` |
| Overlay | `initramfs-overlay/` | `initramfs-overlay-6.0/` |
| Storage volume | `/storage/sdcard` | `/storage/sdcard` |
| External storage env | not exported by the SDK ramdisk | `/sdcard` by default; overridden to `/storage/sdcard` |
| `init.rc` imports | `/init.${ro.hardware}.rc` | `/init.${ro.hardware}.rc` (same) |
| Kernel binder ABI | 32-bit, protocol 7 | 32-bit, protocol 7 |
| Kernel fragment | `vita-android.fragment` | `vita-android.fragment` (same) |
| System image | 750 MiB, unmodified | shrunk to the ext4 minimum (464 MiB in CI run 43345477531), capped at 900 MiB |
| Userdata | 550 MiB | 640 MiB, fresh ext4 |
| Swap | 128 MiB zram + 512 MiB ur0 | same |

The 6.0 SDK ramdisk's `init.environ.rc` exports `EXTERNAL_STORAGE=/sdcard`, and
`init.rc` creates `symlink /storage/self/primary /sdcard`. The framework then
matches that path against `storage_list.xml`, which still declares a single
fixed primary volume at `/storage/sdcard`. `init.vita.rc` overrides the
environment variable to the real mount point and creates the directory before
vold starts. The volume remains unmounted because there is no vold-managed
backing device, exactly as in the 5.1 port; the launcher does not need it.

### Binder protocol

Android 6.0's `libbinder` is built for 32-bit ARM, so it is compiled with
`BINDER_IPC_32BIT`. The bionic header then defines
`BINDER_CURRENT_PROTOCOL_VERSION` as **7**, not 8:

```c
#ifdef BINDER_IPC_32BIT
#define BINDER_CURRENT_PROTOCOL_VERSION 7
#else
#define BINDER_CURRENT_PROTOCOL_VERSION 8
#endif
```

The kernel is built with the same 32-bit ABI (`-DBINDER_IPC_32BIT` in
`drivers/android/Makefile`, verified by `apply-kernel.sh`), so the protocol
matches. This is the same combination that already works on 5.1.1.

## Building

Requirements are the 5.1 build requirements plus `e2fsprogs` (`mke2fs`,
`resize2fs`, `e2fsck`, `dumpe2fs`) and GNU `truncate`.

```sh
bash scripts/download-android-6.0.sh
ANDROID_VERSION=6.0 bash scripts/build.sh
```

The download script:

1. fetches and SHA1-verifies `armeabi-v7a-23_r06.zip`
   (238,333,358 bytes, SHA1 `7cf2ad756e54a3acfd81064b63cb0cb9dff2798d`);
2. extracts `ramdisk.img`, `system.img`, and `build.prop`;
3. verifies that the image really is API 23 / Android 6.0;
4. copies the ramdisk to `prebuilt/ramdisk-6.0.img`;
5. shrinks `system.img` with `resize2fs -M` and truncates the file to the new
   block count (the SDK image is 1.5 GiB but only about 500 MB is used);
6. creates a fresh 640 MiB ext4 `userdata-6.0.img` with `mke2fs`.

All generated files are gitignored. A CI run performs the same steps and
uploads `android-6.0-boot`, which contains:

```
build/out/zImage-6.0
build/out/vita.dtb-6.0
build/out/vita1000.dtb-6.0
build/out/vita-selinux-disabled.dtb-6.0
build/out/initramfs.cpio-6.0
build/out/SHA256SUMS-6.0
build/out/build-info.txt-6.0
prebuilt/system-6.0.img
prebuilt/userdata-6.0.img
```

## Installing on the Vita

There is no SD2Vita. Boot files stay on the Sony card; images stay on ur0.

| File | Destination |
| --- | --- |
| `zImage-6.0` | `ux0:/linux/zImage` |
| `vita.dtb-6.0` | `ux0:/linux/vita.dtb` |
| `vita-selinux-disabled.dtb-6.0` | `ux0:/linux/vita-selinux-disabled.dtb` (fallback) |
| `system-6.0.img` | `ur0:/android/system.img` |
| `userdata-6.0.img` | `ur0:/android/userdata.img` |
| `wpa_supplicant.conf` | `ur0:/android/wpa_supplicant.conf` |

Keep LinuxOnVita's `baremetal-loader.skprx` and `payload.bin`. Boot with
**CROSS**, never the installer's TRIANGLE/reinstall path.

Only one Android version can be installed at a time: the file names on the
Vita are the same for every branch. Back up the 5.1.1 images or be prepared to
copy them again. Deleting `ur0:/android/swap.img` frees 512 MiB and is safe;
the boot recreates it on the next 5.1.1/6.0 boot.

The 6.0 image budget is roughly:

| Item | Size |
| --- | --- |
| `system.img` | 464 MiB in the current build (hard cap 900 MiB) |
| `userdata.img` | 640 MiB |
| `swap.img` | 512 MiB |
| kernel/DTB/config | ~12 MiB |
| **Total** | **~1.6 GiB** |

Leave at least 300-500 MB free on ur0 for VitaOS and exFAT metadata. If
`resize2fs` cannot shrink the system image below 900 MiB, the download script
fails rather than hand you an image that will not fit.

## First boot: headless only

The overlay stops zygote, SurfaceFlinger, media, drm, and RIL on boot and only
starts adbd. This is deliberate: verify the kernel/userspace handoff before
adding the CPU and memory cost of the framework.

1. Boot with CROSS.
2. Watch the framebuffer console. `init.vita` mounts ur0, creates the swap
   image on first boot, then hands off to Android `init`.
3. From the PC:

   ```sh
   adb connect <vita-ip>:5555
   adb shell getprop ro.build.version.release   # must print 6.0
   adb shell getprop ro.build.version.sdk       # must print 23
   adb shell getprop sys.boot_completed         # headless may stay 0
   adb shell free
   adb shell cat /proc/swaps
   adb shell dmesg | tail -n 100
   ```

   `/proc/swaps` should list `/dev/zram0` (128 MiB) and `/dev/loop2`
   (512 MiB). If `swap.img` could not be created or attached, the boot log
   says so and continues with zram only.

4. Pull the logs if adb is not reachable:

   - `ur0:/android/boot.log` — synced kernel/service snapshots.
   - `ur0:/android/vita-net.log` — Wi-Fi, DHCP, routing repair, adb listener.

The stage-1 script leaves ur0 read-write and copies optional override scripts
(`vita-net.sh`, `vita-log.sh`, `vita-ui.sh`) from `ur0:/android/` over the
embedded copies. Delete stale overrides before testing a new build.

## Enabling the UI

Only after the headless boot is clean:

```sh
adb shell setprop vita.ui 1
```

or create an empty `ur0:/android/ui` before boot. The trigger starts
SurfaceFlinger first, then zygote. The 6.0 SDK ships the same emulator EGL/GLES
drivers as 5.1; `default.prop` sets `ro.kernel.qemu=1` and
`ro.kernel.qemu.gles=0` so `libEGL` selects `libGLES_android.so` (PixelFlinger,
GLES 1.x). SurfaceFlinger 6.0 has a software fallback, but HWUI and app
rendering remain software and slow.

Expect 6.0 to need more RAM than 5.1.1. Watch `dumpsys meminfo`, `logcat` for
`lowmemorykiller`/`lmkd` kills, and the zram/swap counters. The 512 MiB disk
swap is a survival mechanism, not a performance fix.

## Known risks and open questions

- **Memory:** 6.0's framework and ART have a higher floor than 5.1.1. The
  launcher may start and then be killed under pressure.
- **Dex2oat:** first boot compiles the framework and system apps. On the
  444 MHz Cortex-A9 this can take a long time; do not assume a hang before
  checking `logcat` and CPU activity.
- **Storage volume:** `/storage/sdcard` exists but is not a real vold volume.
  Apps that require mounted external storage still fail.
- **SELinux:** the port boots permissive. The 6.0 `sepolicy` is loaded from the
  SDK ramdisk; a policy denial that is fatal in enforcing mode may only appear
  as a logged denial here.
- **Graphics:** no SGX driver. PixelFlinger is the only renderer.
- **Binder:** protocol 7 is correct for 32-bit 6.0, but the combination is not
  yet hardware-verified on the Vita.

## Next step after 6.0 headless

Android 7.0 (API 24) still has an `armeabi-v7a` SDK image. It requires a
different ramdisk and an `init.vita.rc` that imports `/system/etc/init` as a
directory, because 7.0's `init.cpp` only parses `/init.rc` directly. Do not
start 7.0 until 6.0 reaches a stable headless boot and the 512 MiB swap has
been verified on hardware.
