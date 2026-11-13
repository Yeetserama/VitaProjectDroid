#!/usr/bin/env bash
# Build the AndroidOnVita boot package.
#
# Steps:
#   1. fetch the pinned LinuxOnVita 6.12 tree (unless KERNEL_SRC is set),
#   2. assemble the Android initramfs,
#   3. add ashmem + the Vita Android config, build zImage and vita1000.dtb,
#   4. patch the DTB bootargs and collect everything in build/out.
#
# Requirements: bash, python3, make, gcc, bc, bison, flex, libssl-dev,
# libelf-dev, device-tree-compiler, and an ARM cross compiler
# (arm-linux-gnueabihf-gcc).
#
# Usage:
#   scripts/build.sh [ANDROID_VERSION]
#   ANDROID_VERSION=6.0 scripts/build.sh
#   KERNEL_SRC=/path/to/linux_vita scripts/build.sh
#   CLEAN=1 scripts/build.sh
#
# ANDROID_VERSION is 5.1 (default) or 6.0.
#
# Artifacts (5.1):
#   build/out/zImage
#   build/out/vita.dtb                     (permissive SELinux bootargs)
#   build/out/vita-selinux-disabled.dtb    (fallback)
#   build/out/vita1000.dtb                 (unpatched, for reference)
#   build/out/initramfs.cpio
#   build/out/SHA256SUMS
#   build/out/build-info.txt
#
# Artifacts (6.0) use the -6.0 suffix:
#   build/out/zImage-6.0
#   build/out/vita.dtb-6.0
#   build/out/vita-selinux-disabled.dtb-6.0
#   build/out/vita1000.dtb-6.0
#   build/out/initramfs.cpio-6.0
#   build/out/SHA256SUMS-6.0
#   build/out/build-info.txt-6.0

set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$ROOT/build"}
KERNEL_REF="a33b819fd7174f60b1d5bcb613de565b3f046687"
KERNEL_URL="https://github.com/incognitojam/linux_vita/archive/${KERNEL_REF}.tar.gz"
KERNEL_SRC=${KERNEL_SRC:-"$BUILD_DIR/linux_vita"}
# Priority: environment variable > argument > default.
ANDROID_VERSION=${ANDROID_VERSION:-${1:-"5.1"}}

echo "build.sh: ANDROID_VERSION=$ANDROID_VERSION"
echo "build.sh: ROOT=$ROOT"
echo "build.sh: BUILD_DIR=$BUILD_DIR"

case "$ANDROID_VERSION" in
    5.1)
        INITRAMFS="$BUILD_DIR/initramfs.cpio"
        STAGE="$BUILD_DIR/initramfs-root"
        OUT="$BUILD_DIR/out"
        ;;
    6.0)
        INITRAMFS="$BUILD_DIR/initramfs.cpio-6.0"
        STAGE="$BUILD_DIR/initramfs-root-6.0"
        OUT="$BUILD_DIR/out"
        ;;
    *)
        echo "build: unknown Android version: $ANDROID_VERSION" >&2
        exit 1
        ;;
esac

die() {
    echo "build: $*" >&2
    exit 1
}

need() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

need python3
need make
need tar
need fdtput
if ! command -v curl >/dev/null 2>&1 && ! command -v wget >/dev/null 2>&1; then
    die "required command not found: curl or wget"
fi

if [ "${CLEAN:-0}" = "1" ]; then
    echo "==> clean"
    case "$KERNEL_SRC" in
        "$BUILD_DIR"/*) rm -rf "$KERNEL_SRC" ;;
        *) die "refusing to remove KERNEL_SRC outside $BUILD_DIR: $KERNEL_SRC" ;;
    esac
    rm -rf "$OUT" "$STAGE" "$INITRAMFS"
fi

mkdir -p "$BUILD_DIR"

if [ ! -f "$KERNEL_SRC/Makefile" ]; then
    echo "==> fetching LinuxOnVita kernel $KERNEL_REF"
    mkdir -p "$BUILD_DIR/downloads"
    TARBALL="$BUILD_DIR/downloads/linux_vita-${KERNEL_REF}.tar.gz"
    if [ ! -f "$TARBALL" ]; then
        if command -v curl >/dev/null 2>&1; then
            curl -fL --retry 3 --retry-delay 2 -o "$TARBALL.part" "$KERNEL_URL"
        else
            wget -O "$TARBALL.part" "$KERNEL_URL"
        fi
        mv "$TARBALL.part" "$TARBALL"
    fi

    EXTRACT="$BUILD_DIR/kernel-extract"
    rm -rf "$EXTRACT"
    mkdir -p "$EXTRACT"
    tar -xzf "$TARBALL" -C "$EXTRACT"
    TOP=$(find "$EXTRACT" -maxdepth 1 -mindepth 1 -type d | head -n 1)
    [ -n "$TOP" ] || die "could not find the extracted kernel directory"
    [ -f "$TOP/Makefile" ] || die "extracted archive does not contain a kernel Makefile"
    rm -rf "$KERNEL_SRC"
    mv "$TOP" "$KERNEL_SRC"
    rmdir "$EXTRACT" 2>/dev/null || true
else
    echo "==> using existing kernel tree: $KERNEL_SRC"
fi

# Invoke through bash explicitly: a Windows Git checkout stores 100644 modes,
# and relying on the kernel shebang would fail there.
echo "==> building initramfs for Android ${ANDROID_VERSION}"
bash "$ROOT/scripts/build-initramfs.sh" "$INITRAMFS" "$STAGE" "$ANDROID_VERSION"

echo "==> building kernel for Android ${ANDROID_VERSION}"
bash "$ROOT/scripts/apply-kernel.sh" "$KERNEL_SRC" "$INITRAMFS" "$OUT" "$ANDROID_VERSION"

echo
echo "boot package for Android ${ANDROID_VERSION} ready in $OUT"
