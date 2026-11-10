#!/usr/bin/env bash
# Build a self-contained armhf glbench bundle for the PS Vita.
#
# The bundle contains:
#   - Mesa 24.0.9 built with softpipe only (no LLVM, no zlib, no glvnd),
#   - the glbench EGL/GLES2 benchmark,
#   - an armhf glibc runtime and libdrm.
#
# Everything is cross-compiled with arm-linux-gnueabihf and is independent
# of the Vita's Android/bionic userspace.  Run it on the device with
# `sh run.sh` from the bundle directory.
#
# Usage:
#   scripts/build-glbench.sh [OUTPUT_DIR]
#
# Environment:
#   MESA_VER, LIBDRM_VER, JOBS, CROSS
#
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-"$ROOT/build/glbench"}
MESA_VER=${MESA_VER:-24.0.9}
LIBDRM_VER=${LIBDRM_VER:-2.4.120}
CROSS=${CROSS:-arm-linux-gnueabihf}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

DL="$OUT/downloads"
WORK="$OUT/work"
DEPS="$OUT/deps"
MESA_PREFIX="$OUT/mesa"
BUNDLE="$OUT/bundle"
CROSS_FILE="$ROOT/scripts/mesa-armhf.ini"

MESA_URL="https://archive.mesa3d.org/mesa-$MESA_VER.tar.xz"
LIBDRM_URL="https://dri.freedesktop.org/libdrm/libdrm-$LIBDRM_VER.tar.xz"

log() { printf '\n==> %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "missing tool: $1" >&2; exit 1; }; }

for t in curl tar meson ninja pkg-config "$CROSS-gcc" "$CROSS-g++" \
         "$CROSS-ar" "$CROSS-readelf" sha256sum; do
    need "$t"
done

rm -rf "$WORK" "$DEPS" "$MESA_PREFIX" "$BUNDLE"
mkdir -p "$DL" "$WORK" "$BUNDLE/lib" "$BUNDLE/dri"

fetch() {
    local url=$1 file=$2
    if [ ! -s "$DL/$file" ]; then
        log "downloading $url"
        curl --fail --location --retry 3 --output "$DL/$file" "$url"
    fi
}

log "sources"
fetch "$MESA_URL" "mesa-$MESA_VER.tar.xz"
fetch "$LIBDRM_URL" "libdrm-$LIBDRM_VER.tar.xz"
tar -C "$WORK" -xf "$DL/mesa-$MESA_VER.tar.xz"
tar -C "$WORK" -xf "$DL/libdrm-$LIBDRM_VER.tar.xz"

log "libdrm $LIBDRM_VER (armhf)"
meson setup "$WORK/libdrm-build" "$WORK/libdrm-$LIBDRM_VER" \
    --cross-file "$CROSS_FILE" \
    --prefix "$DEPS" --libdir lib \
    --buildtype release --wrap-mode nodownload \
    -Dtests=false \
    -Dudev=false \
    -Dvalgrind=disabled \
    -Dman-pages=disabled \
    -Dcairo-tests=disabled \
    -Dinstall-test-programs=false \
    -Dintel=disabled \
    -Dradeon=disabled \
    -Damdgpu=disabled \
    -Dnouveau=disabled \
    -Dvmwgfx=disabled \
    -Domap=disabled \
    -Dexynos=disabled \
    -Dfreedreno=disabled \
    -Dtegra=disabled \
    -Detnaviv=disabled
ninja -C "$WORK/libdrm-build" -j "$JOBS" install

log "Mesa $MESA_VER (softpipe only, surfaceless EGL)"
export PKG_CONFIG_LIBDIR="$DEPS/lib/pkgconfig"
meson setup "$WORK/mesa-build" "$WORK/mesa-$MESA_VER" \
    --cross-file "$CROSS_FILE" \
    --prefix "$MESA_PREFIX" --libdir lib \
    --buildtype release --wrap-mode nodownload \
    -Dplatforms= \
    -Degl-native-platform=surfaceless \
    -Dgallium-drivers=swrast \
    -Dvulkan-drivers= \
    -Dglx=disabled \
    -Dgbm=disabled \
    -Dllvm=disabled \
    -Dgles1=disabled \
    -Dgles2=enabled \
    -Dopengl=false \
    -Dshared-glapi=enabled \
    -Dglvnd=false \
    -Dzlib=disabled \
    -Dzstd=disabled \
    -Dexpat=disabled \
    -Dxmlconfig=disabled \
    -Dshader-cache=disabled \
    -Dbuild-tests=false \
    -Dtools= \
    -Dosmesa=false
ninja -C "$WORK/mesa-build" -j "$JOBS" install

log "glbench"
"$CROSS-gcc" -std=gnu11 -O2 -g \
    -march=armv7-a -mtune=cortex-a9 -mfpu=neon -mfloat-abi=hard \
    -I"$WORK/mesa-$MESA_VER/include" \
    "$ROOT/glbench/glbench.c" -o "$BUNDLE/glbench" \
    -L"$MESA_PREFIX/lib" -L"$DEPS/lib" \
    -Wl,-rpath-link,"$MESA_PREFIX/lib:$DEPS/lib" \
    -l:libEGL.so.1 -l:libGLESv2.so.2 -lm

log "runtime libraries"
copy_runtime() {
    local soname=$1 path
    [ -e "$BUNDLE/lib/$soname" ] && return 0
    path=$("$CROSS-gcc" -print-file-name="$soname")
    if [ -f "$path" ] && [ "$path" != "$soname" ]; then
        cp -L "$path" "$BUNDLE/lib/$soname"
        printf '  %-24s <- %s\n' "$soname" "$path"
    else
        echo "ERROR: $soname is not in the cross sysroot" >&2
        return 1
    fi
}

# Copy the runtime sonames as regular files (no symlinks): Windows tar
# extracts symlinks as text files, which would break the bundle on this PC.
for lib in libEGL.so.1 libGLESv2.so.2 libglapi.so.0; do
    if [ -e "$MESA_PREFIX/lib/$lib" ]; then
        cp -L "$MESA_PREFIX/lib/$lib" "$BUNDLE/lib/$lib"
        printf '  %-24s <- Mesa\n' "$lib"
    fi
done
cp -L "$MESA_PREFIX/lib/dri/"* "$BUNDLE/dri/"
cp -L "$DEPS/lib/libdrm.so.2" "$BUNDLE/lib/libdrm.so.2"
printf '  %-24s <- libdrm\n' "libdrm.so.2"

copy_runtime ld-linux-armhf.so.3
copy_runtime libc.so.6
copy_runtime libm.so.6

needed() {
    "$CROSS-readelf" -d "$1" 2>/dev/null |
        sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'
}

log "resolving dynamic dependencies"
for f in "$BUNDLE/glbench" "$BUNDLE/lib/"*.so.* "$BUNDLE/dri/"*; do
    [ -f "$f" ] || continue
    while IFS= read -r soname; do
        [ -n "$soname" ] || continue
        copy_runtime "$soname"
    done <<EOF
$(needed "$f")
EOF
done

for f in "$BUNDLE/glbench" "$BUNDLE/lib/"*.so.* "$BUNDLE/dri/"*; do
    [ -f "$f" ] || continue
    while IFS= read -r soname; do
        [ -n "$soname" ] || continue
        if [ ! -e "$BUNDLE/lib/$soname" ]; then
            echo "ERROR: $(basename "$f") needs $soname, not in the bundle" >&2
            exit 1
        fi
    done <<EOF
$(needed "$f")
EOF
done

log "bundle metadata"
sed -i 's/\r$//' "$ROOT/glbench/run.sh"
cp "$ROOT/glbench/run.sh" "$BUNDLE/run.sh"
chmod 0755 "$BUNDLE/run.sh" "$BUNDLE/glbench"

cat > "$BUNDLE/README.txt" <<EOF
glbench -- software GLES2 performance gate for the PS Vita

Built:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')
Commit:  $(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo unknown)
Mesa:    $MESA_VER (softpipe only, surfaceless EGL, no LLVM)
libdrm:  $LIBDRM_VER
Target:  armv7 hard-float glibc, 960x544

Install:
  adb push glbench /data/glbench
  adb shell chmod 755 /data/glbench/run.sh

Run:
  adb shell
  cd /data/glbench
  sh run.sh

Useful variants:
  sh run.sh --scene ui --ui-frames 30
  sh run.sh --no-present
  sh run.sh --json
  GALLIUM_DRIVER=llvmpipe sh run.sh   # only if built with LLVM

The benchmark renders offscreen, then presents one frame to
/dev/graphics/fb0 (RGBA readback, rows flipped, straight RGBA copy).  Use
--fb-format bgra if red and blue appear exchanged.  The fb0 path is skipped
automatically if the device is unavailable.
EOF

(cd "$BUNDLE" && find . -type f ! -name SHA256SUMS -print0 |
    sort -z | xargs -0 sha256sum > SHA256SUMS)

log "package"
(cd "$OUT" && tar -czf glbench-vita-armhf.tar.gz bundle)
(cd "$OUT" && sha256sum glbench-vita-armhf.tar.gz > SHA256SUMS)

log "done"
du -sh "$BUNDLE" "$OUT/glbench-vita-armhf.tar.gz"
echo
cat "$OUT/SHA256SUMS"
echo
echo "bundle contents:"
(cd "$BUNDLE" && find . -maxdepth 2 -type f -printf '  %-42p %8s\n' | sort)
