#!/system/bin/sh
# Keep a synced kernel/service snapshot on ur0 while first boot is fragile.
#
# Android init sets panic_on_oops=1 and reboots after four crashes of a
# critical service, so the Vita can reset at any moment.  The old mirror in
# /init.vita rewrote boot.log without calling sync, which is why the first
# boot log stopped at the initial 3.8 s snapshot.  Every snapshot here is
# written to boot.log.new and synced before it is renamed over boot.log.
#
# v3 prints the full process list instead of a filtered one, so the glibc
# wpa_supplicant and dhcpcd are visible even when Wi-Fi never associates.  It
# also records the wireless interface state on every snapshot.

PATH=/bin:/sbin:/system/bin:/system/xbin
export PATH

DIR=/mnt/ur0/android
DEST=$DIR/boot.log
TMP=$DIR/boot.log.new

log() {
    echo "vita-log: $*" > /dev/kmsg 2>/dev/null
}

snapshot() {
    {
        echo "=== vita-log: $(date 2>/dev/null) uptime: $(cut -d' ' -f1 /proc/uptime 2>/dev/null) s ==="
        echo "--- init.svc ---"
        getprop 2>/dev/null | grep '^\[init\.svc\.'
        echo "--- ps ---"
        ps 2>/dev/null
        echo "--- net ---"
        for iface in mlan0 wlan0; do
            [ -e "/sys/class/net/$iface" ] || continue
            echo "$iface: operstate=$(cat "/sys/class/net/$iface/operstate" 2>/dev/null) carrier=$(cat "/sys/class/net/$iface/carrier" 2>/dev/null) flags=$(cat "/sys/class/net/$iface/flags" 2>/dev/null)"
            ifconfig "$iface" 2>&1
        done
        echo "--- logcat (last 200 lines) ---"
        timeout 5 logcat -d -v threadtime -t 200 2>/dev/null
        echo "--- dmesg ---"
        dmesg 2>/dev/null
    } > "$TMP" 2>/dev/null || return 1

    sync
    mv "$TMP" "$DEST" 2>/dev/null || return 1
    sync
}

# `mount_all` in `on fs` finishes before class core starts, but tolerate a
# manual `start vita-log` earlier in boot.
i=0
while [ ! -d "$DIR" ] && [ $i -lt 30 ]; do
    sleep 1
    i=$((i + 1))
done
if [ ! -d "$DIR" ]; then
    log "$DIR is missing; cannot keep a persistent log"
    exit 1
fi

log "starting (every 2 s for 2 min, then every 30 s for 10 min)"

i=0
while [ $i -lt 60 ]; do
    snapshot
    sleep 2
    i=$((i + 1))
done

i=0
while [ $i -lt 20 ]; do
    snapshot
    sleep 30
    i=$((i + 1))
done

snapshot
log "finished"
