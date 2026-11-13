#!/usr/bin/env bash
# Add the Android bits to a LinuxOnVita 6.12 tree and build zImage + DTB.
#
# Usage:
#   scripts/apply-kernel.sh [KERNEL_SRC] [INITRAMFS.cpio] [OUTPUT_DIR] [ANDROID_VERSION]
#
# Environment:
#   CROSS_COMPILE   default arm-linux-gnueabihf-
#   JOBS            default nproc
#   ANDROID_VERSION 5.1 or 6.0 or 7.0 (default 5.1)

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
KERNEL_SRC=${1:-"$ROOT/build/linux_vita"}
INITRAMFS=${2:-"$ROOT/build/initramfs.cpio"}
OUT=${3:-"$ROOT/build/out"}
ANDROID_VERSION=${4:-"5.1"}

echo "apply-kernel.sh: ANDROID_VERSION=$ANDROID_VERSION"
echo "apply-kernel.sh: KERNEL_SRC=$KERNEL_SRC"
echo "apply-kernel.sh: INITRAMFS=$INITRAMFS"
echo "apply-kernel.sh: OUT=$OUT"

CROSS_COMPILE=${CROSS_COMPILE:-arm-linux-gnueabihf-}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
ARCH=arm

case "$ANDROID_VERSION" in
    5.1)
        BOOTARGS="earlyprintk keep_bootcon console=tty0 console=ttyS0,115200 androidboot.hardware=vita androidboot.selinux=permissive androidboot.console=tty0 androidboot.serialno=PCH1000 rdinit=/init.vita"
        FRAGMENT="$ROOT/kernel/config/vita-android.fragment"
        LOCALVERSION="-vita-android"
        ;;
    6.0)
        # Android 6.0 still boots headless while the framework is brought up,
        # so keep the console on the framebuffer for the first hardware runs.
        BOOTARGS="earlyprintk keep_bootcon console=tty0 console=ttyS0,115200 androidboot.hardware=vita androidboot.selinux=permissive androidboot.console=tty0 androidboot.serialno=PCH1000 rdinit=/init.vita"
        # 6.0 uses userspace logd/lmkd, like 5.1, so the same fragment works.
        FRAGMENT="$ROOT/kernel/config/vita-android.fragment"
        LOCALVERSION="-vita-android-6.0"
        ;;
    7.0)
        # Android 7.0 Nougat (API 24).  Same bootargs as 6.0; headless bring-up
        # first, then enable UI via setprop vita.ui 1 after serial console is
        # verified.
        BOOTARGS="earlyprintk keep_bootcon console=tty0 console=ttyS0,115200 androidboot.hardware=vita androidboot.selinux=permissive androidboot.console=tty0 androidboot.serialno=PCH1000 rdinit=/init.vita"
        FRAGMENT="$ROOT/kernel/config/vita-android.fragment"
        LOCALVERSION="-vita-android-7.0"
        ;;
    *)
        echo "apply-kernel: unknown Android version: $ANDROID_VERSION" >&2
        exit 1
        ;;
esac

die() {
    echo "apply-kernel: $*" >&2
    exit 1
}

require_symbol() {
    local sym=$1
    if ! grep -q "^${sym}=y$" "$KERNEL_SRC/.config"; then
        echo "apply-kernel: required kernel symbol ${sym}=y is missing" >&2
        echo "apply-kernel: check the fragment and the kernel build log" >&2
        exit 1
    fi
}

require_absent() {
    local sym=$1
    # A symbol whose dependency is disabled disappears from .config entirely,
    # so absence of an assignment is the success case.  Only =y/=m fail.
    if grep -q "^${sym}=" "$KERNEL_SRC/.config"; then
        echo "apply-kernel: required kernel symbol ${sym} is enabled" >&2
        grep "^${sym}=" "$KERNEL_SRC/.config" >&2
        echo "apply-kernel: check the fragment and the kernel build log" >&2
        exit 1
    fi
}

[ -d "$KERNEL_SRC" ] || die "kernel tree not found: $KERNEL_SRC"
[ -f "$KERNEL_SRC/Makefile" ] || die "$KERNEL_SRC is not a kernel tree"
[ -f "$INITRAMFS" ] || die "initramfs not found: $INITRAMFS (run build-initramfs.sh first)"
command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1 || die "cross compiler ${CROSS_COMPILE}gcc not found"

mkdir -p "$OUT"

echo "==> copying ashmem driver"
rm -rf "$KERNEL_SRC/drivers/misc/ashmem"
cp -a "$ROOT/kernel/ashmem" "$KERNEL_SRC/drivers/misc/ashmem"

echo "==> wiring ashmem and Vita DTBs into the tree"
python3 "$ROOT/scripts/patch-kernel-tree.py" "$KERNEL_SRC"

grep -qF -- '-DBINDER_IPC_32BIT' "$KERNEL_SRC/drivers/android/Makefile" \
    || die "drivers/android/Makefile does not select the 32-bit binder ABI"

echo "==> configuring kernel"
make -C "$KERNEL_SRC" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" vita_defconfig >/dev/null

# Install the assembled initramfs where the kernel's usr/Makefile expects it.
# The .cpio suffix is important: usr/Makefile then uses the file directly
# instead of treating it as a gen_init_cpio file list.
cp "$INITRAMFS" "$KERNEL_SRC/vita-initramfs.cpio"

"$KERNEL_SRC/scripts/kconfig/merge_config.sh" \
    -m -O "$KERNEL_SRC" \
    "$KERNEL_SRC/.config" "$FRAGMENT"

make -C "$KERNEL_SRC" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" olddefconfig

"$KERNEL_SRC/scripts/config" --file "$KERNEL_SRC/.config" \
    --set-str INITRAMFS_SOURCE "vita-initramfs.cpio" \
    --enable INITRAMFS_COMPRESSION_NONE \
    --disable INITRAMFS_COMPRESSION_GZIP \
    --disable INITRAMFS_COMPRESSION_BZIP2 \
    --disable INITRAMFS_COMPRESSION_LZMA \
    --disable INITRAMFS_COMPRESSION_XZ \
    --disable INITRAMFS_COMPRESSION_LZO \
    --disable INITRAMFS_COMPRESSION_LZ4 \
    --disable INITRAMFS_COMPRESSION_ZSTD \
    --set-val INITRAMFS_ROOT_UID 0 \
    --set-val INITRAMFS_ROOT_GID 0

make -C "$KERNEL_SRC" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" olddefconfig

echo "==> checking required kernel configuration"
for sym in \
    CONFIG_ASHMEM \
    CONFIG_ANDROID_BINDER_IPC \
    CONFIG_BLK_DEV_LOOP \
    CONFIG_CGROUPS \
    CONFIG_MEMCG \
    CONFIG_MEMCG_V1 \
    CONFIG_CGROUP_CPUACCT \
    CONFIG_DEBUG_FS \
    CONFIG_DEVTMPFS \
    CONFIG_DMA_SHARED_BUFFER \
    CONFIG_EXFAT_FS \
    CONFIG_EXT4_FS \
    CONFIG_EXT4_FS_SECURITY \
    CONFIG_FUSE_FS \
    CONFIG_INPUT_EVDEV \
    CONFIG_INPUT_MISC \
    CONFIG_INPUT_UINPUT \
    CONFIG_MWIFIEX \
    CONFIG_MWIFIEX_SDIO \
    CONFIG_SECURITY_SELINUX \
    CONFIG_SECURITY_SELINUX_DEVELOP \
    CONFIG_SWAP \
    CONFIG_SW_SYNC \
    CONFIG_SYNC_FILE \
    CONFIG_TMPFS \
    CONFIG_VFAT_FS \
    CONFIG_ZRAM; do
    require_symbol "$sym"
done

# Both 5.1 and 6.0 use userspace logd and lmkd, so CONFIG_LOG and
# CONFIG_ANDROID_LOW_MEMORY_KILLER must not be needed here.

# btmrvl_sdio shares the SD8787 firmware with mwifiex.  If it binds, its
# probe blocks for ~100 s waiting for a BT image, then fails and resets the
# card, taking Wi-Fi down every ~2 minutes.  v0.1 has no Bluetooth stack.
for sym in CONFIG_BT_MRVL CONFIG_BT_MRVL_SDIO; do
    require_absent "$sym"
done

# cfg80211 power save lets the SD8787 sleep between beacons; on this firmware
# it can drop unicast frames, which looks like a working DHCP lease but a dead
# ping/adb connection.  Keep the station awake.
require_absent CONFIG_CFG80211_DEFAULT_PS

grep -q '^CONFIG_INITRAMFS_SOURCE="vita-initramfs.cpio"$' "$KERNEL_SRC/.config" \
    || die "CONFIG_INITRAMFS_SOURCE was not set"
grep -q '^CONFIG_LSM=".*selinux' "$KERNEL_SRC/.config" \
    || die "SELinux is not in CONFIG_LSM"
grep -q '^CONFIG_ANDROID_BINDER_DEVICES="binder' "$KERNEL_SRC/.config" \
    || die "CONFIG_ANDROID_BINDER_DEVICES was not set"

echo "==> building zImage and vita1000.dtb"
make -C "$KERNEL_SRC" ARCH="$ARCH" CROSS_COMPILE="$CROSS_COMPILE" -j"$JOBS" \
    zImage vita1000.dtb

ZIMAGE="$KERNEL_SRC/arch/arm/boot/zImage"
DTB="$KERNEL_SRC/arch/arm/boot/dts/vita1000.dtb"
[ -f "$ZIMAGE" ] || die "build did not produce $ZIMAGE"
[ -f "$DTB" ] || die "build did not produce $DTB"

echo "==> collecting artifacts"
VERSION_SUFFIX=""
case "$ANDROID_VERSION" in
    5.1) VERSION_SUFFIX="" ;;
    6.0) VERSION_SUFFIX="-6.0" ;;
    7.0) VERSION_SUFFIX="-7.0" ;;
    *) echo "apply-kernel: unknown Android version: $ANDROID_VERSION" >&2; exit 1 ;;
esac

echo "ANDROID_VERSION=$ANDROID_VERSION, VERSION_SUFFIX=$VERSION_SUFFIX"

cp "$ZIMAGE" "$OUT/zImage${VERSION_SUFFIX}"
cp "$DTB" "$OUT/vita1000.dtb${VERSION_SUFFIX}"
cp "$DTB" "$OUT/vita.dtb${VERSION_SUFFIX}"
cp "$INITRAMFS" "$OUT/initramfs.cpio${VERSION_SUFFIX}"

command -v fdtput >/dev/null 2>&1 || die "fdtput not found (install device-tree-compiler)"
fdtput -t s "$OUT/vita.dtb${VERSION_SUFFIX}" /chosen bootargs "$BOOTARGS"
cp "$OUT/vita.dtb${VERSION_SUFFIX}" "$OUT/vita-selinux-disabled.dtb${VERSION_SUFFIX}"
fdtput -t s "$OUT/vita-selinux-disabled.dtb${VERSION_SUFFIX}" /chosen bootargs \
    "${BOOTARGS/androidboot.selinux=permissive/androidboot.selinux=disabled}"

{
    echo "kernel: incognitojam/linux_vita vita-port-6.12.0"
    echo "kernel commit: a33b819fd7174f60b1d5bcb613de565b3f046687"
    echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "toolchain: $("${CROSS_COMPILE}gcc" --version | head -n 1)"
    echo "android version: $ANDROID_VERSION"
    echo "binder ABI: 32-bit (protocol 7)"
    echo "wifi: v6 (disable_scan_offload=1, adb listener check, main-lookup rule repair, carrier-gated dhcpcd, watchdog)"
    echo "wifi ps: disabled (CONFIG_CFG80211_DEFAULT_PS is not set)"
    echo "bt: Marvell SD8787 SDIO transport disabled (shares firmware with mwifiex)"
    echo "initramfs bytes: $(stat -c %s "$OUT/initramfs.cpio${VERSION_SUFFIX}" 2>/dev/null || wc -c < "$OUT/initramfs.cpio${VERSION_SUFFIX}")"
    echo "zImage bytes: $(stat -c %s "$OUT/zImage${VERSION_SUFFIX}" 2>/dev/null || wc -c < "$OUT/zImage${VERSION_SUFFIX}")"
    echo
    echo "bootargs (vita.dtb):"
    echo "$BOOTARGS"
    echo
    echo "bootargs (vita-selinux-disabled.dtb):"
    echo "${BOOTARGS/androidboot.selinux=permissive/androidboot.selinux=disabled}"
} > "$OUT/build-info.txt${VERSION_SUFFIX}"

(
    cd "$OUT"
    sha256sum zImage${VERSION_SUFFIX} vita.dtb${VERSION_SUFFIX} vita1000.dtb${VERSION_SUFFIX} vita-selinux-disabled.dtb${VERSION_SUFFIX} initramfs.cpio${VERSION_SUFFIX} > SHA256SUMS${VERSION_SUFFIX}
)

echo
echo "artifacts in $OUT:"
ls -lh "$OUT"/zImage${VERSION_SUFFIX} "$OUT"/vita*.dtb${VERSION_SUFFIX} "$OUT"/initramfs.cpio${VERSION_SUFFIX} "$OUT"/SHA256SUMS${VERSION_SUFFIX}
cat "$OUT/SHA256SUMS${VERSION_SUFFIX}"
