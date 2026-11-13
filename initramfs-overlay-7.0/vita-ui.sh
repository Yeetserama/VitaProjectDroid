#!/system/bin/sh
# Start the Android framework only when ur0:/android/ui exists.
# Alternatively run: adb shell setprop vita.ui 1

if [ -e /mnt/ur0/android/ui ]; then
    echo "vita-ui: enabling framework" > /dev/kmsg
    /system/bin/setprop vita.ui 1
fi
