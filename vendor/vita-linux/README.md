# vita-linux: glibc Wi-Fi userspace for Android

Android 5.1's SD8787 Wi-Fi support needs a `wpa_supplicant` that speaks
`nl80211`. The prebuilt SDK system image does not ship one (the emulator used
`qemud`/`eth0`). Rather than cross-compiling a second supplicant against
bionic, we reuse the glibc build that already works on the Vita in LinuxOnVita
1.5.0 and run it through its own dynamic loader.

These files are taken from the rootfs embedded in the LinuxOnVita 1.5.0
`zImage` (`payload.bin`, gzip/cpio). They are distributed under the licenses
of their upstream projects (wpa_supplicant/OpenSSL/libnl: BSD/GPL/Apache as
applicable; the Marvell firmware under the Marvell/linux-firmware terms).

## Contents

| Path | Source | Purpose |
| --- | --- | --- |
| `usr/sbin/wpa_supplicant` | Alpine `wpa_supplicant` (glibc) | nl80211 supplicant |
| `lib/ld-linux-armhf.so.3` | glibc | dynamic loader for the supplicant |
| `lib/libc.so.6` | glibc | C runtime |
| `lib/libatomic.so.1.2.0` | gcc runtime | required by libcrypto/libssl |
| `usr/lib/libnl-3.so.200.26.0` | libnl | netlink helpers |
| `usr/lib/libnl-genl-3.so.200.26.0` | libnl | generic netlink |
| `usr/lib/libssl.so.3` | OpenSSL 3 | TLS |
| `usr/lib/libcrypto.so.3` | OpenSSL 3 | crypto |
| `lib/firmware/mrvl/sd8787_uapsta.bin` | linux-firmware | SD8787 firmware |
| `lib/firmware/regulatory.db` | wireless-regdb | regulatory database |
| `lib/firmware/regulatory.db.p7s` | wireless-regdb | signature |

## Symlinks created at initramfs build time

```
vita-linux/lib/libatomic.so.1          -> libatomic.so.1.2.0
vita-linux/usr/lib/libnl-3.so.200      -> libnl-3.so.200.26.0
vita-linux/usr/lib/libnl-genl-3.so.200 -> libnl-genl-3.so.200.26.0
```

The loader is always invoked explicitly:

```sh
/vita-linux/lib/ld-linux-armhf.so.3 \
  --library-path /vita-linux/lib:/vita-linux/usr/lib \
  /vita-linux/usr/sbin/wpa_supplicant -Dnl80211 -imlan0 -c /mnt/ur0/android/wpa_supplicant.conf
```

Do not try to run these binaries directly: Android's bionic linker cannot load
glibc ELF files.
