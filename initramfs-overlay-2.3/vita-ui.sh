#!/system/bin/sh
# Start the Android framework only when ur0:/android/ui exists.
# Alternatively run: adb shell setprop vita.ui 1

if [ -e /mnt/ur0/android/ui ]; then
    # vita-diag starts just before this service and needs a moment to take its
    # baseline snapshot.  Without the delay, a marker-triggered crash could
    # happen before the monitor's first poll.
    sleep 2
    echo "vita-ui: enabling framework" > /dev/kmsg
    /system/bin/setprop vita.ui 1
fi
