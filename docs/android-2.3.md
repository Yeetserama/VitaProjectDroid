# Android 2.3.7 (Gingerbread, API 10) on PS Vita

## Status: Experimental (headless boot works; UI start still crashes)

The headless bring-up works: Android 2.3.7 boots, adbd is reachable over
Wi-Fi, and the core services stay up. Enabling the framework is not stable yet.
When `vita.ui=1` is set, `zygote` exits unexpectedly and `media` is killed and
restarted with it. This is the same class of failure that Android 5.1.1 hit
before its zygote preload crash was fixed. The difference is that Gingerbread
has no userspace `logd`, so the crash has to be read from the kernel log and
the preserved logcat snapshot.

The `vita-diag` service now captures logcat, dmesg, ANR traces, tombstones and
memory state into `ur0:/android/diag/` on every zygote or media restart. The
next bring-up step is to reproduce the crash once with those files present and
read `last-crash.txt`.

5.1.1 is also not a usable daily driver yet: its UI starts, but launching an
app can take an unusually long time. That is a separate performance problem,
not the Gingerbread crash.

This document describes the Android 2.3.7 bring-up for the PlayStation Vita
(PCH-1000), based on the Android 5.1.1 port. It is a separate, self-contained
boot package: Gingerbread has no SELinux, no `mount_all`, and no userspace
`logd`/`lmkd`.

## Version facts

| Item | Value |
| --- | --- |
| Android release | 2.3.7 (Gingerbread) |
| API level | 10 |
| SDK package | `system-images;android-10;default;armeabi-v7a` revision 5 |
| SDK archive | `armeabi-v7a-10_r05.zip`, 67,927,049 bytes, SHA1 `8537616a7add47cce24c60f18bc2429e3dc90ae3` |
| Build id | `sdk-eng 2.3.7 GINGERBREAD 3644351 test-keys` |
| System image | ext4, 576,716,800 bytes (~550 MiB) |
| Userdata image | fresh ext4, 256 MiB (the SDK's 2 GiB image does not fit on ur0) |
| Runtime | Dalvik (odexed system apps) |

## Key differences from Android 4.2 / 5.1

| Component | Android 5.1 | Android 4.2 | Android 2.3 |
| --- | --- | --- | --- |
| Kernel logger | userspace `logd` | `CONFIG_LOG`, `/dev/logger/*` | `CONFIG_LOG`, `/dev/log_*` + `/dev/log/*` symlinks |
| Low memory killer | userspace `lmkd` | `CONFIG_ANDROID_LOW_MEMORY_KILLER` | `CONFIG_ANDROID_LOW_MEMORY_KILLER` |
| SELinux | permissive/enforcing | permissive | none (introduced in Android 4.3) |
| Init filesystem mount | `mount_all /fstab.vita` | `mount_all /fstab.vita` | explicit `mount` commands |
| SurfaceFlinger | init service | init service | inside `system_server` (`system_init.cpp`) |
| AudioFlinger | init service | init service | inside `media` (`mediaserver`) |
| Software EGL | `libGLES_android.so` | `libGLES_android.so` | `libGLES_android.so` via bind-mounted `egl.cfg` |
| External storage | `/storage/sdcard` | `/storage/sdcard` | `/mnt/sdcard` |

### Logger compatibility

The shared 4.19 logger driver names its class devices `logger/log_main` on the
4.2/5.1 branches. Android 2.3's ueventd is older: it ignores `DEVNAME` and
derives the node path from the last component of `DEVPATH`, which the kernel
rewrites from `logger/log_main` to `logger!log_main`. That would leave
`/dev/logger!log_main` nodes that nothing opens.

The Gingerbread branch therefore registers flat names (`log_main`, ...), which
ueventd maps to `/dev/log_main` and friends. Gingerbread's `liblog` still opens
`/dev/log/main`, so the `on init` action in `init.vita.rc` creates links after
Android init has mounted its fresh tmpfs over `/dev`:

```
/dev/log/main   -> /dev/log_main
/dev/log/radio  -> /dev/log_radio
/dev/log/events -> /dev/log_events
/dev/log/system -> /dev/log_system
```

The nodes themselves are created by ueventd on coldboot with mode `0662`
(`root:log`) from the appended `/ueventd.rc` rules.

### ueventd rules

Android 2.3's ueventd reads `/ueventd.rc` and then
`/ueventd.${hardware}.rc`, where `${hardware}` comes from `/proc/cpuinfo`, not
from `androidboot.hardware`. The Vita reports a generic board name, so
`build-initramfs.sh` appends `ueventd.vita.rc` to the stock `/ueventd.rc`
rather than installing it under a versioned name.

### Filesystem mounting

Gingerbread's init has no `mount_all` builtin. `/init.vita` mounts
`/mnt/ur0`, attaches the images to loop devices and mounts `/system`
read-only. `init.vita.rc` then mounts `/data` (`/dev/block/loop1`, as created
by ueventd coldboot) and a tmpfs `/cache`. `fstab.vita` is kept as
documentation only.

`/system` is mounted before Android init because the SDK `egl.cfg` selects the
emulator GL pipe (`libEGL_emulation.so`), which does not exist on the Vita.
`/init.vita` bind-mounts `egl.cfg.vita` (`0 0 android`) over
`/system/lib/egl/egl.cfg`, selecting the bundled software renderer
(`libGLES_android.so`).

### Headless gate

`init.vita.rc` runs `stop zygote`, `stop media` and `stop bootanim` in
`on init`. On Gingerbread, `stop` before the service starts sets
`SVC_DISABLED`, so the later `class_start default` in `init.rc` skips them. The
core services (`servicemanager`, `vold`, `netd`, `adbd`, `debuggerd`) still
start, which makes headless bring-up debuggable over adb.

`adb shell setprop vita.ui 1` (or touching `ur0:/android/ui`) starts `media`,
then `vita-ui-start` waits up to 15 seconds for mediaserver to reach `running`
and gives it two more seconds to register its binder services before it runs
`setprop ctl.start zygote`. SurfaceFlinger is instantiated by `system_server`
itself (Gingerbread has no separate `surfaceflinger` init service);
AudioFlinger, MediaPlayerService and AudioPolicyService live in `media`
(`mediaserver`), so both services are needed before `AudioService` can finish
starting. The short delay removes the start-order race where `AudioService`
could connect to a mediaserver that had forked but not yet registered.

`zygote` has `onrestart restart media` and `onrestart restart netd` in the
stock `init.rc`. A zygote crash therefore also makes init kill and restart
`media`, which is why the logs show both services dying together. The zygote
exit is the root event; `media` is collateral.

## Kernel requirements

The kernel config fragment is `kernel/config/vita-android-2.3.fragment`. On top
of the shared Android options it enables the backported logger
(`CONFIG_LOG`) and lowmemorykiller (`CONFIG_ANDROID_LOW_MEMORY_KILLER`), and it
must leave SELinux off:

```
# CONFIG_SECURITY_SELINUX is not set
# CONFIG_SECURITY_SELINUX_DEVELOP is not set
# CONFIG_SECURITY_SELINUX_BOOTPARAM is not set
```

`scripts/apply-kernel.sh` enforces this: for `ANDROID_VERSION=2.3` it requires
both staging drivers and fails if any SELinux symbol is enabled or if
`CONFIG_LSM` mentions selinux. The DTB bootargs therefore omit
`androidboot.selinux`, and no `vita-selinux-disabled.dtb` is produced.

## Building

### 1. Download and prepare the SDK images

```bash
scripts/download-android-2.3.sh
```

The script downloads the pinned API 10 archive, verifies its SHA1, and writes:

| File | Purpose |
| --- | --- |
| `prebuilt/ramdisk-2.3.img` | embedded by `build-initramfs.sh` (SHA256 pinned) |
| `prebuilt/system-2.3.img` | copy to `ur0:/android/system.img` |
| `prebuilt/userdata-2.3.img` | fresh 256 MiB ext4, copy to `ur0:/android/userdata.img` |

Creating a fresh userdata image avoids the SDK's 2 GiB file and gives a clean
first boot. If `userdata.img` is missing on the Vita at boot, `/init.vita`
creates one with busybox `mke2fs` as a fallback.

### 2. Build the boot package

```bash
ANDROID_VERSION=2.3 scripts/build.sh
```

With a pre-fetched kernel tree:

```bash
KERNEL_SRC=/path/to/linux_vita ANDROID_VERSION=2.3 scripts/build.sh
```

### Artifacts

`build/out/` contains:

- `zImage-2.3`
- `vita.dtb-2.3` (patched bootargs, no SELinux)
- `vita1000.dtb-2.3` (unpatched reference)
- `initramfs.cpio-2.3`
- `SHA256SUMS-2.3`
- `build-info.txt-2.3`

## Installation on the Vita

No SD2Vita is used.

1. Copy boot files to the Sony card (`ux0:/linux/`):

   ```
   zImage-2.3   -> ux0:/linux/zImage
   vita.dtb-2.3 -> ux0:/linux/vita.dtb
   ```

   Keep a backup of the existing 5.1 `zImage`/`vita.dtb` if you want to switch
   back.

2. Copy the Android images to `ur0:/android/`:

   ```
   system-2.3.img   -> ur0:/android/system.img
   userdata-2.3.img -> ur0:/android/userdata.img
   ```

   Check free space first. System plus userdata need about 800 MiB. The boot
   also creates a 512 MiB `ur0:/android/swap.img` on first start, so leave at
   least 1.4 GiB free and remove the 4.2/5.1 images if ur0 is tight. If ur0 is
   too full, swap creation and swap-on fail safely and Android still boots
   with zram only.

3. Optional Wi-Fi config at `ur0:/android/wpa_supplicant.conf`:

   ```
   ctrl_interface=/data/misc/wifi/sockets
   update_config=1

   network={
       ssid="YourNetwork"
       psk="YourPassword"
       key_mgmt=WPA-PSK
   }
   ```

   The SD8787 is 2.4 GHz only.

4. Boot with **CROSS**. Do not use the TRIANGLE/reinstall path.

> **Warning:** Gingerbread uses a different `/init.vita.rc` and a different
> kernel config. Do not mix the 2.3 `zImage` with the 5.1 initramfs, or the 5.1
> `zImage` with the 2.3 images.

## Testing

```bash
adb connect <vita-ip>:5555
adb shell
```

Check what booted:

```bash
adb shell getprop ro.build.version.release   # 2.3.7
adb shell getprop ro.build.version.sdk       # 10
adb shell getprop init.svc.zygote            # stopped until vita.ui=1
adb shell cat /mnt/ur0/android/boot.log
adb shell free                               # zram + ur0 swap, if enabled
adb shell cat /proc/swaps
```

The first boot creates `ur0:/android/swap.img` (512 MiB). That takes longer
than later boots; watch for `ur0 swap enabled` in `boot.log`. If `free` shows
no ur0 swap, read the `swap.img` lines in `boot.log` before deleting anything.

Enable the UI:

```bash
adb shell setprop vita.ui 1
adb shell getprop init.svc.zygote            # running
adb shell dumpsys SurfaceFlinger | head
```

Or create the marker before boot:

```
ur0:/android/ui
```

If zygote or media restart, the monitor keeps the newest four snapshots:

```bash
adb pull /mnt/ur0/android/diag
adb shell cat /mnt/ur0/android/diag/last-crash.txt
```

`last-crash.txt` contains `getprop`, `ps`, `/proc/meminfo`, every logcat
buffer, and the tail of dmesg at the moment of the crash. Pull it before
rebooting: the next boot overwrites `boot.log`, and only the diag directory is
preserved.

## Known issues and follow-ups

1. **Zygote crashes when the UI starts.** Setting `vita.ui=1` makes `zygote`
   exit, and init's stock `onrestart restart media` then kills and restarts
   `media` too. This is the same symptom class as the earlier Android 5.1.1
   failure, not a media bug. The `vita-ui-start` helper now gives mediaserver a
   head start and `vita-diag` preserves a crash snapshot, but the underlying
   zygote exception has not been identified yet. Pull
   `ur0:/android/diag/last-crash.txt` before rebooting and read the last
   `logcat` lines from the dying `zygote` process.

2. **Software rendering only.** `libGLES_android.so` is GLES1. It is enough for
   SurfaceFlinger 2.3 and Launcher2, but not for GLES2 apps.

3. **Input mapping.** Gingerbread's `EventHub` looks for key layouts and IDC
   files under `/system/usr/keylayout` and `/system/usr/idc`. The Vita KL/IDC
   files in the initramfs are not used yet; without them the Vita buttons fall
   back to `qwerty.kl`. Touch should still be detected (evdev), but the mapping
   needs a follow-up bind mount or a copied `/system/usr` overlay.

4. **No SD card / vold volume.** The 2.3 ramdisk's `vold.fstab.goldfish` and
   `vold.fstab.ranchu` are removed, and the `vold` service starts without a
   backing volume. There is no `/mnt/sdcard` until a loop-backed volume is
   added.

5. **Wi-Fi.** Uses the same glibc `wpa_supplicant` and SD8787 firmware as 5.1.
   It is started as a default-class oneshot (`vita-net`) because Gingerbread
   has no `late_start` class.

6. **Memory.** 512 MB RAM is still the limit. The boot adds 128 MiB of zram and
   a 512 MiB `ur0:/android/swap.img` through `/dev/loop2`, so the kernel can
   address about 1 GiB of anonymous memory. The SD8787-era Dalvik heap cap was
   also reduced from 192 MiB to 64 MiB and the SDK eng build's global CheckJNI
   was disabled; both changes cut memory use and abort risk. Disk swap is much
   slower than RAM; expect slow app startup and occasional thrashing, not
   desktop-class multitasking. `free` and `/proc/swaps` show whether both areas
   came up.

7. **UI start is still under test.** Headless boot has been verified on
   hardware; the `vita.ui=1` path crashes and is the current blocker. Do not
   treat this as a usable build yet.

## Directory structure

```
androidonvita/
├── kernel/
│   ├── config/
│   │   └── vita-android-2.3.fragment  # Gingerbread kernel config
│   ├── ashmem/                        # shared Android driver
│   ├── logger/                        # backported kernel logger
│   └── lowmemorykiller/               # backported lowmemorykiller
├── initramfs-overlay-2.3/             # stage-1 init, init.vita.rc, egl.cfg
├── prebuilt/
│   ├── ramdisk-2.3.img                # API 10 ramdisk (verified)
│   └── busybox-armv7l
├── scripts/
│   ├── build.sh
│   ├── build-initramfs.sh
│   ├── apply-kernel.sh
│   └── download-android-2.3.sh
└── docs/
    └── android-2.3.md                 # this file
```

## Branch strategy

Each Android version is maintained on its own branch:

- `main` / `feat/ui-software-renderer` - Android 5.1.1
- `android-4.2.2` - Android 4.2.2
- `android-gingerbread` - Android 2.3.7 (this branch)

```bash
git checkout android-gingerbread
git checkout android-4.2.2
git checkout main
```
