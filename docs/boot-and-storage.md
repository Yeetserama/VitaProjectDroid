# Boot, kernel, and storage plan

This document describes the design that the scripts and the initramfs overlay
actually implement. It is not a generic Android-on-ARM guide.

## 1. Boot chain

The Vita bootloader stays locked. We reuse LinuxOnVita's soft boot path:

```
VitaOS + Enso
  -> taiHEN plugin: baremetal-loader.skprx
     -> stages payload.bin, zImage, vita.dtb in RAM
     -> reboots the SoC into bare metal
        -> payload.bin (vita-baremetal-linux-loader)
           -> loads zImage and the DTB
           -> jumps to the Linux zImage
              -> Linux 6.12 + embedded initramfs
                 -> rdinit=/init.vita (busybox stage 1)
                    -> mounts ur0, sets up loop devices, zram
                    -> exec /init (Android 5.1.1 init)
```

This is not a bootloader unlock and it does not erase VitaOS. Rebooting the
console returns to VitaOS. The LinuxOnVita VPK installs and launches this chain;
for Android we replace `zImage` and `vita.dtb`, embed a different initramfs, and
add the Android images.

Use the VPK's **CROSS** button to boot the existing `ux0:/linux/` files. The
installer's reinstall/TRIANGLE path overwrites `zImage` and `vita.dtb` with
LinuxOnVita's desktop kernel.

## 2. Files on storage: two different roles

**Boot files (read by VitaOS, before Linux starts):**

```
ux0:/linux/
    baremetal-loader.skprx    from LinuxOnVita
    payload.bin               from LinuxOnVita
    zImage                    our Android-enabled kernel
    vita.dtb                  our patched DTB (bootargs + androidboot.*)
```

The taiHEN loader runs under VitaOS, so it can read the official Sony memory
card. That card is the right place for the boot files: the loader only needs to
read them before the bare-metal handoff.

**Android images (read by Linux, after the handoff):**

```
ur0:/android/
    system.img                ext4, read-only
    userdata.img              ext4, writable
    wpa_supplicant.conf       optional Wi-Fi config
    ui                        optional marker: enable the framework
    boot.log                  kernel ring-buffer mirror from /init.vita
    vita-net.log              written by vita-net.sh for headless debugging
    wpa_supplicant.log        copy of the glibc supplicant log
```

`ur0:` is `/dev/mmcblk0p12` (fallbacks `/dev/mmcblk1p12` and
`/dev/mmcblk2p12`), an exFAT partition shared with VitaOS. `/init.vita` mounts
it read-write and attaches the two images to `/dev/loop0` and `/dev/loop1`.
`/cache` is a 64 MB tmpfs mounted by the Android fstab; there is no `cache.img`.

Do **not** put the images on the official Sony memory card. The loader can read
it, but Linux cannot: the card uses Sony's proprietary MSIF protocol and there
is no Linux driver. Once the SoC reboots into bare metal, VitaOS and its MSIF
driver are gone. This is not a mount option or a missing package.

The working split is:

```
Sony memory card (ux0, VitaOS-visible)     ur0 (Linux-visible)
  /linux/zImage                              /android/system.img
  /linux/vita.dtb                            /android/userdata.img
  /linux/payload.bin                         /android/wpa_supplicant.conf
  /linux/baremetal-loader.skprx              /android/ui (optional)
```

Linux mounts ur0 directly, so no StorageMgr/YAMT plugin or SD2Vita adapter is
needed. For the capacity budget and risks, see
[ur0-storage-plan.md](ur0-storage-plan.md).

## 3. Kernel changes for Android

The kernel is the pinned `incognitojam/linux_vita` 6.12 tree plus:

- `kernel/ashmem/` - ashmem backported from Linux 4.19 and adapted to 6.12
  (`shrinker_alloc`/`shrinker_register`, `vfs_iter_read`, `shmem_file_setup`).
- `scripts/patch-kernel-tree.py` - wires ashmem into
  `drivers/misc/Kconfig` and `drivers/misc/Makefile`, and adds the Vita DTBs to
  `arch/arm/boot/dts/Makefile`.
- `kernel/config/vita-android.fragment` - merged over
  `arch/arm/configs/vita_defconfig` with `merge_config.sh`.

The fragment covers:

- binder (binder/hwbinder/vndbinder), ashmem, dma-buf, sync file, sw_sync;
- cgroups with the legacy v1 memory controller (`CONFIG_MEMCG_V1=y`), which
  Android 5.1 init expects;
- ext4, exFAT, vfat, FUSE, tmpfs xattrs/ACLs, devtmpfs;
- loop devices, zram with lz4, zsmalloc;
- SELinux with `selinux` in `CONFIG_LSM`, permissive boot parameters;
- IPv6, netfilter/NAT, xt_* matches for netd, `dummy`/`tun`;
- mwifiex + SD8787, cfg80211/mac80211, unsigned regulatory DB;
- input evdev/uinput, IKCONFIG, and initramfs conveniences.

`CONFIG_MODULES` is off in the Vita defconfig. Everything must be built in; there
is no `insmod` escape hatch.

### 3.1 ashmem

Linux 6.12 no longer contains `drivers/staging/android/ashmem.c`. Android 5.1
still expects `/dev/ashmem`. The project keeps a 4.19-derived driver under
`kernel/ashmem/` and copies it into the kernel tree at build time. This is the
first compile risk in CI; the APIs used were checked against 6.12 but the driver
has not been compiled yet.

Alternative: patch bionic/libcutils to use `memfd`. Android 10+ did this, but
backporting it to 5.1 touches many callers and is more invasive. Do the driver
first.

### 3.2 binder and sync

Binder is already in mainline and works on 32-bit ARM. `CONFIG_SW_SYNC` gives
Android's `libsync` the `/dev/sw_sync` device it expects. SurfaceFlinger uses
fences even with a CPU composer, so `SYNC_FILE` and `SW_SYNC` stay enabled.

### 3.3 SELinux

The prebuilt Android 5.1.1 `sepolicy` header uses policy version **26**
(magic `0xf97cff8c`, `"SE Linux"`). Linux 6.12 supports SELinux policy versions
15-33, so the format is compatible. Loading the policy on the Vita is still
unverified.

Boot parameters:

```
androidboot.selinux=permissive
```

The build produces a second DTB with `androidboot.selinux=disabled` as a
fallback. Do not make SELinux enforcing before the port works.

## 4. Android ramdisk and stage-1 init

### 4.1 Initramfs contents

The kernel embeds a single raw newc cpio built by
`scripts/build-initramfs.sh`:

1. Unpack `prebuilt/ramdisk.img` (stock Android 5.1.1 ARM ramdisk).
2. Remove goldfish/ranchu files.
3. Add `initramfs-overlay/`: `/init.vita`, `/init.vita.rc`, `/fstab.vita`,
   `/ueventd.vita.rc`, `/default.prop`, `/vita-net.sh`, `/vita-ui.sh`, and the
   input keylayout/IDC files.
4. Add static ARM busybox as `/sbin/busybox` and `/bin/sh`.
5. Add the glibc Wi-Fi userspace and firmware from `vendor/vita-linux/` under
   `/vita-linux/` and `/lib/firmware/`.

Every cpio entry is written with uid/gid 0. The kernel is configured with
`CONFIG_INITRAMFS_SOURCE="vita-initramfs.cpio"`; because the name ends in
`.cpio`, `usr/Makefile` treats the file as a direct archive and does not try to
run `gen_init_cpio` on it.

### 4.2 `/init.vita`

The kernel's `rdinit=/init.vita` makes the busybox script PID 1. It:

1. mounts devtmpfs, proc, and sysfs;
2. finds `mmcblk{0,1,2}p12` and mounts it at `/mnt/ur0` with `rw,noatime`;
3. verifies `system.img` and `userdata.img`;
4. attaches them to `/dev/loop0` (read-only) and `/dev/loop1` (writable);
5. enables 128 MiB zram swap and sets `vm.swappiness=100`;
6. `exec /init`.

Android's init mounts a fresh tmpfs over `/dev` and mounts proc/sys itself
later. The loop associations and the `/mnt/ur0` mount survive.

If stage 1 fails, it logs the reason to `/dev/kmsg` and the console and drops to
a busybox shell. That is deliberate: without USB or a working adbd there is no
other debug channel.

### 4.3 fstab

`/fstab.vita` is mounted by Android init from the `on fs` action:

```fstab
/dev/block/loop0  /system  ext4  ro,barrier=1                              wait
/dev/block/loop1  /data    ext4  noatime,nosuid,nodev,barrier=1,noauto_da_alloc  wait
tmpfs             /cache   tmpfs rw,nosuid,nodev,noatime,mode=0755,size=64m      defaults
```

The `wait` flag waits for the loop block device that ueventd creates. Do not add
`check`: the Android ramdisk has no `e2fsck`.

### 4.4 Android init additions

`init.rc` imports `/init.${ro.hardware}.rc`; `androidboot.hardware=vita` selects
`/init.vita.rc`. It:

- mounts debugfs in `on init`;
- runs `mount_all /fstab.vita` in `on fs`;
- creates `/data/system` as `0771 system:system` and the
  `/data/system/devices/{keylayout,idc}` directories before system_server would;
- symlinks the input config files from the initramfs into
  `/data/system/devices/...`;
- in `on boot`, stops `zygote`, `surfaceflinger`, `media`, `drm`, and
  `ril-daemon`, and starts `adbd`;
- makes `/dev/graphics/fb0` writable by the `graphics` group;
- starts `vita-ui` and `vita-net` as `late_start` oneshots.

`class_start late_start` runs from the `nonencrypted` action after `on boot`, so
the Wi-Fi script sees a mounted `/data` and `/system`.

Headless gating uses properties that the stock `property_contexts` accepts
through its `default_prop` catch-all:

```sh
adb shell setprop vita.ui 1
```

or the presence of `ur0:/android/ui` before boot. The `on property:vita.ui=1`
action starts zygote and SurfaceFlinger. `class_start` skips services marked
`SVC_DISABLED`, and `stop` sets that flag even for a service that has not
started, so the framework really stays down until requested.

### 4.5 adbd

The stock ramdisk has `/sbin/adbd` in class `core`, disabled. `default.prop` sets:

```
ro.secure=0
ro.debuggable=1
ro.adb.secure=0
service.adb.tcp.port=5555
```

`init.vita.rc` runs `start adbd` in `on boot`. adbd listens on TCP without
system_server, so it is the primary debug channel. There is no USB gadget
support in the Vita kernel config.

## 5. Display path

The display is initialized by the bare-metal loader and exposed as a
`simple-framebuffer` child of `/chosen`:

```
framebuffer@20000000 {
    compatible = "simple-framebuffer";
    reg = <0x20000000 (960*544*4)>;
    width = <960>;
    height = <544>;
    stride = <(960*4)>;
    format = "a8b8g8r8";
};
```

Linux 6.12's `drivers/of/platform.c` explicitly creates a platform device from
`of_get_compatible_child(of_chosen, "simple-framebuffer")`, so the node works
without a separate display driver.

`ueventd` maps the kernel `graphics` class to `/dev/graphics/fb0`, not
`/dev/fb0`. `init.vita.rc` fixes the permissions. This is a single buffer with
no page flip, no vsync, and no DRM/KMS. The prebuilt image has only
`gralloc.default`/`gralloc.goldfish`/`gralloc.ranchu` and no `hwcomposer.*`;
there is no guest-side software GLES 2.0 library.

The display work is therefore:

1. a `hwcomposer.vita` (or an fbdev presenter) that copies the client target
   into `/dev/graphics/fb0`;
2. a software GLES 2.0 path (Mesa softpipe/llvmpipe, SwiftShader 2.x, or a
   custom 2D-only composition path) before SurfaceFlinger can start.

Keep `console=tty0` and `CONFIG_FRAMEBUFFER_CONSOLE` during bring-up. Once the
UI works, move the console to `ttyS0,115200` only, or disable fbcon, so kernel
logs do not draw over the UI.

## 6. Input

- Touch: `vita-syscon-ts` reports MT protocol B positions in 0..1920 x 0..1080.
  Android's InputReader scales them to the 960x544 logical display. The IDC file
  marks it as a touchscreen and `device.internal=1`.
- Buttons: `vita-buttons` reports the Linux key codes used by the keylayout:
  D-pad 544-547, A/B/X/Y 304/305/307/308, L1/R1 310/311, Select/Start/Mode
  314/315/316, Volume 115/114, Power 116.
- Analog sticks and the rear touchpad are not mapped yet. A `uinput` daemon can
  translate them later; `CONFIG_INPUT_UINPUT` is enabled.

EventHub looks first in `$ANDROID_ROOT/usr/...` and then in
`$ANDROID_DATA/system/devices/...`. `/system` is a read-only loop mount, so the
initramfs copies live under `/usr/...` and are symlinked into
`/data/system/devices/...` by `init.vita.rc`.

## 7. Wi-Fi

- Kernel: `mwifiex` creates `mlan0`; firmware
  `mrvl/sd8787_uapsta.bin` and `regulatory.db` are in the initramfs under
  `/lib/firmware`.
- Userspace: the prebuilt bionic image has no usable supplicant integration for
  this kernel, so the initramfs carries LinuxOnVita 1.5.0's glibc
  `wpa_supplicant` and its dependency closure under `/vita-linux/`.
- `vita-net.sh` runs as a `late_start` oneshot:
  - creates `/data/misc/dhcp` and `/data/misc/dhcpcd` (`1014:1014`, mode 0770)
    before `dhcpcd` drops to `AID_DHCP`;
  - waits for `mlan0`/`wlan0`;
  - rewrites `ur0:/android/wpa_supplicant.conf` to put the control socket under
    `/data/misc/wifi/sockets`;
  - launches the supplicant through `/vita-linux/lib/ld-linux-armhf.so.3` and
    waits for its control socket;
  - retries Android's `dhcpcd` until an IPv4 address appears;
  - writes `/data/misc/wifi/vita-net.log`, mirrors it to
    `ur0:/android/vita-net.log`, and copies the supplicant log to
    `ur0:/android/wpa_supplicant.log`.
- `netd` expects the out-of-tree `xt_qtaguid` module. Expect errors; adbd and
  basic networking should still work. Framework-level Wi-Fi settings are a later
  problem.

## 8. Debugging

- **Framebuffer console:** `console=tty0` plus fbcon. This is the only early
  output without soldering. It is overwritten if the Android UI starts.
- **UART0:** `console=ttyS0,115200 earlyprintk keep_bootcon`. Requires soldering
  to the test points; LinuxOnVita documents the pins.
- **ADB over TCP:** `adb connect <vita-ip>:5555`, then `adb shell`, `logcat -d`,
  `dmesg`. Requires Android init to reach `on boot` and Wi-Fi to associate.
- **Stage-1 shell:** on failure, `/init.vita` drops to busybox on the framebuffer
  console. Check `/dev/mmcblk*`, `losetup`, `dmesg`, and
  `/mnt/ur0/android/`.
- **Boot loop recovery:** hold **L** while powering on to bypass taiHEN plugins
  and return to LiveArea. Restore the known-good LinuxOnVita kernel/DTB pair.

## 9. Build and install sequence

1. Run `bash scripts/build.sh` on Linux/WSL2/Docker, or download the CI
   artifact.
2. Back up `ux0:/linux/`.
3. Copy `zImage` and `vita.dtb` to `ux0:/linux/`. Keep the LinuxOnVita
   `payload.bin` and `baremetal-loader.skprx`.
4. Copy `system.img` and `userdata.img` to `ur0:/android/`.
5. Optionally copy `wpa_supplicant.conf` to `ur0:/android/`.
6. Launch LinuxOnVita, press **CROSS**, and watch the framebuffer console.
7. Connect adb, capture `dmesg`/`logcat`, and fix the first failure.
8. Only after headless Android is stable, try `vita.ui=1` and attack the display
   and GLES path.

Keep a known-good LinuxOnVita `zImage`/`vita.dtb` pair on the card so you can
switch back and confirm the loader still works.

## 10. Known unknowns

- The kernel package has never been compiled; ashmem's 6.12 adaptation is the
  first likely build failure.
- The Android `sepolicy` has not been loaded on the Vita.
- `adbd`, `netd`, and the glibc supplicant have not run on hardware.
- The single-buffer `simple-framebuffer` may reject page-flip ioctls.
- No software GLES 2.0 implementation exists in the SDK image.
- `/data` is a 550 MB image; the first ART boot and dex2oat may fill it faster
  than expected.
