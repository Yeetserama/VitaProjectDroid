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

1. **logger** (`CONFIG_LOG`) - Provides the flat `/dev/log_main`, `/dev/log_radio`,
   `/dev/log_events`, and `/dev/log_system` nodes. The 4.2 init script links the
   `/dev/log/*` names that liblog opens to these nodes.
2. **lowmemorykiller** (`CONFIG_ANDROID_LOW_MEMORY_KILLER`) - Kills processes based on oom_adj

Both have been backported from Linux 4.19 and placed in:
- `kernel/logger/`
- `kernel/lowmemorykiller/`

The kernel config fragment is `kernel/config/vita-android-4.2.fragment`.

## Building

### Prerequisites

1. Download and verify the API 17 armeabi-v7a SDK image:
   ```bash
   scripts/download-android-4.2.sh
   ```
   This fetches the pinned `armeabi-v7a-17_r06.zip` (size and SHA1 are
   verified), extracts `ramdisk.img`, and creates a fresh 512 MiB ext4
   `userdata.img`. Outputs:
   - `prebuilt/ramdisk-4.2.img` (embedded in the initramfs)
   - `prebuilt/system-4.2.img` (copy to `ur0:/android/system.img`)
   - `prebuilt/userdata-4.2.img` (copy to `ur0:/android/userdata.img`)

   No Android SDK or `sdkmanager` installation is required.

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
- `system-4.2.img` (CI artifact copy of `prebuilt/system-4.2.img`)
- `userdata-4.2.img` (CI artifact copy of `prebuilt/userdata-4.2.img`)

## Installation on Vita

1. Copy kernel artifacts to `ux0:/linux/`:
   ```
   zImage-4.2 -> ux0:/linux/zImage
   vita.dtb-4.2 -> ux0:/linux/vita.dtb
   vita-selinux-disabled.dtb-4.2 -> ux0:/linux/vita-selinux-disabled.dtb
   ```

2. Copy Android images to `ur0:/android/`:
   ```
   system-4.2.img -> ur0:/android/system.img
   userdata-4.2.img -> ur0:/android/userdata.img
   ```
   The CI artifact `android-4.2.2-boot` contains both images. When building
   locally they are `prebuilt/system-4.2.img` and `prebuilt/userdata-4.2.img`.

3. Create `ur0:/android/wpa_supplicant.conf` for Wi-Fi:

   ```
   network={
       ssid="YourNetwork"
       psk="YourPassword"
       key_mgmt=WPA-PSK
   }
   ```

   The SD8787 is 2.4 GHz only. Only the network blocks and radio settings
   are used: at boot the port rewrites the control-interface line to
   `ctrl_interface=DIR=/data/system/wpa_supplicant GROUP=1010` so that
   WifiService and the supplicant share the same socket. The file on ur0 is
   re-read on every boot, so add or change networks there; networks saved
   through Settings are not written back to ur0.

4. Boot via LinuxOnVita (press CROSS to boot the new zImage). The framework
   and launcher start automatically, with the on-screen navigation bar
   enabled (`qemu.hw.mainkeys=0`).

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

The UI starts automatically. `vita.ui=1` is set by `init.vita.rc` at the end
of `on boot`, after the headless safety stops, which starts SurfaceFlinger,
media, and zygote. For a headless debug boot, remove that `setprop` line from
`initramfs-overlay-4.2/init.vita.rc`; `adb shell setprop vita.ui 1` still
starts the framework manually.

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
│   ├── system-4.2.img                 # 4.2 system image (downloaded)
│   ├── userdata-4.2.img               # fresh 512 MiB /data (created)
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
- `android-4.2.2` - Android 4.2.2 (this branch)
- `android-gingerbread` - Android 2.3.7 Gingerbread

`android-4.2` is kept as the pre-fix 4.2 base; all new 4.2 work happens on
`android-4.2.2`.

Switch between versions:
```bash
git checkout android-4.2.2      # Work on 4.2.2
git checkout android-gingerbread # Work on Gingerbread
git checkout main               # Work on 5.1
```

## References

- [Android 4.2 Platform Highlights](https://android-developers.googleblog.com/2012/11/introducing-android-42-new-and-improved.html)
- [Linux 4.19 logger driver](https://github.com/torvalds/linux/blob/v4.19/drivers/staging/android/logger.c)
- [Linux 4.19 lowmemorykiller driver](https://github.com/torvalds/linux/blob/v4.19/drivers/staging/android/lowmemorykiller.c)
