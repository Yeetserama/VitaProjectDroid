#!/usr/bin/env bash
# Download the Android 3.x (Honeycomb, API 11) SDK system image and
# prepare the files the Vita build needs.
#
# Android 3.x system images are distributed as a Google APIs add-on
# (google_apis-11_r01.zip) rather than a standalone sys-img archive.
# The add-on contains images/system.img and images/ramdisk.img.
#
# Source: https://dl.google.com/android/repository/google_apis-11_r01.zip
#
# Outputs:
#   prebuilt/ramdisk-3.img    stock ramdisk, embedded by build-initramfs.sh
#   prebuilt/system-3.img     copy to ur0:/android/system.img on the Vita
#   prebuilt/userdata-3.img   copy to ur0:/android/userdata.img on the Vita
#
# The SDK ships a system image which may be larger than ideal for ur0.
# This script creates a fresh 512 MiB ext4 userdata image instead;
# the framework builds /data on first boot.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK_DIR="$ROOT/downloads/android-11"
OUT_DIR="$ROOT/prebuilt"
ZIP="$SDK_DIR/google_apis-11_r01.zip"
ZIP_URL="https://dl.google.com/android/repository/google_apis-11_r01.zip"
ZIP_SIZE=83477179
ZIP_SHA1="5eab5e81addee9f3576d456d205208314b5146a5"
IMAGE_DIR="google_apis-11_r01/images"
USERDATA_MB=512

mkdir -p "$SDK_DIR" "$OUT_DIR"

die() {
    echo "download-android-3: $*" >&2
    exit 1
}

sha1_of() {
    if command -v sha1sum >/dev/null 2>&1; then
        sha1sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 1 "$1" | awk '{print $1}'
    else
        die "no sha1sum or shasum available"
    fi
}

if [ ! -f "$ZIP" ]; then
    echo "==> downloading $ZIP_URL"
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 3 --retry-delay 2 -o "$ZIP.part" "$ZIP_URL"
    else
        wget -O "$ZIP.part" "$ZIP_URL"
    fi
    mv "$ZIP.part" "$ZIP"
fi

echo "==> verifying archive"
SIZE=$(stat -c %s "$ZIP" 2>/dev/null || wc -c < "$ZIP")
SHA1=$(sha1_of "$ZIP")
echo "    size: $SIZE bytes"
echo "    sha1: $SHA1"
[ "$SIZE" -eq "$ZIP_SIZE" ] || die "size mismatch: expected $ZIP_SIZE, got $SIZE"
[ "$SHA1" = "$ZIP_SHA1" ] || die "sha1 mismatch: expected $ZIP_SHA1, got $SHA1"

# Extract system image and ramdisk
EXTRACT="$SDK_DIR/extract"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
unzip -o "$ZIP" -d "$EXTRACT" >/dev/null

echo "==> installing ramdisk as $OUT_DIR/ramdisk-3.img"
cp "$EXTRACT/$IMAGE_DIR/ramdisk.img" "$OUT_DIR/ramdisk-3.img"

echo "==> installing system image as $OUT_DIR/system-3.img"
cp "$EXTRACT/$IMAGE_DIR/system.img" "$OUT_DIR/system-3.img"

echo "==> creating a fresh ${USERDATA_MB} MiB ext4 userdata image"
dd if=/dev/zero of="$OUT_DIR/userdata-3.img" bs=1M count="$USERDATA_MB" status=none
mkfs.ext4 -F -L userdata "$OUT_DIR/userdata-3.img" >/dev/null

echo "==> verifying outputs"
ls -l "$OUT_DIR/ramdisk-3.img" "$OUT_DIR/system-3.img" "$OUT_DIR/userdata-3.img"

echo
echo "Done. Copy these to the Vita (ur0:/android/):"
echo "  $OUT_DIR/system-3.img    -> ur0:/android/system.img"
echo "  $OUT_DIR/userdata-3.img   -> ur0:/android/userdata.img"
echo
echo "Then build the boot package:"
echo "  ANDROID_VERSION=3 scripts/build.sh"
