#!/usr/bin/env bash
# Download the Android 6.0 (API 23) SDK system image for armeabi-v7a and
# prepare the files the Vita build needs.
#
# Source: https://dl.google.com/android/repository/sys-img/android/
#         armeabi-v7a-23_r06.zip
#
# Outputs:
#   prebuilt/ramdisk-6.0.img    stock ramdisk, embedded by build-initramfs.sh
#   prebuilt/system-6.0.img     shrunk system image, copy to ur0:/android/system.img
#   prebuilt/userdata-6.0.img   fresh 640 MiB ext4 image, copy to ur0:/android/userdata.img
#
# The SDK system image is 1.5 GiB but only about 500 MiB is used.  ur0 has
# roughly 2.5 GiB free, and the boot also creates a 512 MiB swap image, so the
# system image must be shrunk before it is copied to the Vita.  resize2fs -M
# shrinks the filesystem to its minimum size; the file is then truncated to the
# new block count.

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
SDK_DIR="$ROOT/downloads/android-23"
OUT_DIR="$ROOT/prebuilt"
ZIP="$SDK_DIR/armeabi-v7a-23_r06.zip"
ZIP_URL="https://dl.google.com/android/repository/sys-img/android/armeabi-v7a-23_r06.zip"
ZIP_SIZE=238333358
ZIP_SHA1="7cf2ad756e54a3acfd81064b63cb0cb9dff2798d"
USERDATA_MB=640

mkdir -p "$SDK_DIR" "$OUT_DIR"

die() {
    echo "download-android-6.0: $*" >&2
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

# Sanity-check that this really is the Android 6.0 image before touching ur0
# images.
grep -q '^ro.build.version.sdk=23$' "$EXTRACT/build.prop" \
    || die "$EXTRACT/build.prop is not SDK 23"
grep -q '^ro.build.version.release=6\.0$' "$EXTRACT/build.prop" \
    || die "$EXTRACT/build.prop is not Android 6.0"

python3 - "$EXTRACT/system.img" <<'EOF'
import sys

# ext4 superblock magic lives at 1024 + 0x38.
path = sys.argv[1]
with open(path, "rb") as f:
    f.seek(1024 + 0x38)
    magic = f.read(2)
if magic != b"\x53\xef":
    raise SystemExit(f"{path}: not an ext2/3/4 filesystem (magic {magic!r})")
print(f"  {path}: ext filesystem")
EOF

echo "==> installing ramdisk as $OUT_DIR/ramdisk-6.0.img"
cp "$EXTRACT/ramdisk.img" "$OUT_DIR/ramdisk-6.0.img"

command -v resize2fs >/dev/null 2>&1 || die "resize2fs not found (install e2fsprogs)"
command -v e2fsck >/dev/null 2>&1 || die "e2fsck not found (install e2fsprogs)"
command -v dumpe2fs >/dev/null 2>&1 || die "dumpe2fs not found (install e2fsprogs)"
command -v truncate >/dev/null 2>&1 || die "truncate not found (install coreutils)"
command -v mke2fs >/dev/null 2>&1 || die "mke2fs not found (install e2fsprogs)"

SYSTEM="$OUT_DIR/system-6.0.img"
echo "==> shrinking the SDK system image to its minimum size"
cp "$EXTRACT/system.img" "$SYSTEM"
e2fsck -f -y "$SYSTEM" >/dev/null 2>&1 || true
if ! resize2fs -M "$SYSTEM" >/dev/null 2>&1; then
    echo "==> resize2fs -M failed; falling back to a 700 MiB filesystem"
    e2fsck -f -y "$SYSTEM" >/dev/null 2>&1 || true
    resize2fs "$SYSTEM" 700M
fi
e2fsck -f -y "$SYSTEM" >/dev/null 2>&1 || true

blocks=$(dumpe2fs -h "$SYSTEM" 2>/dev/null \
    | awk -F: '/^Block count:/ { gsub(/[ \t]/, "", $2); print $2; exit }')
block_size=$(dumpe2fs -h "$SYSTEM" 2>/dev/null \
    | awk -F: '/^Block size:/ { gsub(/[ \t]/, "", $2); print $2; exit }')
[ -n "$blocks" ] || die "could not read the system image block count"
[ -n "$block_size" ] || die "could not read the system image block size"
SYSTEM_BYTES=$((blocks * block_size))
[ "$SYSTEM_BYTES" -gt 0 ] || die "invalid system image size"
truncate -s "$SYSTEM_BYTES" "$SYSTEM"
e2fsck -f -y "$SYSTEM" >/dev/null 2>&1 || true
chmod 0644 "$SYSTEM"

SYSTEM_MB=$((SYSTEM_BYTES / 1048576))
echo "  $SYSTEM: ${SYSTEM_MB} MiB"
[ "$SYSTEM_MB" -le 900 ] \
    || die "shrunk system image is ${SYSTEM_MB} MiB; expected at most 900 MiB"

USERDATA="$OUT_DIR/userdata-6.0.img"
echo "==> creating a fresh ${USERDATA_MB} MiB ext4 userdata image"
rm -f "$USERDATA"
truncate -s "${USERDATA_MB}M" "$USERDATA"
mke2fs -q -t ext4 -F -m 0 -L userdata "$USERDATA"
chmod 0644 "$USERDATA"

echo "==> verifying outputs"
ls -l "$OUT_DIR/ramdisk-6.0.img" "$SYSTEM" "$USERDATA"
python3 - "$USERDATA" <<'EOF'
import os, sys
path = sys.argv[1]
want = 640 * 1024 * 1024
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
echo "  $SYSTEM    -> ur0:/android/system.img"
echo "  $USERDATA  -> ur0:/android/userdata.img"
echo ""
echo "Then build the boot package:"
echo "  ANDROID_VERSION=6.0 scripts/build.sh"
