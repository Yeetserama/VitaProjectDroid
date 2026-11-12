#!/usr/bin/env bash
# Download the Android 2.3.7 (Gingerbread, API 10) SDK system image for
# armeabi-v7a and prepare the files the Vita build needs.
#
# Source: https://dl.google.com/android/repository/sys-img/android/
#         armeabi-v7a-10_r05.zip
#
# Outputs:
#   prebuilt/ramdisk-2.3.img    stock ramdisk, embedded by build-initramfs.sh
#   prebuilt/system-2.3.img     copy to ur0:/android/system.img on the Vita
#   prebuilt/userdata-2.3.img   copy to ur0:/android/userdata.img on the Vita
#
# The SDK ships a 2 GiB userdata.img, which does not fit on ur0 next to the
# 550 MiB system image.  This script creates a fresh 256 MiB ext4 image
# instead; the framework builds /data on first boot.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK_DIR="$ROOT/downloads/android-10"
OUT_DIR="$ROOT/prebuilt"
ZIP="$SDK_DIR/armeabi-v7a-10_r05.zip"
ZIP_URL="https://dl.google.com/android/repository/sys-img/android/armeabi-v7a-10_r05.zip"
ZIP_SIZE=67927049
ZIP_SHA1="8537616a7add47cce24c60f18bc2429e3dc90ae3"
USERDATA_MB=256

mkdir -p "$SDK_DIR" "$OUT_DIR"

die() {
    echo "download-android-2.3: $*" >&2
    exit 1
}

sha1_of() {
    if command -v sha1sum >/dev/null 2>&1; then
        sha1sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 1 "$1" | awk '{print $1}'
    else
        python3 - "$1" <<'EOF'
import hashlib, sys
h = hashlib.sha1()
with open(sys.argv[1], "rb") as f:
    for chunk in iter(lambda: f.read(1 << 20), b""):
        h.update(chunk)
print(h.hexdigest())
EOF
    fi
}

if [ -f "$ZIP" ]; then
    actual=$(sha1_of "$ZIP")
    if [ "$actual" != "$ZIP_SHA1" ]; then
        echo "==> cached archive has SHA1 $actual, expected $ZIP_SHA1; re-downloading"
        rm -f "$ZIP"
    fi
fi

if [ ! -f "$ZIP" ]; then
    echo "==> downloading $ZIP_URL"
    if command -v curl >/dev/null 2>&1; then
        curl -fL --retry 5 --retry-delay 3 -o "$ZIP.part" "$ZIP_URL"
    elif command -v wget >/dev/null 2>&1; then
        wget -O "$ZIP.part" "$ZIP_URL"
    else
        die "neither curl nor wget is available"
    fi
    mv "$ZIP.part" "$ZIP"
fi

actual_size=$(wc -c < "$ZIP" | tr -d ' ')
[ "$actual_size" = "$ZIP_SIZE" ] || die "archive is $actual_size bytes, expected $ZIP_SIZE"
actual_sha1=$(sha1_of "$ZIP")
[ "$actual_sha1" = "$ZIP_SHA1" ] || die "archive SHA1 is $actual_sha1, expected $ZIP_SHA1"
echo "==> verified archive: $ZIP_SIZE bytes, SHA1 $ZIP_SHA1"

echo "==> extracting ramdisk.img, system.img and build.prop"
python3 - "$ZIP" "$SDK_DIR/extract" <<'EOF'
import os
import sys
import zipfile

archive, dest = sys.argv[1], sys.argv[2]
wanted = ("armeabi-v7a/ramdisk.img", "armeabi-v7a/system.img",
          "armeabi-v7a/build.prop", "armeabi-v7a/source.properties")
os.makedirs(dest, exist_ok=True)
with zipfile.ZipFile(archive) as z:
    names = set(z.namelist())
    for name in wanted:
        if name not in names:
            raise SystemExit(f"{archive}: missing {name}")
        out = os.path.join(dest, os.path.basename(name))
        with z.open(name) as src, open(out, "wb") as dst:
            while True:
                chunk = src.read(1 << 20)
                if not chunk:
                    break
                dst.write(chunk)
        print(f"  {name} -> {out} ({os.path.getsize(out)} bytes)")
EOF

EXTRACT="$SDK_DIR/extract"

# Sanity-check that this really is the Gingerbread image before copying 550 MiB.
grep -q '^ro.build.version.sdk=10$' "$EXTRACT/build.prop" \
    || die "$EXTRACT/build.prop is not SDK 10"
grep -q '^ro.build.version.release=2\.3' "$EXTRACT/build.prop" \
    || die "$EXTRACT/build.prop is not Android 2.3.x"
python3 - "$EXTRACT/system.img" "$EXTRACT/userdata.img" <<'EOF'
import sys

# ext4 superblock magic lives at 1024 + 0x38.
for path in sys.argv[1:]:
    with open(path, "rb") as f:
        f.seek(1024 + 0x38)
        magic = f.read(2)
    if magic != b"\x53\xef":
        raise SystemExit(f"{path}: not an ext2/3/4 filesystem (magic {magic!r})")
    print(f"  {path}: ext filesystem")
EOF

echo "==> installing ramdisk as $OUT_DIR/ramdisk-2.3.img"
cp "$EXTRACT/ramdisk.img" "$OUT_DIR/ramdisk-2.3.img"

echo "==> installing system image as $OUT_DIR/system-2.3.img"
cp "$EXTRACT/system.img" "$OUT_DIR/system-2.3.img"

USERDATA="$OUT_DIR/userdata-2.3.img"
if command -v mke2fs >/dev/null 2>&1 && command -v truncate >/dev/null 2>&1; then
    # A fresh image avoids rewriting the 2 GiB SDK blob and gives the Vita a
    # small /data.  Gingerbread creates its directory tree on first boot.
    echo "==> creating a fresh ${USERDATA_MB} MiB ext4 userdata image"
    rm -f "$USERDATA"
    truncate -s "${USERDATA_MB}M" "$USERDATA"
    mke2fs -q -t ext4 -F -L userdata "$USERDATA" \
        || die "mke2fs failed (install e2fsprogs)"
elif command -v resize2fs >/dev/null 2>&1 && command -v truncate >/dev/null 2>&1; then
    echo "==> shrinking SDK userdata.img to ${USERDATA_MB} MiB"
    cp "$EXTRACT/userdata.img" "$USERDATA"
    e2fsck -f -y "$USERDATA" >/dev/null 2>&1 || true
    resize2fs "$USERDATA" "${USERDATA_MB}M"
    truncate -s "${USERDATA_MB}M" "$USERDATA"
    e2fsck -f -y "$USERDATA" || true
else
    die "mke2fs/resize2fs not found; install e2fsprogs to create userdata-2.3.img"
fi

echo "==> verifying outputs"
ls -l "$OUT_DIR/ramdisk-2.3.img" "$OUT_DIR/system-2.3.img" "$USERDATA"
python3 - "$USERDATA" <<'EOF'
import os, sys
path = sys.argv[1]
want = 256 * 1024 * 1024
have = os.path.getsize(path)
if have != want:
    raise SystemExit(f"{path}: {have} bytes, expected {want}")
with open(path, "rb") as f:
    f.seek(1024 + 0x38)
    if f.read(2) != b"\x53\xef":
        raise SystemExit(f"{path}: not an ext filesystem")
print(f"  {path}: {have} bytes, ext filesystem")
EOF

echo ""
echo "Done. Copy these to the Vita (ur0:/android/):"
echo "  $OUT_DIR/system-2.3.img    -> ur0:/android/system.img"
echo "  $USERDATA                  -> ur0:/android/userdata.img"
echo ""
echo "Then build the boot package:"
echo "  ANDROID_VERSION=2.3 scripts/build.sh"
