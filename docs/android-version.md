# Android version

## 1. The short answer

**Primary target: Android 5.1.1 (Lollipop, API 22), 32-bit ARMv7.**

Bring-up uses the prebuilt SDK image
`system-images;android-22;default;armeabi-v7a` (build LMY48X), not an AOSP
build. **Fallback if 5.1 is too slow: Android 4.4.4 (KitKat, API 19).**

Do not target Android 6.0 or newer. The Vita has 512 MB of RAM, a 32-bit ARMv7
CPU, no GPU driver, and no modern HAL stack. The version number is not what
limits us; the hardware and the missing graphics driver are.

## 2. Version comparison

| Version | API | RAM reality | Kernel work on Linux 6.12 | Graphics | Verdict |
| --- | --- | --- | --- | --- | --- |
| 4.4.4 | 19 | Designed for 512 MB ("Project Svelte") | binder + ashmem + logger + lowmemorykiller | software GLES 2.0 | Best performance, oldest apps; fallback |
| 5.1.1 | 22 | 512 MB minimum on paper, 1 GB realistic | binder + ashmem | software GLES 2.0 | **Recommended**: newest practical |
| 6.0 | 23 | 1 GB realistic | binder + ashmem | software GLES | Too heavy |
| 7.1.2 | 25 | 1 GB+ | binder + ashmem | software GLES | Too heavy |
| 8.1 Go | 27 | 1 GB, Treble/HIDL | binder + ashmem, HIDL | software GLES | Not realistic |
| 9+ | 28+ | 2 GB+ | Treble, APEX, etc. | software | No |

Notes:

- Android 5.1 uses **logd** and **lmkd** in userspace, so the removed kernel
  `logger` and `lowmemorykiller` drivers are not needed. Only `binder` and
  `ashmem` are missing from Linux 6.12.
- Android 4.4 uses the kernel `logger` and `lowmemorykiller` staging drivers,
  which no longer exist in Linux 6.12. They can be backported, but it is extra
  work.
- Android 5.1 is the last version that is realistically viable on 512 MB and
  still has a large base of period-correct APKs.
- Android 6/7 would boot but leave too little RAM for apps. Android 8+ adds
  Treble/HIDL and expects 1 GB+; it is not a sensible target for this port.

## 3. Why not a modern Android?

- **RAM:** 512 MB is below the practical minimum for Android 8+ with Google
  services. Even Android 5.1 needs low-RAM tuning and zram.
- **CPU:** 4x Cortex-A9 at 444 MHz is slower than a low-end 2015 phone. ART
  compilation and modern app runtimes will be painful.
- **GPU:** no Linux driver for the SGX543MP4+. Modern Android assumes hardware
  acceleration and Vulkan for many features.
- **HALs:** Android 8+ requires Treble/HIDL (or AIDL in newer versions), which
  means writing and maintaining many more interfaces.
- **64-bit:** the Vita is 32-bit ARMv7 only. Some newer Android components and
  apps assume 64-bit.
- **Security updates:** old AOSP is unpatched. Do not put personal accounts on
  this device.

## 4. Bring-up configuration: prebuilt SDK image

The first boot deliberately avoids an AOSP build. The SDK image provides a
known-good generic ARMv7 userspace:

```
$ANDROID_HOME/system-images/android-22/default/armeabi-v7a/
    system.img      750 MB, raw ext4, API 22, armeabi-v7a
    userdata.img    550 MB, raw ext4, empty
    ramdisk.img     gzip cpio, vendored in prebuilt/ramdisk.img
```

What it contains that matters:

- `ro.product.device=generic`, `ro.product.board=` empty,
  `ro.board.platform=` empty. With `androidboot.hardware=vita`, Android sets
  `ro.hardware=vita`; `hw_get_module` falls back to `gralloc.default`.
- `gralloc.default`, `gralloc.goldfish`, and `gralloc.ranchu`; there is **no**
  `hwcomposer.*` module.
- `libGLES_android.so`, a software GLES 1.1 renderer. There is **no** guest-side
  software GLES 2.0 library.
- The standard 5.1.1 ramdisk (`init`, `sbin/adbd`, `sbin/healthd`,
  `sbin/ueventd`, `sepolicy`, `property_contexts`, and the service definitions).

The system image is mounted read-only from `/dev/loop0`; `userdata.img` is
mounted read-write from `/dev/loop1`; `/cache` is a 64 MB tmpfs. The unmodified
sizes fit ur0 with about 1.2 GB left. See
[ur0-storage-plan.md](ur0-storage-plan.md).

The generic image is not tuned for the Vita. Expect:

- no low-RAM properties by default;
- no HWC, so the display path needs work;
- no software GLES 2.0; the only renderer is `libGLES_android.so`
  (PixelFlinger, GLES 1.x), which SurfaceFlinger can use through the emulator
  fallback described below;
- generic dhcpcd/netd assumptions, including the missing `xt_qtaguid`;
- a generic `build.prop` on a read-only filesystem.

For the first headless boot, none of those block `adbd`. They become the next
phases after Android init and networking are proven.

### Software renderer fallback

SurfaceFlinger does not need a GLES 2.0 driver to present frames. If the
emulator properties are set before `libEGL` loads, it selects the legacy
software path and uses `GLES10RenderEngine` with
`/system/lib/egl/libGLES_android.so`:

```properties
ro.kernel.qemu=1
ro.kernel.qemu.gles=0
ro.sf.lcd_density=240
```

Verified on hardware: SurfaceFlinger reports `Android PixelFlinger 1.4`,
`OpenGL ES-CM 1.0`, and `GL_MAX_TEXTURE_SIZE = 4096`, and it draws the 5.1 boot
animation on the 960x544 panel. `libGLES_android.so` supplies EGL and GLES 1.x
only, so GLES 2.0 apps and HWUI still cannot run. The `Launcher2` home screen is
2D and remains the v0.2 target.

The two `ro.kernel.qemu*` properties must be present before the process that
loads `libEGL` starts. The v0.2 boot kit now ships them in `default.prop`.
`adb shell setprop` still works once for experiments while they are unset
(root); after that they are read-only until reboot, which made the runtime
diagnosis above possible.

### Framework boot blocker

With the renderer working, SurfaceFlinger and the boot animation run, but zygote
originally exited during `preloadResources()`, before `system_server` started.
The failure was an `ExceptionInInitializerError` with no Java stack trace in
logcat. An on-device `app_process` probe reproduced it:

```
java.lang.NullPointerException: Attempt to invoke virtual method
'char[] java.lang.String.toCharArray()' on a null object reference
    at java.io.File.fixSlashes(File.java:185)
    at java.io.File.<init>(File.java:134)
    at android.os.Environment.getLegacyExternalStorageDirectory(Environment.java:397)
    at android.os.Debug.<clinit>(Debug.java:96)
```

`Debug.<clinit>` builds its default trace path from
`Environment.getLegacyExternalStorageDirectory()`, which is
`new File(System.getenv("EXTERNAL_STORAGE"))` with no null check. The SDK
ramdisk's `init.environ.rc` never exports `EXTERNAL_STORAGE`; stock devices do it
in their `init.<hardware>.rc`. `ZygoteInit.preloadResources()` calls
`Debug.startAllocCounting()` before anything else, so the class initializer runs
immediately and zygote dies. This also explains why the full, truncated, and
empty `preloaded-classes` lists all failed identically.

`init.vita.rc` now exports `EXTERNAL_STORAGE=/storage/sdcard0` in `on init`,
matching a device with physical external storage. Running the probe with that
variable set makes the complete `preloadResources()` prologue succeed.

### Low-RAM properties

If needed, these can be applied through the initramfs `default.prop` where the
property is not `ro.`-locked, or by remounting `/system` read-write and editing
`build.prop` for experiments:

```properties
ro.config.low_ram=true
ro.config.max_starting_bg=4
ro.sf.lcd_density=240
dalvik.vm.heapstartsize=8m
dalvik.vm.heapgrowthlimit=96m
dalvik.vm.heapsize=192m
dalvik.vm.dex2oat-filter=interpret-only
```

`interpret-only` reduces first-boot compilation time and disk use, at the cost
of slower app execution. Do not change these until the first boot shows actual
memory or dex2oat pressure.

### What to leave out

- Google Apps, Play Store, Play Services (already absent from the SDK image)
- Bluetooth, NFC, GPS, camera, telephony (no drivers)
- Multiuser, encrypted storage
- Live Wallpapers, heavy launcher, heavy widgets
- Unnecessary system apps and wallpapers

AOSP Launcher3 is heavier than it looks on 512 MB. A minimal launcher is a later
UI decision.

## 5. App compatibility expectations

- **32-bit ARM only.** No ARM64 apps, no 64-bit-only apps.
- **No Play Store.** Sideload APKs or use an old F-Droid client.
- **Old WebView.** Android 5.1's AOSP WebView is ancient; modern sites will
  break. Without Google Play, updating WebView is difficult.
- **No Google services.** Apps that depend on Play Services will not work.
- **No Vulkan, no modern GLES extensions.** Software GLES 2.0 is the ceiling.
- **No audio/camera/GPS/Bluetooth** until drivers exist.
- **Modern apps** often require API 23+ or 26+ and will refuse to install.
- **Period-correct apps** from 2012-2016 have the best chance.

This is a retro Android device. Treat it like a 2013 phone with a great screen
and a gamepad, not like a modern tablet.

## 6. If you later build AOSP

A custom AOSP 5.1.1 build is optional and should wait until the prebuilt image
has proven the kernel/userspace handoff. It would allow:

- a real `device/sony/vita` product with low-RAM defaults;
- a smaller system image;
- a built-in HWC/GLES path;
- control over SELinux policy and properties.

AOSP 5.1 is old and expects an old host:

- Ubuntu 14.04 or 16.04 (use Docker; modern distros break the build)
- OpenJDK 7
- GCC 4.8 host toolchain (AOSP prebuilts include the cross compiler)
- 16 GB RAM recommended, 8 GB minimum
- 150 GB free disk
- Python 2.7 for the build system

On Windows, use WSL2 + Docker Desktop or a Linux VM. Do not try to build AOSP
5.1 natively on Windows.

A minimal `device/sony/vita` sketch:

```makefile
TARGET_ARCH := arm
TARGET_ARCH_VARIANT := armv7-a-neon
TARGET_CPU_ABI := armeabi-v7a
TARGET_CPU_ABI2 := armeabi
TARGET_CPU_SMP := true
TARGET_BOARD_PLATFORM := vita
TARGET_BOOTLOADER_BOARD_NAME := vita
TARGET_NO_BOOTLOADER := true
TARGET_NO_KERNEL := true          # the Vita loader boots our zImage directly
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USES_LOGD := true          # 5.1
```

Do not add the `vita` device until a generic `aosp_arm-eng` build works.

## 7. Decision

Lock the first target to **Android 5.1.1, prebuilt SDK image, API 22**. Keep
Android 4.4.4 as a documented fallback, not a parallel target. Supporting two
Android versions at once would double the HAL work and delay the first boot.
