# Android 4.2.2 (Jelly Bean, API 17) on PS Vita

## Status: Experimental

This document describes the Android 4.2.2 bring-up for the PlayStation Vita,
based on the Android 5.1.1 port but with the necessary kernel and userspace
changes for API 17.

## Key Differences from Android 5.1

| Component | Android 5.1 | Android 4.2 |
|-----------|-------------|-------------|
| **Kernel logger** | userspace `logd` | kernel `CONFIG_LOG` (staging, backported) |
| **Low memory killer** | userspace `lmkd` | kernel `CONFIG_ANDROID_LOW_MEMORY_KILLER` (staging, backported) |
| **Binder ABI** | 32-bit, protocol 7 | 32-bit, protocol 7 |
| **SELinux** | permissive/enforcing | permissive (default in 4.2) |
| **System image** | `android-22` SDK | `android-17` SDK |
| **Ramdisk** | 5.1 ramdisk | 4.2 ramdisk |
| **ART/Dalvik** | ART only | Dalvik + ART (optional) |

## Kernel Requirements

Android 4.2 requires two kernel staging drivers that were removed in Linux 6.12:

1. **logger** (`CONFIG_LOG`) - Provides `/dev/logger/*` for kernel logging
2. **lowmemorykiller** (`CONFIG_ANDROID_LOW_MEMORY_KILLER`) - Kills processes based on oom_adj

Both have been backported from Linux 4.19 and placed in:
- `kernel/logger/`
- `kernel/lowmemorykiller/`

The kernel config fragment is `kernel/config/vita-android-4.2.fragment`.

## Building

### Prerequisites

1. Android SDK with API 17 system image:
   ```bash
   sdkmanager "system-images;android-17;default;armeabi-v7a"
   ```

2. Download the SDK images (ramdisk, system, userdata):
   ```bash
   scripts/download-android-4.2.sh
   ```
   This copies `ramdisk.img` to `prebuilt/ramdisk-4.2.img`.

### Build Commands

```bash
# Build Android 4.2 boot package
ANDROID_VERSION=4.2 scripts/build.sh

# Or with a custom kernel tree
KERNEL_SRC=/path/to/linux_vita ANDROID_VERSION=4.2 scripts/build.sh
```

### Artifacts

The build produces version-suffixed artifacts in `build/out/`:
- `zImage-4.2`
- `vita.dtb-4.2` (permissive SELinux)
- `vita-selinux-disabled.dtb-4.2` (fallback)
- `vita1000.dtb-4.2` (unpatched reference)
- `initramfs.cpio-4.2`
- `SHA256SUMS-4.2`
- `build-info.txt-4.2`

## Installation on Vita

1. Copy kernel artifacts to `ux0:/linux/`:
   ```
   zImage-4.2 -> ux0:/linux/zImage
   vita.dtb-4.2 -> ux0:/linux/vita.dtb
   vita-selinux-disabled.dtb-4.2 -> ux0:/linux/vita-selinux-disabled.dtb
   ```

2. Copy Android images to `ur0:/android/`:
   ```
   system.img (from Android 4.2 SDK) -> ur0:/android/system.img
   userdata.img (from Android 4.2 SDK) -> ur0:/android/userdata.img
   ```

3. Create `ur0:/android/wpa_supplicant.conf` for Wi-Fi.

4. Boot via LinuxOnVita (press CROSS to boot the new zImage).

## Known Issues

1. **Dalvik vs ART**: Android 4.2 uses Dalvik by default. ART is available but experimental.
   The SDK image may need `dalvik.vm.dex2oat-*` properties tuned.

2. **Graphics**: Same software renderer fallback as 5.1 (`ro.kernel.qemu=1`,
   `ro.kernel.qemu.gles=0`). GLES 2.0 apps will not work.

3. **SELinux**: Android 4.2 SELinux policy is older. The permissive DTB should work.

4. **Memory**: 4.2 has a smaller memory footprint than 5.1, which is an advantage
   on the Vita's 512 MB RAM.

5. **Netd routing**: Android 4.2's netd does not have the same RouteController
   issue as 5.1, but the routing repair code is kept for safety.

## Testing

After boot, connect via adb:
```bash
adb connect <vita-ip>:5555
adb shell
```

Check kernel log:
```bash
adb shell cat /mnt/ur0/android/boot.log
```

Enable UI:
```bash
adb shell setprop vita.ui 1
# or touch ur0:/android/ui and reboot
```

## Directory Structure

```
androidonvita/
├── kernel/
│   ├── config/
│   │   ├── vita-android.fragment      # 5.1 config
│   │   └── vita-android-4.2.fragment  # 4.2 config
│   ├── ashmem/                        # Shared (both versions)
│   ├── logger/                        # 4.2 only (backported)
│   └── lowmemorykiller/               # 4.2 only (backported)
├── initramfs-overlay/                 # 5.1 initramfs overlay
├── initramfs-overlay-4.2/             # 4.2 initramfs overlay
├── prebuilt/
│   ├── ramdisk.img                    # 5.1 ramdisk
│   ├── ramdisk-4.2.img                # 4.2 ramdisk (downloaded)
│   └── busybox-armv7l
├── scripts/
│   ├── build.sh                       # Main build (supports VERSION)
│   ├── build-initramfs.sh             # Initramfs build (supports VERSION)
│   ├── apply-kernel.sh                # Kernel build (supports VERSION)
│   ├── patch-kernel-tree.py           # Kernel tree patching
│   └── download-android-4.2.sh        # Download 4.2 SDK images
└── docs/
    ├── android-version.md             # Version comparison
    └── android-4.2.md                 # This file
```

## Branch Strategy

Each Android version is maintained on a separate Git branch:
- `main` or `feat/ui-software-renderer` - Android 5.1.1 (primary)
- `android-4.2` - Android 4.2.2 (this branch)

Switch between versions:
```bash
git checkout android-4.2   # Work on 4.2
git checkout main          # Work on 5.1
```

## References

- [Android 4.2 Platform Highlights](https://android-developers.googleblog.com/2012/11/introducing-android-42-new-and-improved.html)
- [Linux 4.19 logger driver](https://github.com/torvalds/linux/blob/v4.19/drivers/staging/android/logger.c)
- [Linux 4.19 lowmemorykiller driver](https://github.com/torvalds/linux/blob/v4.19/drivers/staging/android/lowmemorykiller.c)