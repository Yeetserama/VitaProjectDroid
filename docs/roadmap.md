# Roadmap

The order is deliberate: prove the kernel/userspace handoff headlessly, prove
storage and networking, then spend time on graphics. Graphics is the make-or-break
gate and should not start before Android init and adb are stable.

## Phase 0 - Hardware and environment audit

**Status: complete.**

- PCH-1000 (OLED), firmware 3.65 with HENkaku + Enso.
- "Enable Unsafe Homebrew" on; full console backup taken.
- LinuxOnVita 6.12 installed on the official 16 GB card (`ux0:`), boots to a
  working shell/desktop, and completed first-boot setup.
- Storage decision: no SD2Vita. Boot files on `ux0:/linux/`, Android images on
  `ur0:/android/` (~2.5 GB free).
- Inventory script added at `scripts/vita-inventory.sh`.

**Exit criteria met:** LinuxOnVita boots reliably, input/Wi-Fi/storage work, and
the hardware inventory is recorded.

## Phase 1 - Prebuilt image and boot package

**Status: in progress.** Scripts and CI are written; the first kernel build has
not run.

**Goal:** produce a reproducible `zImage` + `vita.dtb` + `initramfs.cpio` package
for the Vita without building AOSP.

- Download and verify the Android 5.1.1 SDK image
  (`system-images;android-22;default;armeabi-v7a`).
- Inspect `system.img`/`userdata.img` ext4 features and contents.
- Vendor the stock ARM ramdisk and static busybox with pinned SHA-256 hashes.
- Recover LinuxOnVita 1.5.0's glibc `wpa_supplicant`, dependency closure, and
  SD8787 firmware into `vendor/vita-linux/`.
- Write `init.vita`, `init.vita.rc`, `fstab.vita`, `ueventd.vita.rc`,
  `default.prop`, `vita-net.sh`, and `vita-ui.sh`.
- Backport ashmem from Linux 4.19 to 6.12; wire it into `drivers/misc`.
- Merge `kernel/config/vita-android.fragment` over `vita_defconfig`.
- Build `zImage` and `vita1000.dtb`; patch the DTB bootargs with `fdtput`.
- Run the GitHub Actions build and fix the first compile/config errors.

**Exit criteria:** CI produces `zImage`, `vita.dtb`,
`vita-selinux-disabled.dtb`, and `initramfs.cpio` with recorded hashes, and the
required-symbol checks pass.

## Phase 2 - First headless Android boot

**Status: next.**

**Goal:** Android's init runs on real Vita hardware and adbd is reachable.

- Copy `zImage`/`vita.dtb` to `ux0:/linux/` and the images to
  `ur0:/android/`.
- Boot with the permissive DTB and `console=tty0`.
- Watch stage 1: devtmpfs, ur0 mount, loop devices, zram, `exec /init`.
- Verify Android init imports `/init.vita.rc`, mounts `/system`, `/data`, and
  `/cache`, and starts `adbd`.
- Fix the first failure: SELinux policy load, missing device node, mount flags,
  or a missing kernel symbol.
- Connect `adb connect <vita-ip>:5555`, then capture `dmesg`, `logcat`, and
  `getprop`.
- Confirm the disabled-SELinux DTB boots if permissive does not.

**Exit criteria:** adbd stays up, `adb shell` works without system_server, and
there is no kernel panic for at least 10 minutes.

## Phase 3 - Storage, data, and networking

**Goal:** Android has persistent `/data` and a working debug/network path.

- Verify `/data` is writable and survives a reboot.
- Check the first ART/dex2oat boot against the 550 MB data image; grow or tune
  if it fills.
- Bring up Wi-Fi: `mlan0`, firmware load, supplicant, DHCP lease.
- Verify `vita-net.log` on ur0 and the address from adb.
- Decide whether netd's `xt_qtaguid` errors need a stub or can be ignored.
- Confirm clean shutdown/reboot and that VitaOS still boots.
- Run repeated boot cycles (at least 10) to catch exFAT/ur0 corruption.

**Exit criteria:** `adb shell` works after reboot, `/data` persists, Wi-Fi
associates and leases, and ur0 survives 10 boot cycles.

## Phase 4 - Input

**Goal:** touch and buttons are usable from Android.

- Verify `vita-syscon-ts` multitouch axes and the IDC file.
- Verify the keylayout for D-pad, face buttons, L1/R1, Select/Start/Mode,
  Volume, and Power.
- Use `getevent`/`uinput` to inject events from adb and confirm InputReader sees
  them.
- Add analog-stick-to-D-pad navigation later.

**Exit criteria:** `getevent` and `logcat` show correct key/touch events, and a
simple Android app can be navigated with the Vita controls.

## Phase 5 - Display and graphics

**Status: blocked on the missing GLES 2.0 path.**

**Goal:** SurfaceFlinger presents frames and a trivial GLES 2.0 app runs.

- Implement a minimal `hwcomposer.vita` or fbdev presenter against
  `/dev/graphics/fb0`.
- Get SurfaceFlinger to present a solid color or test pattern.
- Spike software GLES:
  - build a standalone GLES 2.0 triangle test on the Vita;
  - try Mesa softpipe/llvmpipe, SwiftShader 2.x, and a 2D-only fallback;
  - measure frame time at 960x544.
- If GLES works, integrate it as the EGL/GLES driver and start the launcher.
- If GLES does not work, decide whether the 2D-only path is worth continuing.

**Exit criteria:** SurfaceFlinger presents frames; a trivial GLES 2.0 app
renders; the launcher appears, even if slow.

## Phase 6 - UI and hardware stubs

**Goal:** the device is usable without a keyboard.

- Enable the framework with `vita.ui=1` or `ur0:/android/ui`.
- Lights HAL: backlight and PS button LED.
- Power HAL and `healthd`: stub or minimal battery reporting.
- Audio HAL: stub with no output; do not block boot on audio.
- Disable Bluetooth, camera, GPS, telephony in the product config.
- Set density, boot animation, and a lightweight launcher.
- Document unsupported hardware clearly.

**Exit criteria:** a user can unlock, open Settings, connect to Wi-Fi, and launch
a simple app using only the Vita's controls.

## Phase 7 - Performance and app testing

**Goal:** make the result as pleasant as the hardware allows.

- Tune ART filters, heap sizes, background app limits, and zram.
- Measure boot time, launcher frame time, app launch time, and memory pressure.
- Test a set of period-correct APKs (2D games, readers, utilities).
- Reduce tearing if possible; investigate a DRM/KMS driver for the display.
- Document known-good apps and settings.

**Exit criteria:** a stable retro Android for the Vita with a documented app
compatibility list.

## Definition of done for v0.1

- Android 5.1.1 boots headless on a PCH-1000 from ur0.
- `adb shell`, `logcat`, and `dmesg` work over Wi-Fi.
- `/data` persists across reboots.
- Touch and buttons are visible to Android.
- No kernel panic in a 1-hour soak test.
- Audio/camera/Bluetooth/sensors are documented as unsupported.
- A user can return to VitaOS by rebooting.

The launcher/UI is v0.2 and depends on Phase 5.

## Risk register

| Risk | Impact | Likelihood | Mitigation |
| --- | --- | --- | --- |
| No usable software GLES 2.0 | No normal Android UI | High | Spike early; fallback to 2D-only |
| No HWC in the SDK image | SurfaceFlinger cannot present | High | Minimal fbdev presenter / custom HWC |
| 512 MB RAM causes OOM | Unusable | High | Low-RAM config, zram, minimal system, no GApps |
| ur0/exFAT corruption | Data loss, VitaOS repair | Medium | Backup, clean shutdowns, read-only system, keep free space |
| ashmem 6.12 compile/API issues | Kernel build fails | Medium | 4.19 driver adapted to 6.12; fix in CI |
| SELinux policy load fails | Android init stops | Medium | Permissive/disabled DTBs; inspect dmesg |
| netd `xt_qtaguid` missing | Network errors | Medium | Accept for bring-up; stub later |
| Wi-Fi supplicant integration | No adb/network | Medium | glibc supplicant closure; CLI logs |
| No UART access | Hard early debugging | Medium | fbcon + stage-1 busybox shell; solder later |
| Thermal/battery limits | Instability | Medium | Cap CPU, monitor temperature, keep charged |
| Legal/proprietary files | Takedown | Low if careful | No Sony firmware/keys in the repo |

## Effort estimate

| Milestone | Experienced embedded dev | Hobbyist evenings |
| --- | --- | --- |
| Phase 0 | done | done |
| Phase 1 | 1-3 days | 1-2 weeks |
| Phase 2 | 2-5 days | 1-3 weeks |
| Phase 3 | 1-2 weeks | 3-6 weeks |
| Phase 4 | 2-4 days | 1-2 weeks |
| Phase 5 | 1-6 months (graphics risk) | 3-12 months |
| Phase 6 | 2-4 weeks | 2-3 months |
| Phase 7 | ongoing | ongoing |

These are rough. The graphics phase dominates and may fail.
