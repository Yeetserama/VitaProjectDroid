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
set -u

DIR=$(dirname "$0")
case "$DIR" in
    /*) ;;
    *) DIR="$(pwd)/$DIR" ;;
esac

# softpipe is the portable baseline.  Set GALLIUM_DRIVER=llvmpipe only if the
# bundle was built with an LLVM-enabled Mesa.
GALLIUM_DRIVER=${GALLIUM_DRIVER:-softpipe}
LIBGL_ALWAYS_SOFTWARE=1
LIBGL_DRIVERS_PATH="$DIR/dri"
MESA_LOADER_DRIVER_OVERRIDE=${MESA_LOADER_DRIVER_OVERRIDE:-swrast}
export GALLIUM_DRIVER LIBGL_ALWAYS_SOFTWARE LIBGL_DRIVERS_PATH
export MESA_LOADER_DRIVER_OVERRIDE

if [ ! -x "$DIR/lib/ld-linux-armhf.so.3" ]; then
    echo "glbench: missing $DIR/lib/ld-linux-armhf.so.3" >&2
    exit 1
fi
if [ ! -x "$DIR/glbench" ]; then
    echo "glbench: missing $DIR/glbench" >&2
    exit 1
fi

exec "$DIR/lib/ld-linux-armhf.so.3" \
    --inhibit-cache \
    --library-path "$DIR/lib:$DIR/dri" \
    "$DIR/glbench" "$@"
