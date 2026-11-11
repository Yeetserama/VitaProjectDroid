#!/usr/bin/env bash
# Download the Android 4.2.2 (API 17) SDK system image for armeabi-v7a.
#
# This downloads the ramdisk.img, system.img, and userdata.img from Google's
# SDK repository. The ramdisk.img is used by build-initramfs.sh and must be
# placed at prebuilt/ramdisk-4.2.img. The system.img and userdata.img are
# copied to ur0:/android/ on the Vita.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK_DIR="$ROOT/downloads/android-17"
OUT_DIR="$ROOT/prebuilt"

mkdir -p "$SDK_DIR" "$OUT_DIR"

# Android 4.2.2 (API 17) ARM EABI v7a System Image
# Package: system-images;android-17;default;armeabi-v7a
# Using sdkmanager from Android SDK command line tools.

if command -v sdkmanager >/dev/null 2>&1; then
    echo "==> Found sdkmanager, installing system-images;android-17;default;armeabi-v7a"
    sdkmanager "system-images;android-17;default;armeabi-v7a"
    SDK_PATH="$ANDROID_HOME/system-images/android-17/default/armeabi-v7a"
elif [ -d "$ANDROID_HOME/system-images/android-17/default/armeabi-v7a" ]; then
    echo "==> Found existing Android 4.2 SDK image at $ANDROID_HOME"
    SDK_PATH="$ANDROID_HOME/system-images/android-17/default/armeabi-v7a"
else
    echo "==> sdkmanager not found. Please install Android SDK command line tools"
    echo "    and run: sdkmanager \"system-images;android-17;default;armeabi-v7a\""
    echo ""
    echo "    Then run this script again, or manually copy:"
    echo "      \$ANDROID_HOME/system-images/android-17/default/armeabi-v7a/ramdisk.img -> $OUT_DIR/ramdisk-4.2.img"
    echo "      \$ANDROID_HOME/system-images/android-17/default/armeabi-v7a/system.img -> ur0:/android/system.img"
    echo "      \$ANDROID_HOME/system-images/android-17/default/armeabi-v7a/userdata.img -> ur0:/android/userdata.img"
    exit 1
fi

if [ ! -d "$SDK_PATH" ]; then
    echo "SDK image not found at $SDK_PATH" >&2
    exit 1
fi

echo "==> Copying ramdisk.img to $OUT_DIR/ramdisk-4.2.img"
cp "$SDK_PATH/ramdisk.img" "$OUT_DIR/ramdisk-4.2.img"

echo "==> Verifying ramdisk.img"
file "$OUT_DIR/ramdisk-4.2.img"

echo ""
echo "Done. The following files are ready:"
echo "  $OUT_DIR/ramdisk-4.2.img (for initramfs build)"
echo "  $SDK_PATH/system.img (copy to ur0:/android/system.img on Vita)"
echo "  $SDK_PATH/userdata.img (copy to ur0:/android/userdata.img on Vita)"
echo ""
echo "To build Android 4.2:"
echo "  ANDROID_VERSION=4.2 scripts/build.sh"