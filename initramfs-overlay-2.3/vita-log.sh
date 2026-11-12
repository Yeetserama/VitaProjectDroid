#!/system/bin/sh
# Keep a synced kernel/service snapshot on ur0 while first boot is fragile.

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
        echo "=== vita-log: $(date 2>/dev/null) uptime: $(cut -d' ' -f1 /proc/uptime 2>/dev/null) s ==="
        echo "--- init.svc ---"
        getprop 2>/dev/null | grep '^\[init\.svc\.'
        echo "--- ps (userspace) ---"
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
        echo "--- logcat (last 200 lines) ---"
        timeout 5 logcat -d -v threadtime -t 200 2>/dev/null
        echo "--- dmesg ---"
        dmesg 2>/dev/null || true
    } > "$TMP" 2>/dev/null || return 1

    sync
    mv "$TMP" "$DEST" 2>/dev/null || return 1
    sync
}

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