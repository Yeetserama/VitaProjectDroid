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
#
# v5 keeps the userspace processes but drops the kernel-thread flood, checks
# whether adbd is actually listening on its TCP port, and removes the
# status-0 "untracked pid" lines that Android init emits for every orphaned
# helper.  Non-zero exits are kept: those are real failures.
#
# v6 adds the policy rule table and the main route table, because netd can
# flush the "lookup main" rule and leave the link up but unicast unreachable,
# plus the mwifiex power-save counters.

PATH=/bin:/sbin:/system/bin:/system/xbin
export PATH

DIR=/mnt/ur0/android
DEST=$DIR/boot.log
TMP=$DIR/boot.log.new

log() {
    echo "vita-log: $*" > /dev/kmsg 2>/dev/null
}

snapshot() {
    port=$(getprop service.adb.tcp.port 2>/dev/null)
    [ -n "$port" ] || port=5555
    hex=$(printf '%04X' "$port" 2>/dev/null)
    {
        echo "=== vita-log: $(date 2>/dev/null) uptime: $(cut -d' ' -f1 /proc/uptime 2>/dev/null) s android: $(getprop ro.build.version.release 2>/dev/null) sdk: $(getprop ro.build.version.sdk 2>/dev/null) ==="
        echo "--- init.svc ---"
        getprop 2>/dev/null | grep '^\[init\.svc\.'
        echo "--- ps (userspace) ---"
        # Kernel threads are shown with their name in brackets; drop those
        # lines but keep every userspace process, including short-lived ones.
        ps 2>/dev/null | grep -v -E '\[[^]]*\][[:space:]]*$'
        echo "--- ps (adb/wifi) ---"
        ps 2>/dev/null | grep -E '[a]dbd|[n]etd|[w]pa_supplicant|[d]hcpcd|[v]ita-'
        echo "--- adb ---"
        echo "init.svc.adbd=$(getprop init.svc.adbd 2>/dev/null)"
        echo "service.adb.tcp.port=$(getprop service.adb.tcp.port 2>/dev/null)"
        inet=$(ifconfig mlan0 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p')
        [ -n "$inet" ] || inet=$(ifconfig wlan0 2>/dev/null | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p')
        echo "wifi inet: ${inet:-none}"
        echo "LISTEN on port $port:"
        listeners=$(awk -v p=":$hex" 'FNR > 1 && $4 == "0A" && substr($2, length($2) - length(p) + 1) == p { print "  " $2 " uid=" $8 }' /proc/net/tcp /proc/net/tcp6 2>/dev/null)
        if [ -n "$listeners" ]; then
            echo "$listeners"
        else
            echo "  (none)"
        fi
        echo "--- arp ---"
        cat /proc/net/arp 2>/dev/null
        echo "--- net ---"
        for iface in mlan0 wlan0; do
            [ -e "/sys/class/net/$iface" ] || continue
            echo "$iface: operstate=$(cat "/sys/class/net/$iface/operstate" 2>/dev/null) carrier=$(cat "/sys/class/net/$iface/carrier" 2>/dev/null) flags=$(cat "/sys/class/net/$iface/flags" 2>/dev/null)"
            ifconfig "$iface" 2>&1
        done
        echo "--- routing ---"
        ip rule 2>&1
        ip route show 2>&1 | head -n 20
        echo "--- mwifiex ps ---"
        grep -E 'ps_mode|ps_state|deep_sleep' /sys/kernel/debug/mwifiex/mlan0/debug 2>/dev/null \
            || echo "  (none)"
        echo "--- logcat (last 200 lines) ---"
        timeout 5 logcat -d -v threadtime -t 200 2>/dev/null
        echo "--- init reaping (status-0 lines removed from dmesg below) ---"
        dmesg 2>/dev/null | grep -c -E 'init: (waitpid returned pid [0-9]+, status = 00000000|untracked pid [0-9]+ exited with status 0)'
        echo "--- dmesg ---"
        dmesg 2>/dev/null | grep -v -E 'init: (waitpid returned pid [0-9]+, status = 00000000|untracked pid [0-9]+ exited with status 0)' || true
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
