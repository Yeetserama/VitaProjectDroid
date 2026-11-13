#!/usr/bin/env bash
# Assemble the Android initramfs for the Vita.
#
# Inputs:
#   prebuilt/ramdisk.img        stock Android 5.1.1 ARM ramdisk (gzip cpio)
#   prebuilt/ramdisk-6.0.img    stock Android 6.0 ARM ramdisk (gzip cpio)
#   prebuilt/busybox-armv7l     static ARM busybox used by /init.vita
#   initramfs-overlay/          Vita-specific init, fstab, input files (5.1)
#   initramfs-overlay-6.0/      Vita-specific init, fstab, input files (6.0 / 7.0)
#   vendor/vita-linux/          glibc wpa_supplicant + firmware
#
# Output:
#   A raw newc cpio archive with uid/gid 0 for every entry.  The kernel build
#   embeds it directly (CONFIG_INITRAMFS_SOURCE=...cpio).
#
# Usage:
#   scripts/build-initramfs.sh [OUTPUT.cpio] [STAGING_DIR] [ANDROID_VERSION]
#   ANDROID_VERSION: 5.1 or 6.0 or 7.0 (default 5.1)

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${1:-"$ROOT/build/initramfs.cpio"}
STAGE=${2:-"$ROOT/build/initramfs-root"}
ANDROID_VERSION=${3:-"5.1"}

echo "build-initramfs.sh: ANDROID_VERSION=$ANDROID_VERSION"

case "$ANDROID_VERSION" in
    5.1)
        RAMDISK="$ROOT/prebuilt/ramdisk.img"
        OVERLAY="$ROOT/initramfs-overlay"
        RAMDISK_SHA256="293F60C1DC08ADFCDFBF2FE4C37F73AB4B75C5A84A9CC25F15001878630715DD"
        ;;
    6.0)
        RAMDISK="$ROOT/prebuilt/ramdisk-6.0.img"
        OVERLAY="$ROOT/initramfs-overlay-6.0"
        RAMDISK_SHA256="713225AFABEE109C75493F8D793945768303DED45E6F9021D839C673A26475A8"
        ;;
    7.0)
        RAMDISK="$ROOT/prebuilt/ramdisk-7.0.img"
        OVERLAY="$ROOT/initramfs-overlay-7.0"
        RAMDISK_SHA256="9A287CC86FB06AE2EE72D7F227BE29A1888968855343C6AD0C3A7D374C85AC52"
        ;;
    *)
        echo "build-initramfs: unknown Android version: $ANDROID_VERSION" >&2
        exit 1
        ;;
esac

BUSYBOX="$ROOT/prebuilt/busybox-armv7l"
VENDOR="$ROOT/vendor/vita-linux"

BUSYBOX_SHA256="CD04052B8B6885F75F50B2A280BFCBF849D8710C8E61D369C533ACF307EDA064"

die() {
    echo "build-initramfs: $*" >&2
    exit 1
}

sha256_of() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        die "need sha256sum or shasum to verify prebuilt inputs"
    fi
}

check_sha256() {
    local file=$1 expected=$2 actual
    actual=$(sha256_of "$file" | tr '[:lower:]' '[:upper:]')
    [ "$actual" = "$expected" ] || die "SHA-256 mismatch for $file: got $actual, expected $expected"
}

command -v python3 >/dev/null 2>&1 || die "python3 is required"
[ -f "$RAMDISK" ] || die "missing $RAMDISK"
[ -f "$BUSYBOX" ] || die "missing $BUSYBOX"
[ -d "$OVERLAY" ] || die "missing $OVERLAY"
[ -d "$VENDOR" ] || die "missing $VENDOR"

check_sha256 "$RAMDISK" "$RAMDISK_SHA256"
check_sha256 "$BUSYBOX" "$BUSYBOX_SHA256"

echo "==> unpacking Android ${ANDROID_VERSION} ramdisk"
rm -rf "$STAGE"
mkdir -p "$STAGE"
python3 "$ROOT/scripts/unpack_cpio.py" "$RAMDISK" "$STAGE"

# The Vita never runs on goldfish/ranchu.  Removing these also prevents a
# stale fstab or ueventd rule from being picked up by accident.
rm -f \
    "$STAGE/init.goldfish.rc" \
    "$STAGE/init.ranchu.rc" \
    "$STAGE/ueventd.goldfish.rc" \
    "$STAGE/ueventd.ranchu.rc" \
    "$STAGE/fstab.goldfish" \
    "$STAGE/fstab.ranchu"

[ -x "$STAGE/init" ] || die "Android init missing from ramdisk"
[ -x "$STAGE/sbin/adbd" ] || die "adbd missing from ramdisk"

echo "==> adding Vita overlay for Android ${ANDROID_VERSION}"
cp -a "$OVERLAY/." "$STAGE/"

# A Git checkout on Windows does not preserve execute bits.  Restore them here
# so the checks below do not fail on an otherwise correct tree.
chmod 0755 "$STAGE/init.vita" "$STAGE/vita-log.sh" "$STAGE/vita-net.sh" "$STAGE/vita-ui.sh" 2>/dev/null || true

echo "==> installing busybox"
mkdir -p "$STAGE/sbin" "$STAGE/bin"
cp "$BUSYBOX" "$STAGE/sbin/busybox"
chmod 0755 "$STAGE/sbin/busybox"
# /init.vita has a #!/bin/sh shebang, so this must exist before the script
# runs.  busybox --install -s /bin adds the rest of the applets at boot.
ln -sfn /sbin/busybox "$STAGE/bin/sh"

echo "==> installing glibc Wi-Fi userspace"
mkdir -p "$STAGE/vita-linux" "$STAGE/vita-linux/lib" "$STAGE/vita-linux/usr/lib"
cp -a "$VENDOR/." "$STAGE/vita-linux/"
rm -f "$STAGE/vita-linux/README.md"
chmod 0755 \
    "$STAGE/vita-linux/usr/sbin/wpa_supplicant" \
    "$STAGE/vita-linux/lib/ld-linux-armhf.so.3" 2>/dev/null || true
ln -sfn libatomic.so.1.2.0 "$STAGE/vita-linux/lib/libatomic.so.1"
ln -sfn libnl-3.so.200.26.0 "$STAGE/vita-linux/usr/lib/libnl-3.so.200"
ln -sfn libnl-genl-3.so.200.26.0 "$STAGE/vita-linux/usr/lib/libnl-genl-3.so.200"

# The kernel firmware loader only looks in /lib/firmware, not in the private
# /vita-linux tree.  Keep a copy there for mwifiex and cfg80211.
mkdir -p "$STAGE/lib"
cp -a "$VENDOR/lib/firmware" "$STAGE/lib/"

echo "==> checking required files"
for path in \
    "$STAGE/init" \
    "$STAGE/init.vita" \
    "$STAGE/init.vita.rc" \
    "$STAGE/fstab.vita" \
    "$STAGE/ueventd.vita.rc" \
    "$STAGE/default.prop" \
    "$STAGE/vita-log.sh" \
    "$STAGE/bin/sh" \
    "$STAGE/sbin/busybox" \
    "$STAGE/sbin/adbd" \
    "$STAGE/sbin/healthd" \
    "$STAGE/sbin/ueventd" \
    "$STAGE/sepolicy" \
    "$STAGE/property_contexts" \
    "$STAGE/usr/keylayout/PlayStation_Vita_Buttons__Syscon_.kl" \
    "$STAGE/usr/idc/PlayStation_Vita_Touchscreen__Syscon_.idc" \
    "$STAGE/vita-linux/usr/sbin/wpa_supplicant" \
    "$STAGE/vita-linux/lib/ld-linux-armhf.so.3" \
    "$STAGE/vita-linux/usr/lib/libssl.so.3" \
    "$STAGE/vita-linux/usr/lib/libcrypto.so.3" \
    "$STAGE/lib/firmware/mrvl/sd8787_uapsta.bin" \
    "$STAGE/lib/firmware/regulatory.db"; do
    # Use -L as well: bin/sh is an absolute symlink to /sbin/busybox, and -e
    # on the build host would follow it into the host's own root filesystem.
    [ -e "$path" ] || [ -L "$path" ] || die "missing required initramfs file: ${path#"$STAGE"/}"
done

[ -x "$STAGE/init.vita" ] || die "init.vita is not executable"
[ -x "$STAGE/vita-log.sh" ] || die "vita-log.sh is not executable"
[ -x "$STAGE/vita-net.sh" ] || die "vita-net.sh is not executable"
[ -x "$STAGE/vita-ui.sh" ] || die "vita-ui.sh is not executable"
[ -x "$STAGE/vita-linux/usr/sbin/wpa_supplicant" ] || die "wpa_supplicant is not executable"

echo "==> packing newc cpio"
mkdir -p "$(dirname "$OUT")"
python3 "$ROOT/scripts/pack_cpio.py" "$STAGE" "$OUT"

echo
echo "initramfs: $OUT"
ls -lh "$OUT"
echo "sha256: $(sha256_of "$OUT")"
