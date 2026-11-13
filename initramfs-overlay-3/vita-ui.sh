#!/system/bin/sh
# Compatibility helper: init.vita.rc already sets vita.ui=1 and starts the
# framework.  If that line is removed for a headless build, an empty
# /mnt/ur0/android/ui file (or `setprop vita.ui 1`) still starts it.

if [ -e /mnt/ur0/android/ui ]; then
    echo "vita-ui: enabling framework" > /dev/kmsg
    /system/bin/setprop vita.ui 1
fi