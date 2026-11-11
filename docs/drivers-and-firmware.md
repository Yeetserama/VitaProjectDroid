# Drivers and firmware: the honest answer

## 1. Short answer

You do **not** get Android drivers by extracting the Vita's firmware.

- VitaOS is FreeBSD-based. Its drivers are Sony kernel modules and userland
  libraries built for that kernel. Android uses a Linux kernel. The two are not
  compatible.
- The drivers Android needs are **Linux drivers**. Most of them already exist in
  the community Linux port. The missing ones (GPU, audio, camera, sensors,
  Bluetooth, battery, suspend) do not exist in Sony's firmware in a form Linux
  can use either.
- The only proprietary data Android needs is the **Wi-Fi firmware blob** for the
  Marvell SD8787, plus the wireless regulatory database. Those come from
  `linux-firmware`, not from Sony.
- There is no public Linux/Android driver for the Vita's PowerVR SGX543MP4+.
  That is a reverse-engineering project, not a file you can extract.

"Firmware", "driver", and "vendor blob" are often used interchangeably online.
They are different things:

| Term | What it is | Vita example |
| --- | --- | --- |
| Driver | Kernel or userspace code that talks to hardware | `sdhci-vita.c`, `mwifiex`, `vita-syscon-ts.c` |
| Firmware | Data loaded into a device's microcontroller | `mrvl/sd8787_uapsta.bin` |
| Vendor blob | Closed userspace library for a specific SoC | PowerVR EGL/GLES libraries (not available for Linux on Vita) |
| VitaOS module | Sony kernel extension for the FreeBSD-based OS | `.skprx` modules in `os0:`/`vs0:` |

## 2. What already works in the Linux port

The `incognitojam/linux_vita` 6.12 port and LinuxOnVita patches provide:

| Function | Driver / source | Status |
| --- | --- | --- |
| CPU, SMP, GIC, timers | `arch/arm/mach-vita`, ARM GIC/TWD | Works |
| UART debug console | `drivers/tty/serial/vita-uart.c` | Works |
| I2C | `drivers/i2c/busses/i2c-vita.c` | Works |
| SPI | `drivers/spi/spi-vita.c` | Works |
| GPIO | `drivers/gpio/gpio-vita.c` | Works |
| Reset controller | `drivers/reset/reset-vita.c` | Works |
| Syscon (touch/buttons/RTC/clock) | `drivers/mfd/vita-syscon.c` | Works |
| Touchscreen | `drivers/input/touchscreen/vita-syscon-ts.c` | Works |
| Buttons | `drivers/input/joystick/vita-buttons.c` | Works |
| RTC | `drivers/rtc/rtc-vita.c` | Works |
| SDHCI (eMMC, SD2Vita, SDIO) | `drivers/mmc/host/sdhci-vita.c` | Works |
| Wi-Fi | `drivers/net/wireless/marvell/mwifiex` + `pwrseq_vita_wlan` | Works |
| Display | `simple-framebuffer` (initialized by the bare-metal loader) | Works, but no KMS/page-flip/vsync |
| CPU frequency | `vita-cpufreq` (LinuxOnVita patch) | Works |
| LEDs | `gpio-leds` | Works |

## 3. What is missing

| Function | Status | Consequence for Android |
| --- | --- | --- |
| GPU (SGX543MP4+) | No Linux driver; LinuxOnVita lists it as long-term research | No hardware GLES; software rendering only |
| Audio | No codec driver; the `vita-audio` branch only enables ALSA and clock control | No sound; audio HAL must be a stub |
| Battery/charger | No `power_supply` driver; LinuxOnVita reports battery unknown | `healthd` shows unknown; no charge status |
| Suspend/resume | Not implemented (roadmap item) | Screen-off is just backlight off; no deep sleep |
| Bluetooth | No driver; the `vita-bluetooth` branch has no changes yet | No BT audio/input |
| Camera | No V4L2 sensor driver | Camera HAL stub; no camera |
| Motion sensors | No IIO driver | Sensor HAL stub; no gyro/accelerometer |
| 3G modem | No driver | No telephony/data on PCH-1100 |
| USB | `# CONFIG_USB_SUPPORT is not set` | No USB ADB or USB host; use Wi-Fi ADB |
| Display controller | Only `simple-framebuffer`; no DRM/KMS | No page flip, no vsync, tearing possible |

This is the real gap. It is not a matter of finding the right Sony file.

## 4. Firmware blobs Android actually needs

### Wi-Fi: Marvell SD8787

The Vita's Wi-Fi is a Marvell SD8787 (SDIO). The `mwifiex` driver requests:

```
mrvl/sd8787_uapsta.bin
```

Get it from the upstream `linux-firmware` repository:

```
https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git
```

This project embeds it in the initramfs at
`/lib/firmware/mrvl/sd8787_uapsta.bin`, the path `request_firmware` uses before
Android mounts any filesystem. Keep `/lib/firmware/regulatory.db` there too. Do
not rely on `/vendor/firmware` or `/system/etc/firmware` being mounted when
`mwifiex` probes.

### Regulatory database

```
regulatory.db
regulatory.db.p7s
```

These are also in `linux-firmware`. Without them, cfg80211 may refuse to set
regulatory rules or fall back to a world domain.

### Bluetooth (only if BT is enabled later)

The combo chip uses Marvell Bluetooth firmware from `linux-firmware`. The exact
file depends on the `btmrvl`/`hci` path. Do not add it until there is a working
Bluetooth driver.

### What about Sony firmware files?

None are needed for Android. Do not ship Sony firmware, keys, or decrypted
modules in this repository.

## 5. If you want to reverse-engineer the Vita hardware

The staged GPU/audio plan, workspace rules and go/no-go gates are in
[`reverse-engineering.md`](reverse-engineering.md).

Dumping your own console's firmware is useful for research:

- **VitaShell** can browse and dump `os0:`, `vs0:`, `ur0:`, and `ux0:` content.
  Some files are decrypted; some are not.
- **wiki.henkaku.xyz** documents many hardware registers, Syscon commands, and
  initialization sequences discovered by the community.
- The Linux port itself is the best documentation of what was learned: DTS,
  drivers, and patches show the register addresses and sequences.
- **psvimgtools** and related tools are for backups/PUP handling, not for
  producing Android drivers.

Use dumps to understand the hardware, then write a Linux driver. Do not expect
to load a VitaOS module into Linux.

### The GPU and audio are the two hard research targets

- **GPU:** PowerVR SGX543MP4+ (Series5XT). There is no mainline driver. A Linux
  driver would need command stream, memory management, power, and a userspace
  EGL/GLES compiler/runtime. This is far beyond a normal port.
- **Audio:** the Vita's audio path goes through Syscon/clockgen and a codec. The
  `vita-audio` branch only turns on ALSA and clock control; there is no codec
  driver yet. A simple PCM playback driver might be feasible with register
  research; Android audio then needs a tinyalsa HAL.

## 6. Legal and ethical boundaries

- **AOSP** is Apache-2.0 (mostly). **Linux** is GPL-2.0. **LinuxOnVita** is
  GPL-3.0. **VitaSDK/taiHEN/Enso** have their own open-source licenses. Respect
  them.
- **Sony firmware and keys are proprietary.** Dumping them from your own console
  for interoperability research may be legal where you live; distributing them
  is not. Do not commit them, bundle them in a release, or link to pirated
  copies.
- **Marvell firmware** in `linux-firmware` is redistributable under its included
  license. Keep the license text with it.
- **Game assets and APKs** are copyrighted. The project must not ship them. A
  user supplies their own legally acquired apps.
- **Modding your console** may violate Sony's terms of service and warranty.
  That is your decision and your risk.

## 7. What to download, concretely

| Need | Source |
| --- | --- |
| Vita Linux kernel + DTS | `https://github.com/incognitojam/linux_vita` (branch `vita-port-6.12.0`) |
| Vita Linux distro/loader/build scripts | `https://github.com/devwithzachary/LinuxOnVita` |
| Bare-metal Linux loader | `https://github.com/xerpi/vita-baremetal-linux-loader` |
| Bare-metal payload loader | `https://github.com/xerpi/vita-baremetal-loader` |
| Homebrew toolchain | `https://vitasdk.org/` |
| Android 5.1.1 system image | Android SDK `system-images;android-22;default;armeabi-v7a` from `https://dl.google.com/android/repository/sys-img/android/` |
| Android source (optional, later) | `https://android.googlesource.com/platform/manifest` |
| Wi-Fi firmware + regulatory DB | `https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git` |
| Hardware notes | `https://wiki.henkaku.xyz/` |

If a guide tells you to download a "Vita Android ROM" or a "driver pack" from a
file host, treat it as suspect. No such official package exists.
