# ur0-only storage plan (no SD2Vita)

**Decision (2026-11-08):** use the PCH-1000's internal `ur0:` partition for the
Android images. No SD2Vita adapter. A full backup of the console has been taken,
and `ur0:` has about **2.5 GB free**.

`ur0:` is the internal NAND partition, exposed by Linux as `/dev/mmcblk0p12`
and mounted at `/mnt/ur0`. It is shared with VitaOS system data. LinuxOnVita
mounts it read-only by default; `/init.vita` remounts it read-write for the
Android boot. Fallback device names are `/dev/mmcblk1p12` and
`/dev/mmcblk2p12`.

## Capacity budget

The first boot uses the unmodified SDK image, so there is no shrinking step and
no cache image:

| Image | Size | Mode | Purpose |
| --- | --- | --- | --- |
| `system.img` | 750 MB | ro loop | Android 5.1.1 system, framework, SystemUI, launcher |
| `userdata.img` | 550 MB | rw loop | settings, app data, ART cache |
| `/cache` | 64 MB tmpfs | RAM | dex2oat / scratch, recreated every boot |
| `swap.img` | 512 MB | swapon loop | disk swap for 5.1.1 and later |
| **ur0 total** | **~1.8 GB** | | leaves **~0.7 GB** on a 2.5 GB partition |

Do not fill ur0. Keep at least **300-500 MB free** for VitaOS and exFAT metadata.
The images are sparse files on the host, but when copied to exFAT they occupy
their full logical size; check with `df -h /mnt/ur0` on the Vita side or with
VitaShell's free-space display.

The swap image is used by **5.1.1 and later only**. It is created on first boot
if missing and can be deleted to reclaim 512 MB; Android then boots with zram
only.

Android 6.0 (experimental branch) uses the same layout and file names, so only
one version can be installed at a time:

| Image | Size | Mode | Purpose |
| --- | --- | --- | --- |
| `system.img` | up to 900 MB after `resize2fs -M` | ro loop | Android 6.0 system; expect 500-600 MB |
| `userdata.img` | 640 MB | rw loop | 6.0 settings, app data, ART cache |
| `/cache` | 64 MB tmpfs | RAM | dex2oat / scratch |
| `swap.img` | 512 MB | swapon loop | same ur0 swap image |
| **ur0 total** | **~1.7-2.0 GB** | | leaves **~0.5-0.8 GB** on a 2.5 GB partition |

The download script fails if the shrunk 6.0 system image exceeds 900 MiB. Do
not install 5.1.1 and 6.0 at the same time; swap the two image files.

If free space is below ~1.5 GB, the tight budget is:

| Image | Size |
| --- | --- |
| `system.img` | 600 MB, after removing unused system apps |
| `userdata.img` | 384 MB |
| `/cache` | 64 MB tmpfs |
| `swap.img` | 512 MB (5.1.1+) |
| **ur0 total** | **~1.5 GB** |

Shrinking `system.img` requires resizing the ext4 filesystem and the file
itself, then verifying that Android still boots. Do that only after the
unmodified image has booted once.

## Layout

```
Sony card ux0 (VitaOS-visible)        ur0 (Linux-visible)
  /linux/baremetal-loader.skprx         /android/system.img
  /linux/payload.bin                    /android/userdata.img
  /linux/zImage                         /android/swap.img              (5.1.1+)
  /linux/vita.dtb                       /android/wpa_supplicant.conf   (optional)
                                        /android/ui                    (optional)
                                        /android/vita-net.log          (written by Android)
```

The loader reads the boot files from the Sony card and stages them in DRAM.
Linux then mounts ur0 and loop-mounts the Android images. The official Sony card
cannot be mounted by Linux: it uses Sony's MSIF protocol and there is no Linux
driver.

## Boot wrapper

`initramfs-overlay/init.vita` is the stage-1 init (`rdinit=/init.vita`):

1. Mount `devtmpfs`, `proc`, and `sys`.
2. Find `/dev/mmcblk{0,1,2}p12`, mount it at `/mnt/ur0` with `rw,noatime`
   (exFAT, then vfat, then auto-detect as fallbacks).
3. Verify `/mnt/ur0/android/system.img` and `userdata.img` exist.
4. `losetup -r /dev/loop0 /mnt/ur0/android/system.img`
5. `losetup /dev/loop1 /mnt/ur0/android/userdata.img`
6. Enable 128 MiB zram swap, then attach `swap.img` to `/dev/loop2`,
   `mkswap` it, and `swapon` it. Set `vm.swappiness=60`.
7. `exec /init`.

Android's `init.vita.rc` then runs `mount_all /fstab.vita`, which mounts:

```fstab
/dev/block/loop0  /system  ext4  ro,barrier=1                              wait
/dev/block/loop1  /data    ext4  noatime,nosuid,nodev,barrier=1,noauto_da_alloc  wait
tmpfs             /cache   tmpfs rw,nosuid,nodev,noatime,mode=0755,size=64m      defaults
```

The `wait` flag handles the loop-device/ueventd timing. Do not add the `check`
flag: the Android ramdisk has no `e2fsck`, and a first boot should not depend on
one.

## Reclaiming space on ur0

`alpine.img` is the optional Alpine desktop chroot, not the Linux base. If it is
on ur0 and you do not need it, move it to the Sony card with VitaShell or delete
it:

```sh
vita-storage rw ur0
rm /mnt/ur0/alpine.img
vita-storage ro ur0
```

It can be recreated later with `alpine-chroot create`.

## Swap on ur0 (5.1.1+)

`/init.vita` creates `/mnt/ur0/android/swap.img` (512 MiB) on first boot and
attaches it to `/dev/loop2`; `system.img` and `userdata.img` keep loop0 and
loop1. The file is reused on later boots. If ur0 is full or `swapon` fails,
boot continues with the 128 MiB zram device only.

To reclaim the space, delete the image and reboot; it is recreated only when
the boot script finds it missing:

```sh
vita-storage rw ur0
rm /mnt/ur0/android/swap.img
vita-storage ro ur0
```

Keep ZRAM. It lives in RAM and does not consume ur0. Do not add a second
swapfile on `ux0:`; the Sony card is not visible to Linux.

## Risks

- **ur0 is VitaOS system storage.** A crash or power loss during a write can
  corrupt exFAT and force a VitaOS database rebuild or a restore.
- **exFAT is not journaled.** Use clean shutdowns, keep the backup current, and
  avoid running the Android UI while the battery is low.
- **NAND wear.** Android 5.1.1 and later deliberately place a fixed 512 MiB
  swap image on ur0. That is an accepted tradeoff for surviving memory
  pressure on a 512 MB device; do not grow it or add more swap files. Keep
  userdata small and keep logs off ur0 where possible.
- **512 MB RAM is the binding constraint.** Low-RAM Android, no GApps, minimal
  apps. Expect a slow UI and possible OOM kills even with zram and swap.
- **Loop devices are not partitions.** Tools that expect a real block device
  (some `vold` paths, `fsck`, OTA) will not work unmodified.

## Fallbacks

1. **Android 4.4.4** - smaller system image and Dalvik instead of ART; lighter
   on 512 MB RAM.
2. **Shrink the images** - remove unused system apps and resize `system.img`;
   reduce `userdata.img` to 384 MB. Verify ext4 features remain 6.12-compatible.
3. **tmpfs `/data`** - ephemeral but avoids ur0 writes; costs RAM and is only a
   diagnostic path.
4. **SD2Vita** - not part of this project's target, but still the practical
   path if ur0 proves too small or too risky for daily use. LinuxOnVita already
   supports it.

## Next steps

1. Build the boot package (`bash scripts/build.sh`) and copy the artifacts to
   `ux0:/linux/`.
2. Copy the unmodified `system.img` and `userdata.img` to `ur0:/android/`.
3. Boot headless and reach `adbd` over Wi-Fi.
4. Measure free space, boot time, and memory after the first Android boot.
5. Only then decide whether to shrink images, add a cache image, or change the
   storage layout.
