#!/usr/bin/env bash
# Download the Android 2.2 (Froyo, API 8) SDK system image for armeabi-v7a.
#
# Note: Android 2.2 is very old and may not be available via sdkmanager anymore.
# You may need to download from alternative sources or use a prebuilt image.
#
# This downloads the ramdisk.img, system.img, and userdata.img.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK_DIR="$ROOT/downloads/android-8"
OUT_DIR="$ROOT/prebuilt"

mkdir -p "$SDK_DIR" "$OUT_DIR"

echo "=========================================="
echo "Android 2.2 (Froyo) SDK Image Download"
echo "=========================================="
echo ""
echo "WARNING: Android 2.2 (API 8) is very old and the official SDK images"
echo "may no longer be available via sdkmanager."
echo ""
echo "Options:"
echo "1. Try sdkmanager (may fail):"
echo "   sdkmanager \"system-images;android-8;default;armeabi-v7a\""
echo ""
echo "2. Download from archive.org or other mirrors"
echo ""
echo "3. Use a prebuilt system.img from a Froyo device port"
echo ""
echo "Required files (place in $OUT_DIR/):"
echo "  ramdisk-2.2.img    -> $OUT_DIR/ramdisk-2.2.img"
echo "  system.img         -> copy to ur0:/android/system.img on Vita"
echo "  userdata.img       -> copy to ur0:/android/userdata.img on Vita"
echo ""

if command -v sdkmanager >/dev/null 2>&1; then
    echo "==> Found sdkmanager, attempting to install system-images;android-8;default;armeabi-v7a"
    if sdkmanager "system-images;android-8;default;armeabi-v7a"; then
        SDK_PATH="$ANDROID_HOME/system-images/android-8/default/armeabi-v7a"
        if [ -d "$SDK_PATH" ]; then
            echo "==> Copying ramdisk.img to $OUT_DIR/ramdisk-2.2.img"
            cp "$SDK_PATH/ramdisk.img" "$OUT_DIR/ramdisk-2.2.img"
            echo "Done."
        fi
    else
        echo "sdkmanager failed - Android 2.2 images likely unavailable"
    fi
elif [ -d "$ANDROID_HOME/system-images/android-8/default/armeabi-v7a" ]; then
    echo "==> Found existing Android 2.2 SDK image at $ANDROID_HOME"
    SDK_PATH="$ANDROID_HOME/system-images/android-8/default/armeabi-v7a"
    echo "==> Copying ramdisk.img to $OUT_DIR/ramdisk-2.2.img"
    cp "$SDK_PATH/ramdisk.img" "$OUT_DIR/ramdisk-2.2.img"
else
    echo "sdkmanager not found or Android 2.2 image not available."
    echo ""
    echo "You may need to:"
    echo "1. Find a Froyo (2.2) ARMv7 system image from an alternate source"
    echo "2. Extract ramdisk.img, system.img, userdata.img"
    echo "3. Place them manually in $OUT_DIR/ and on the Vita"
fi

echo ""
echo "To build Android 2.2 after obtaining images:"
echo "  ANDROID_VERSION=2.2 scripts/build.sh"