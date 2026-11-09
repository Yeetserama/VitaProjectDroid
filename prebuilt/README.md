# prebuilt

Small, redistributable pieces committed so a kernel build only needs the
kernel source and a cross compiler.

| File | Size | SHA-256 | Origin |
| --- | --- | --- | --- |
| `ramdisk.img` | 806,576 | `293F60C1DC08ADFCDFBF2FE4C37F73AB4B75C5A84A9CC25F15001878630715DD` | `system-images;android-22;default;armeabi-v7a` (`armeabi-v7a/ramdisk.img`) |
| `busybox-armv7l` | 1,152,216 | `CD04052B8B6885F75F50B2A280BFCBF849D8710C8E61D369C533ACF307EDA064` | <https://busybox.net/downloads/binaries/1.31.0-defconfig-multiarch-musl/busybox-armv7l> |

`busybox-armv7l` is a statically linked ARM EABI executable with no
interpreter (verified with `pyelftools`). It provides the stage-1 shell,
`mount`, `losetup`, `mkswap`, `swapon`, `mdev`, and the other applets used by
`/init.vita`.

`ramdisk.img` is the unmodified Android 5.1.1 ARM ramdisk. The initramfs build
copies it, removes the goldfish/ranchu configuration files, and adds the Vita
overlay.

The 194 MB SDK zip is **not** needed for the kernel build; only these two small
files are. The SDK system/user data images are installed separately on the
Vita (see `docs/getting-started.md`).
