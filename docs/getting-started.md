# Getting started: build, install, and first boot

Target: **PCH-1000 (OLED), Android 5.1.1 (API 22), LinuxOnVita 6.12, no
SD2Vita**.

This guide starts from a console that already boots LinuxOnVita. It builds the
Android boot package, copies the Android images to `ur0:`, and brings the
system up headless so `adbd` can be reached over Wi-Fi. No Android boot has been
attempted yet on this unit; treat the first run as a bring-up experiment.

## 0. Before you start

Confirm:

- Firmware **3.65 with HENkaku + Enso**.
- **Settings -> HENkaku Settings -> Enable Unsafe Homebrew** is on.
- A full console backup exists, including `ux0:/linux/` and `ur0:`.
- `ur0:` has room for about **1.3 GB** of Android images (2.5 GB total, with
  300-500 MB kept free for VitaOS).
- You have a way to copy files to `ux0:` and `ur0:` (VitaShell over USB or FTP).
- You know your Wi-Fi SSID and password. Without Wi-Fi there is no adb path on
  the first boot; the only output is the framebuffer console.

Do not use the LinuxOnVita installer's reinstall/TRIANGLE path to boot Android:
it overwrites `ux0:/linux/zImage` and `vita.dtb`. Use **CROSS** to boot the files
already in `ux0:/linux/`.

## 1. Get the prebuilt Android images

The bring-up uses Google's generic Android 5.1.1 SDK image, not an AOSP build.
Install it with Android Studio's SDK Manager or the command-line tools:

```sh
sdkmanager "system-images;android-22;default;armeabi-v7a"
```

The image directory is:

```
$ANDROID_HOME/system-images/android-22/default/armeabi-v7a/
    system.img      ~750 MB, raw ext4, API 22, armeabi-v7a
    userdata.img    ~550 MB, raw ext4, empty
    ramdisk.img     gzip cpio, already vendored in prebuilt/
```

Verify `system.img` is a raw ext4 image:

```sh
file system.img
```

`userdata.img` is almost empty (only `/lost+found`); Android will populate it on
first boot. Do not shrink or convert either image for the first boot. Both fit
on ur0:

| Image | Size | Mode |
| --- | --- | --- |
| `system.img` | 750 MB | read-only loop |
| `userdata.img` | 550 MB | writable loop |
| `/cache` | 64 MB tmpfs | RAM, recreated every boot |
| **ur0 total** | **~1.3 GB** | leaves ~1.2 GB |

If `ur0:` free space is tight, see
[docs/ur0-storage-plan.md](ur0-storage-plan.md) for the shrink/fallback options.

## 2. Build the boot package

On a Linux host, WSL2, or Docker:

```sh
git clone <this repository>
cd androidonvita
bash scripts/build.sh
```

The script fetches the pinned LinuxOnVita kernel tree, builds the initramfs from
`prebuilt/ramdisk.img` plus `initramfs-overlay/`, adds ashmem and the Android
kernel fragment, and produces:

```
build/out/zImage
build/out/vita.dtb                     permissive SELinux
build/out/vita-selinux-disabled.dtb    fallback
build/out/vita1000.dtb                 unpatched reference
build/out/initramfs.cpio
build/out/SHA256SUMS
build/out/build-info.txt
```

You can also download the same files from the GitHub Actions
`android-on-vita-boot` artifact. The workflow builds and uploads the package on
every push to `main`; the first successful build is run #4 (commit `8fc952b`).
Its `SHA256SUMS` has been verified after download.

## 3. Copy files to the Vita

With VitaShell:

1. Back up the existing `ux0:/linux/` directory somewhere safe.
2. Copy `zImage` and `vita.dtb` from `build/out/` to `ux0:/linux/`.
   Keep LinuxOnVita's `baremetal-loader.skprx` and `payload.bin`. Do not copy
   `vita1000.dtb` over the working DTB; use `vita.dtb`.
3. Create `ur0:/android/` and copy `system.img` and `userdata.img` into it.
4. Optional Wi-Fi config: create `ur0:/android/wpa_supplicant.conf`.

A minimal `wpa_supplicant.conf`:

```
update_config=1
network={
    ssid="MyNetwork"
    psk="MyPassword"
}
```

`vita-net.sh` strips any `ctrl_interface=` line, points the control socket at
`/data/misc/wifi/sockets`, and tolerates CRLF line endings. The interface is
`mlan0` (fallback `wlan0`). Wi-Fi is optional for the first kernel/init test but
required for adb over TCP.

5. Optional UI marker: create an empty file `ur0:/android/ui` to auto-enable the
   framework. Leave it absent for the first headless boot.

## 4. Boot

1. Launch **LinuxOnVita** from LiveArea.
2. Press **CROSS** to boot the existing `ux0:/linux/zImage` + `vita.dtb`.
3. Watch the framebuffer console. Stage 1 logs start with `init.vita:`.

Expected sequence:

```
init.vita: starting
init.vita: mounting /dev/mmcblk0p12 on /mnt/ur0 (rw)
init.vita: attaching Android images
init.vita: zram0 swap enabled (128 MiB)
init.vita: handing off to /init
```

Android init then imports `/init.vita.rc`, mounts `/system`, `/data`, and
`/cache` from `fstab.vita`, starts the core services, and starts `adbd`. The
framework stays stopped.

If stage 1 fails, it drops to a busybox shell on the framebuffer console. Use
that shell to check `/dev/mmcblk*`, `losetup`, and the exact paths under
`/mnt/ur0/android/`. Reboot the Vita to retry.

## 5. Connect adb

Once Wi-Fi associates and DHCP succeeds, `vita-net.sh` writes the interface
address to `/data/misc/wifi/vita-net.log` and appends it to
`ur0:/android/vita-net.log`. On the PC:

```sh
adb connect <vita-ip>:5555
adb shell
```

`adbd` runs as root without authentication because this is a bring-up image
(`ro.secure=0`, `ro.adb.secure=0`). Do not expose port 5555 to an untrusted
network.

Useful checks:

```sh
adb shell getprop | head -n 40
adb shell cat /proc/cmdline
adb shell dmesg | tail -n 100
adb shell logcat -d
adb shell ls -l /dev/binder /dev/ashmem /dev/sw_sync /dev/graphics/fb0
adb shell ls -l /system /data /cache
adb shell mount
```

If adb is not reachable, check the framebuffer console for `vita-net` errors and
the DHCP address. If the console shows Android init starting but no network,
the Wi-Fi config or firmware path is the first thing to fix.

## 6. Enable Wi-Fi details and the UI

Wi-Fi runs as a `late_start` oneshot. To retry it from adb:

```sh
adb shell start vita-net
```

To see the supplicant log:

```sh
adb shell cat /data/misc/wifi/wpa_supplicant.log
adb shell cat /data/misc/wifi/vita-net.log
```

The framework is deliberately not started. When the core system is stable,
enable it with:

```sh
adb shell setprop vita.ui 1
```

or create `ur0:/android/ui` and reboot. This starts zygote and SurfaceFlinger.
Expect the display path to fail or show a black screen at first: the SDK image
has no HWC and no software GLES 2.0. Capture `logcat` before changing anything.

## 7. Fallback and recovery

- **SELinux policy load fails:** copy `vita-selinux-disabled.dtb` over
  `ux0:/linux/vita.dtb` and reboot.
- **Kernel panics or hangs:** restore the known-good LinuxOnVita
  `zImage`/`vita.dtb` pair and boot with **CROSS**.
- **Android init fails before adbd:** use the framebuffer console; stage 1 leaves
  a busybox shell on failure.
- **ur0 corruption:** restore the console backup. Keep clean shutdowns and avoid
  writes while experimenting.
- **Boot loop in VitaOS:** hold **L** during power-on to bypass taiHEN plugins,
  then fix `ur0:tai/config.txt` / `ux0:tai/config.txt` from VitaShell.

## 8. Boot arguments and artifacts

The patched `vita.dtb` uses:

```
earlyprintk keep_bootcon console=tty0 console=ttyS0,115200
androidboot.hardware=vita androidboot.selinux=permissive
androidboot.console=tty0 androidboot.serialno=PCH1000 rdinit=/init.vita
```

The fallback DTB changes only `androidboot.selinux=permissive` to `disabled`.

`build-info.txt` records the kernel commit, toolchain, artifact sizes, and both
bootarg strings. `SHA256SUMS` covers `zImage`, both patched DTBs, the unpatched
DTB, and `initramfs.cpio`.

## 9. What to report back

For the first boot, keep:

- the full framebuffer-console log (photo or serial capture);
- `dmesg` from adb, if it starts;
- `logcat -d` from the first Android boot;
- `adb shell getprop`;
- `adb shell mount`;
- `ur0:/android/vita-net.log`;
- the exact `SHA256SUMS` and `build-info.txt` used.

That is enough to separate kernel/config problems from Android userspace and
graphics problems.
