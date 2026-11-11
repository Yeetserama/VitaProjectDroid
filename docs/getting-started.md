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
   Also copy `vita-selinux-disabled.dtb` there under that name, so the SELinux
   fallback can be applied from VitaShell without a PC.
3. Create `ur0:/android/` and copy `system.img` and `userdata.img` into it.
4. Optional Wi-Fi config: create `ur0:/android/wpa_supplicant.conf`.

A minimal `wpa_supplicant.conf`:

```
update_config=1
ctrl_interface=/var/run/wpa_supplicant
network={
    ssid="MyNetwork"
    psk="MyPassword"
    key_mgmt=WPA-PSK
}
```

Add `scan_ssid=1` inside the `network={...}` block for a hidden SSID. The file
must be plain text; CRLF from a Windows editor is tolerated.

`vita-net.sh` rewrites this file for Android on every boot. It strips any
`ctrl_interface=` line, points the control socket at
`/data/misc/wifi/sockets`, and adds `disable_scan_offload=1` so
`wpa_supplicant` performs normal scans instead of the driver's scheduled scan.
The first hardware boot showed mwifiex starting `sched_scan` and never
returning results, so the supplicant never associated. If the config has no
`country=` line, the script adds `country=IE`; put an explicit `country=XX`
line in `wpa_supplicant.conf` to override that default.

The script starts one supplicant with `-d -t` debug logging, waits for the
carrier, then starts one `dhcpcd` instance. If normal scanning still does not
associate, it retries once with `ap_scan=2`, which bypasses scanning entirely.
It mirrors the last 400 supplicant log lines to
`ur0:/android/wpa_supplicant.log` every 10 seconds. The interface is `mlan0`
(fallback `wlan0`). Wi-Fi is optional for the first kernel/init test but
required for adb over TCP.

The v5 kernel disables the Marvell Bluetooth SDIO transport and cfg80211
power save. `btmrvl_sdio` shares the SD8787 card and firmware with `mwifiex`;
on the v3 boot its probe waited about 100 seconds for a Bluetooth image,
failed, and reset the card, dropping Wi-Fi after roughly 150 seconds.
Bluetooth is not used by the headless image, so the transport stays off until
there is a reason to debug it. Power save is off because the v4 boot had a
valid DHCP lease (broadcast) but no ping replies and no adb connection; the
SD8787 firmware can sleep between beacons and drop unicast frames. The script
also runs a watchdog after the first lease: if the interface disappears it
waits for `mwifiex` to re-probe and restarts the supplicant, and if only the
carrier drops it restarts after 30 seconds. Recovery attempts and a five-minute
heartbeat go to `vita-net.log`, and the log of the failed phase is kept as
`ur0:/android/wpa_supplicant.crash.log`.

v5 also checks adb itself. After every DHCP lease and at each watchdog
heartbeat it reads `/proc/net/tcp` for a `LISTEN` socket on the adb port. If
the socket is missing it sets `service.adb.tcp.port` and restarts adbd once.
The log then contains the exact `adb connect <ip>:5555` command, plus an
`--- adb ---` / `adb check` block with the interface address, a ping to the
DHCP gateway, the ARP cache, the route table, and the `INPUT` firewall rules.
That distinguishes "adbd never listened" from "adbd listens but the network
drops the connection", and the gateway ping separates "the Vita cannot receive
unicast at all" from "the phone allows gateway traffic but isolates clients".

`ur0:/android/vita-net.sh`, `vita-log.sh`, and `vita-ui.sh` override the
embedded copies when they start with `#!`. Copy a new script there and reboot
to test a Wi-Fi or logging change without rebuilding `zImage`. Delete the ur0
copy to return to the embedded version; the boot log says which copy was used.

5. Optional UI marker: create an empty file `ur0:/android/ui` to auto-enable the
   framework. Leave it absent for the first headless boot.

### First-boot checklist

Before booting, `ur0:/android/` should contain:

```
system.img              ~750 MB
userdata.img            ~550 MB
wpa_supplicant.conf     optional, needed for adb over TCP
ui                      optional, absent for headless boot
```

`ux0:/linux/` should contain the new `zImage`, `vita.dtb`, and
`vita-selinux-disabled.dtb` alongside LinuxOnVita's `baremetal-loader.skprx` and
`payload.bin`. `boot.log` and the other `*.log` files appear on ur0 during and
after the boot; they are outputs, not inputs.

## 4. Boot

1. Launch **LinuxOnVita** from LiveArea.
2. Press **CROSS** to boot the existing `ux0:/linux/zImage` + `vita.dtb`.
3. Watch the framebuffer console. Stage 1 logs start with `init.vita:`.

Expected sequence:

```
init.vita: starting
init.vita: mounting /dev/mmcblk0p12 on /mnt/ur0 (rw)
init.vita: initial kernel log saved (ur0:/android/boot.log); vita-log takes over
init.vita: attaching Android images
init.vita: zram0 swap enabled (128 MiB)
init.vita: handing off to /init
```

Android init then imports `/init.vita.rc`, mounts `/system`, `/data`, and
`/cache` from `fstab.vita`, starts the core services, and starts `adbd`. The
framework stays stopped.

If stage 1 fails, it drops to a busybox shell on the framebuffer console. The
Vita has no keyboard in Linux, so treat the shell as a visual indicator rather
than a recovery console; `ur0:/android/boot.log` is the file to read. Reboot the
Vita to retry.

Stage 1 snapshots the kernel ring buffer (which includes Android init and
service messages) to `ur0:/android/boot.log` immediately after mounting ur0.
Once `class core` starts, the `vita-log` init service refreshes it with a
snapshot every 2 seconds for two minutes, then every 30 seconds for ten more
minutes; each snapshot is `sync`ed before it is renamed over `boot.log`, so a
reset cannot lose the latest one. On a stage-1 failure `/init.vita` writes a
final snapshot before dropping to the shell. If the console is unreadable or
adbd never starts, open `ur0:` from VitaOS and read that file; its last lines
show how far the boot got.

### Failure signatures

| Symptom | Likely cause |
| --- | --- |
| `FATAL: ur0 block device (mmcblkXp12) not found` | Kernel has no SCE partition parser or the wrong mmc driver; console only, nothing is written to ur0. |
| `FATAL: missing /mnt/ur0/android/system.img` | Images are not in `ur0:/android/`, or ur0 mounted but the names differ. `boot.log` holds the final dmesg. |
| `FATAL: losetup system.img` | `CONFIG_BLK_DEV_LOOP` missing or the image is not a regular file. |
| **Vita reboots a few seconds after** `init.vita: handing off to /init` | SELinux policy load failed; Android 5.1 calls `android_reboot(..., "recovery")` in that case. Boot the fallback `vita-selinux-disabled.dtb`. |
| Android init runs, `boot.log` shows `fs_mgr` mount errors | Loop nodes or ext4 mount failed; check the `fs_mgr` lines and `mount` output in `boot.log`. |
| Console stops at `class_start core`, no `adbd` | A `critical` core service is restarting (`healthd`, `lmkd`, `servicemanager`, `ueventd`). `boot.log` names the service and includes recent logcat. After more than four crashes in four minutes init reboots into recovery. |
| `servicemanager` logs `binder: driver version differs from user space` | The kernel advertises binder protocol 8. Rebuild with `-DBINDER_IPC_32BIT` in `drivers/android/Makefile`; check `build-info.txt` for `binder ABI: 32-bit (protocol 7)`. |
| adbd never reachable, console alive | Wi-Fi/DHCP problem: read `ur0:/android/vita-net.log` and `ur0:/android/wpa_supplicant.log`. |
| Wi-Fi works, then drops after about two minutes and comes back later | `btmrvl_sdio` bound the SD8787 Bluetooth function and reset the shared card. The v5 kernel disables `CONFIG_BT_MRVL`/`CONFIG_BT_MRVL_SDIO`; check `build-info.txt` for `bt: Marvell SD8787 SDIO transport disabled` and `dmesg` for `btmrvl`. |
| `vita-net.log` shows `watchdog: carrier lost` or `watchdog: ... disappeared` | The firmware reset the card after the first lease. The script waits for `mlan0` to return and restarts the supplicant; `wpa_supplicant.crash.log` holds the log from before the reset. If this repeats, check that the Bluetooth transport is really disabled and look for `mwifiex` errors in the saved dmesg. |
| DHCP lease and a `LISTEN` socket on 5555, but the PC cannot ping or connect | The link can carry broadcast/DHCP but not unicast between the two clients. The v5 kernel disables cfg80211 power save; if it still fails, the phone hotspot is probably isolating clients. Turn off client/AP isolation or tether both machines through a router/AP that allows client-to-client traffic. |
| `boot.log` has `init: waitpid returned pid ...` / `untracked pid ... exited with status 0` | Healthy init reaping. v5 removes these status-0 lines from the dmesg block and prints their count under `--- init reaping ---`; non-zero exits are still shown. |
| `dhcpcd: flock ... Try again` | Two `dhcpcd` instances raced for the lease file. The script starts `dhcpcd` only after the carrier is up and the watchdog starts it once per recovery; check for a manual `dhcpcd` left running under adb. |
| `wpa_supplicant.log` has `sched_scan start` but no `CTRL-EVENT-SCAN-RESULTS` | Scheduled-scan offload is broken on this driver/kernel. The v4 script sets `disable_scan_offload=1`; check that the generated config in `vita-net.log` contains it. |
| `vita-net.log` shows `carrier=0` and the supplicant log has no `Trying to associate` | The supplicant has no usable scan results. Confirm the SSID is broadcast on 2.4 GHz, the AP uses WPA2-PSK, and the regdomain allows its channel. The debug log lists scan results and reject reasons. |
| `carrier=1` but no `inet addr:` | Association works and DHCP does not. Check `dhcpcd`'s pid file under `/data/misc/dhcp`, and that `/data/misc/dhcp` is owned by `1014:1014`. |
| `ifconfig` shows flags without `UP` | The interface was never brought up, which also explains `dhcpcd: waiting for carrier`. The script uses busybox `ifconfig` because the Android `ip` binary produced no output on the first boot. |

## 5. Connect adb

Once Wi-Fi associates and DHCP succeeds, `vita-net.sh` writes the interface
address to `/data/misc/wifi/vita-net.log` and to `ur0:/android/vita-net.log`.
A healthy boot logs:

```
vita-net: adb: listener on port 5555 (init.svc.adbd=running); adb connect 192.168.43.70:5555
vita-net: adb check: inet=192.168.43.70 port=5555
vita-net: gateway=192.168.43.1
vita-net: ping gateway 192.168.43.1:
```

`adb check` continues with the gateway ping result, `/proc/net/arp`,
`/proc/net/route`, and `iptables -S INPUT`. If the socket is missing, the same
lines say `no listener on port 5555 ... restarting adbd` and the script waits
up to 10 seconds for the new listener. The gateway ping working while the PC
cannot ping the Vita is the signature of hotspot client isolation; a failed
gateway ping points at the Wi-Fi receive path. Copy the supplicant log to
`ur0:/android/wpa_supplicant.log`. On the PC:

```sh
adb connect <vita-ip>:5555
adb shell
```

If the DHCP address is unknown, read `ur0:/android/vita-net.log` from VitaOS or
mount the card on a PC; the `inet` line appears there even when adb is not
running.

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
the DHCP address, then read `ur0:/android/vita-net.log` and
`ur0:/android/wpa_supplicant.log`. The `LISTEN on port` line in `boot.log`
shows whether adbd bound its TCP socket at all; `(none)` means the problem is
on the Vita, while a listener plus a failed ping points at the network path
(cfg80211 power save or hotspot client isolation). If the console shows
Android init starting but no network, the Wi-Fi config, the firmware path, or
`dhcpcd`'s `/data/misc/dhcp` directory is the first thing to fix.

## 6. Enable Wi-Fi details and the UI

Wi-Fi runs as a `late_start` oneshot. To retry it from adb:

```sh
adb shell start vita-net
```

To see the supplicant log:

```sh
adb shell cat /data/misc/wifi/wpa_supplicant.log
adb shell cat /data/misc/wifi/vita-net.log
adb shell /sbin/busybox ifconfig mlan0
adb shell cat /sys/class/net/mlan0/carrier /sys/class/net/mlan0/operstate
adb shell ls -l /data/misc/dhcp /data/misc/dhcpcd
```

The same supplicant and network logs are mirrored to
`ur0:/android/wpa_supplicant.log` and `ur0:/android/vita-net.log`, so they can
be read without adb. Each boot moves the previous copies to
`wpa_supplicant.prev.log` and `vita-net.prev.log` before starting, so a second
boot does not destroy the first run's evidence. If the normal-scan phase fails
and the script restarts with `ap_scan=2`, the first phase is kept as
`wpa_supplicant.normal.log`. When the watchdog sees the interface or carrier
drop, it saves the failed phase as `wpa_supplicant.crash.log` before restarting
the supplicant. The ur0 network log includes the generated supplicant config
(with the psk removed), the carrier/operstate state blocks, the watchdog
recovery lines, and the mwifiex/cfg80211 dmesg lines.

The framework is deliberately not started. When the core system is stable,
enable it with:

```sh
adb shell setprop vita.ui 1
```

or create `ur0:/android/ui` and reboot. This starts zygote and SurfaceFlinger.
Expect the display path to fail or show a black screen at first: the SDK image
has no HWC and no software GLES 2.0. Capture `logcat` before changing anything.

## 7. Fallback and recovery

- **Vita reboots by itself after handoff:** read `ur0:/android/boot.log`. The
  synced snapshot names any critical service that is crash-looping and includes
  the last logcat lines. If it shows that the SELinux policy failed to load,
  copy `vita-selinux-disabled.dtb` over `ux0:/linux/vita.dtb` and boot again.
- **Kernel panics or hangs:** restore the known-good LinuxOnVita
  `zImage`/`vita.dtb` pair and boot with **CROSS**.
- **Android init fails before adbd:** read the framebuffer console; with
  `loglevel 7` it shows init and service messages. If no output is visible,
  `ur0:/android/boot.log` is authoritative. Stage 1 leaves a busybox shell on
  failure, but there is no keyboard, so use the log.
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
- `ur0:/android/boot.log`;
- `dmesg` from adb, if it starts;
- `logcat -d` from the first Android boot;
- `adb shell getprop`;
- `adb shell mount`;
- `ur0:/android/vita-net.log` and `ur0:/android/wpa_supplicant.log`;
- the exact `SHA256SUMS` and `build-info.txt` used.

That is enough to separate kernel/config problems from Android userspace and
graphics problems.
