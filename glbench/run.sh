#!/system/bin/sh
# Run the bundled glbench on the Vita.
#
# Everything (glibc, Mesa, the DRI driver) is inside this directory, so the
# Android system libraries are never used.  Invoke it from /data:
#
#   cd /data/glbench
#   sh run.sh
#   sh run.sh --scene ui --ui-frames 30
#
# Android 5.1's toolbox has no dirname, and adb's PATH does not include /bin
# where the initramfs installs the busybox applets, so resolve the directory
# with shell parameter expansion instead of calling dirname.
set -u

case "$0" in
    */*) DIR=${0%/*} ;;
    *) DIR=. ;;
esac
case "$DIR" in
    /*) ;;
    .) DIR="$(pwd)" ;;
    *) DIR="$(pwd)/$DIR" ;;
esac

# adb push usually leaves new files at 0644; the loader and the binary need
# the execute bit.  Ignore failure here and report it with the checks below.
chmod 755 "$DIR/lib/ld-linux-armhf.so.3" "$DIR/glbench" 2>/dev/null

if [ ! -x "$DIR/lib/ld-linux-armhf.so.3" ]; then
    echo "glbench: missing or not executable: $DIR/lib/ld-linux-armhf.so.3" >&2
    echo "glbench: expected bundle layout:" >&2
    ls -l "$DIR" "$DIR/lib" "$DIR/dri" 1>&2
    exit 1
fi
if [ ! -x "$DIR/glbench" ]; then
    echo "glbench: missing or not executable: $DIR/glbench" >&2
    ls -l "$DIR" 1>&2
    exit 1
fi

# softpipe is the portable baseline.  Set GALLIUM_DRIVER=llvmpipe only if the
# bundle was built with an LLVM-enabled Mesa.
GALLIUM_DRIVER=${GALLIUM_DRIVER:-softpipe}
LIBGL_ALWAYS_SOFTWARE=1
LIBGL_DRIVERS_PATH="$DIR/dri"
LD_LIBRARY_PATH="$DIR/lib:$DIR/dri"
MESA_LOADER_DRIVER_OVERRIDE=${MESA_LOADER_DRIVER_OVERRIDE:-swrast}
export GALLIUM_DRIVER LIBGL_ALWAYS_SOFTWARE LIBGL_DRIVERS_PATH
export LD_LIBRARY_PATH MESA_LOADER_DRIVER_OVERRIDE

exec "$DIR/lib/ld-linux-armhf.so.3" \
    --inhibit-cache \
    --library-path "$DIR/lib:$DIR/dri" \
    "$DIR/glbench" "$@"
