#!/system/bin/sh
# Bring up the SD8787 Wi-Fi using the glibc wpa_supplicant bundled in the
# initramfs at /vita-linux.  Runs as a oneshot default-class service after
# Android init has mounted /system and /data.

PATH=/bin:/system/bin:/system/xbin
export PATH

CONF=/mnt/ur0/android/wpa_supplicant.conf
IFACE=mlan0
WORK=/data/misc/wifi
CTRL_CONF=$WORK/vita-wpa.conf
APSCAN_CONF=$WORK/vita-wpa-apscan2.conf
LOG=$WORK/vita-net.log
UR0=/mnt/ur0/android
UR0_LOG=$UR0/vita-net.log
WPA_LOG=$WORK/wpa_supplicant.log
UR0_WPA_LOG=$UR0/wpa_supplicant.log
WPA_PID=/dev/vita-wpa.pid
STATE_TMP=$WORK/vita-net-state.tmp

log() {
    echo "vita-net: $*" > /dev/kmsg 2>/dev/null
    echo "vita-net: $*" >> "$LOG"
    echo "vita-net: $*" >> "$UR0_LOG" 2>/dev/null
}

sync_logs() {
    sync
}

carrier() {
    cat "/sys/class/net/$IFACE/carrier" 2>/dev/null
}

now_s() {
    cut -d' ' -f1 /proc/uptime 2>/dev/null | cut -d. -f1
}

iface_ip() {
    /sbin/busybox ifconfig "$IFACE" 2>/dev/null \
        | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p' | head -n 1
}

adb_listener() {
    port=${1:-5555}
    hex=$(printf '%04X' "$port" 2>/dev/null)
    [ -n "$hex" ] || return 1
    awk -v p=":$hex" 'FNR > 1 && $4 == "0A" && substr($2, length($2) - length(p) + 1) == p { found = 1 } END { exit(found ? 0 : 1) }' \
        /proc/net/tcp /proc/net/tcp6 2>/dev/null
}

iface_mask() {
    /sbin/busybox ifconfig "$IFACE" 2>/dev/null \
        | sed -n 's/.*Mask:\([0-9.]*\).*/\1/p' | head -n 1
}

mask_prefix() {
    iface_mask | awk -F. 'NF == 4 { n = 0; for (i = 1; i <= 4; i++) { v = $i + 0; while (v > 0) { n += v % 2; v = int(v / 2) } } print n }'
}

network_addr() {
    echo "$1 $2" | awk '{
        split($1, a, "."); split($2, m, ".");
        printf "%d.%d.%d.%d\n", a[1]-(a[1]%(256-m[1])), a[2]-(a[2]%(256-m[2])), a[3]-(a[3]%(256-m[3])), a[4]-(a[4]%(256-m[4]))
    }'
}

iface_network() {
    addr=$(iface_ip)
    mask=$(iface_mask)
    [ -n "$addr" ] && [ -n "$mask" ] && network_addr "$addr" "$mask"
}

ip_rule_list() {
    /sbin/busybox ip rule 2>&1
}

routing_dump() {
    gw=$(getprop "dhcp.$IFACE.gateway" 2>/dev/null)
    echo "vita-net: ip rule:"
    ip_rule_list
    echo "vita-net: ip route show:"
    /sbin/busybox ip route show 2>&1
    echo "vita-net: ip route get ${gw:-<no gateway>}:"
    [ -n "$gw" ] && /sbin/busybox ip route get "$gw" 2>&1
}

ensure_main_rule() {
    ip_rule_list 2>/dev/null | grep -q 'lookup main' && return 0
    for attempt in \
        "/sbin/busybox ip rule add lookup main pref 1000" \
        "/sbin/busybox ip rule add from all lookup main pref 1000" \
        "/system/bin/ip rule add from all lookup main pref 1000"; do
        tool=${attempt%% *}
        [ -x "$tool" ] || continue
        $attempt >> "$LOG" 2>&1
        if ip_rule_list 2>/dev/null | grep -q 'lookup main'; then
            log "routing: restored the main lookup rule ($attempt)"
            return 0
        fi
    done
    log "routing: could not restore the main lookup rule"
    return 1
}

repair_routing() {
    ensure_main_rule || return 1
    net=$(iface_network)
    prefix=$(mask_prefix)
    gw=$(getprop "dhcp.$IFACE.gateway" 2>/dev/null)
    if [ -n "$net" ] && [ -n "$prefix" ]; then
        /sbin/busybox ip route replace "$net/$prefix" dev "$IFACE" >> "$LOG" 2>&1 \
            || log "routing: could not install the $net/$prefix route"
    fi
    if [ -n "$gw" ]; then
        /sbin/busybox ip route replace default via "$gw" dev "$IFACE" >> "$LOG" 2>&1 \
            || log "routing: could not install the default route via $gw"
    fi
    return 0
}

routing_ok() {
    ip_rule_list 2>/dev/null | grep -q 'lookup main' || return 1
    /sbin/busybox ip route show 2>/dev/null | grep -q '^default' || return 1
}

append_state() {
    cat > "$STATE_TMP" 2>&1
    cat "$STATE_TMP" >> "$LOG" 2>/dev/null
    cat "$STATE_TMP" >> "$UR0_LOG" 2>/dev/null
    rm -f "$STATE_TMP"
    sync_logs
}

console_banner() {
    [ -e /dev/tty0 ] || mknod /dev/tty0 c 4 0 2>/dev/null
    [ -c /dev/tty0 ] || return 0
    banner_ip=$(iface_ip)
    {
        echo
        echo "Android 2.3.7 (Gingerbread) on PS Vita (headless)"
        echo "Wi-Fi: ${banner_ip:-no lease} via $IFACE  gateway: $(getprop "dhcp.$IFACE.gateway" 2>/dev/null)"
        echo "adb connect ${banner_ip:-<vita-ip>}:$(getprop service.adb.tcp.port 2>/dev/null)"
        echo
    } > /dev/tty0 2>/dev/null || true
}

adb_net_check() {
    gw=$(getprop "dhcp.$IFACE.gateway" 2>/dev/null)
    {
        echo "vita-net: adb check: inet=$(iface_ip) port=$(getprop service.adb.tcp.port 2>/dev/null)"
        echo "vita-net: gateway=${gw:-unknown}"
        echo "vita-net: dhcp properties:"
        getprop 2>/dev/null | grep '^\[dhcp\.' || echo "  (none)"
        echo "vita-net: --- routing before repair ---"
        routing_dump
    } | append_state

    repair_routing

    {
        echo "vita-net: --- routing after repair ---"
        routing_dump
        echo "vita-net: /proc/net/route:"
        cat /proc/net/route 2>/dev/null
        if [ -n "$gw" ]; then
            ping_bin=/bin/ping
            [ -x "$ping_bin" ] || ping_bin=/system/bin/ping
            echo "vita-net: ping gateway $gw:"
            "$ping_bin" -c 1 -W 2 "$gw" 2>&1 | tail -n 4
            echo "vita-net: /proc/net/arp after the ping:"
            cat /proc/net/arp 2>/dev/null
        fi
        echo "vita-net: iptables -S INPUT:"
        /system/bin/iptables -S INPUT 2>&1
        echo "vita-net: ndc network list:"
        /system/bin/ndc network list 2>&1
        echo "vita-net: --- mwifiex power state ---"
        grep -E 'ps_mode|ps_state|deep_sleep|sleep_params' \
            "/sys/kernel/debug/mwifiex/$IFACE/debug" 2>/dev/null \
            || echo "  (no debugfs entry)"
        echo "vita-net: --- end adb check ---"
    } | append_state
}

ensure_adb() {
    port=$(getprop service.adb.tcp.port 2>/dev/null)
    [ -n "$port" ] || port=5555
    state=$(getprop init.svc.adbd 2>/dev/null)
    ip=$(iface_ip)

    if adb_listener "$port"; then
        log "adb: listener on port $port (init.svc.adbd=${state:-unknown}); adb connect ${ip:-<vita-ip>}:$port"
        return 0
    fi

    log "adb: no listener on port $port (init.svc.adbd=${state:-unknown}); restarting adbd"
    if [ "$(getprop service.adb.tcp.port 2>/dev/null)" != "$port" ]; then
        setprop service.adb.tcp.port "$port" >> "$LOG" 2>&1
    fi
    setprop ctl.restart adbd >> "$LOG" 2>&1

    n=0
    while [ $n -lt 10 ]; do
        sleep 1
        if adb_listener "$port"; then
            ip=$(iface_ip)
            log "adb: listener on port $port after restart; adb connect ${ip:-<vita-ip>}:$port"
            return 0
        fi
        n=$((n + 1))
    done

    log "adb: still no listener on port $port (init.svc.adbd=$(getprop init.svc.adbd 2>/dev/null))"
    return 1
}

have_lease() {
    /sbin/busybox ifconfig "$IFACE" 2>/dev/null | grep -q 'inet addr:'
}

state_block() {
    {
        echo "vita-net: --- $IFACE state ---"
        echo "vita-net: operstate=$(cat "/sys/class/net/$IFACE/operstate" 2>/dev/null) carrier=$(carrier) flags=$(cat "/sys/class/net/$IFACE/flags" 2>/dev/null)"
        echo "vita-net: busybox ifconfig:"
        /sbin/busybox ifconfig "$IFACE" 2>&1
        echo "vita-net: toolbox ifconfig:"
        /system/bin/ifconfig "$IFACE" 2>&1
        echo "vita-net: /proc/net/route:"
        cat /proc/net/route 2>/dev/null
        echo "vita-net: --- end $IFACE state ---"
    } > "$STATE_TMP" 2>&1
    cat "$STATE_TMP" >> "$LOG" 2>/dev/null
    cat "$STATE_TMP" >> "$UR0_LOG" 2>/dev/null
    rm -f "$STATE_TMP"
    sync_logs
}

sync_wpa_log() {
    [ -f "$WPA_LOG" ] || return 0
    tail -n 400 "$WPA_LOG" > "$UR0_WPA_LOG.new" 2>/dev/null || return 0
    mv "$UR0_WPA_LOG.new" "$UR0_WPA_LOG" 2>/dev/null || return 0
    sync_logs
}

save_wpa_log() {
    [ -f "$WPA_LOG" ] || return 0
    tail -n 1000 "$WPA_LOG" > "$UR0/wpa_supplicant.crash.log.new" 2>/dev/null \
        && mv -f "$UR0/wpa_supplicant.crash.log.new" "$UR0/wpa_supplicant.crash.log" 2>/dev/null
    sync_logs
}

wpa_events() {
    [ -f "$WPA_LOG" ] || return 0
    grep -E 'CTRL-EVENT-(CONNECTED|DISCONNECTED|SCAN-RESULTS|ASSOC-REJECT|AUTH-REJECT|NETWORK-NOT-FOUND)|Trying to associate|Associated with|WPA: Key negotiation' \
        "$WPA_LOG" 2>/dev/null | tail -n 8 | while IFS= read -r line; do
            log "wpa: $line"
        done
}

stop_wpa() {
    if [ -f "$WPA_PID" ]; then
        pid=$(cat "$WPA_PID" 2>/dev/null)
        if [ -n "$pid" ] && grep -q 'wpa_supplicant' "/proc/$pid/cmdline" 2>/dev/null; then
            kill "$pid" 2>/dev/null
            sleep 2
            kill -9 "$pid" 2>/dev/null
        fi
        rm -f "$WPA_PID"
    fi
    rm -f "$WORK/sockets/$IFACE" "$WORK/sockets/global" 2>/dev/null
}

start_wpa() {
    conf=$1
    tag=$2
    : > "$WPA_LOG"
    log "starting wpa_supplicant ($tag)"
    /vita-linux/lib/ld-linux-armhf.so.3 \
        --library-path /vita-linux/lib:/vita-linux/usr/lib \
        /vita-linux/usr/sbin/wpa_supplicant \
        -B -Dnl80211 -i "$IFACE" -c "$conf" -d -t -P "$WPA_PID" \
        -f "$WPA_LOG" >> "$LOG" 2>&1

    i=0
    while [ $i -lt 15 ]; do
        [ -S "$WORK/sockets/$IFACE" ] && break
        sleep 1
        i=$((i + 1))
    done
    if [ -S "$WORK/sockets/$IFACE" ]; then
        log "wpa_supplicant control socket ready"
    else
        log "no wpa_supplicant control socket after 15 s; see $WPA_LOG"
    fi
    sync_wpa_log
}

stop_dhcp() {
    pidfile=/data/misc/dhcp/dhcpcd-$IFACE.pid
    if [ -f "$pidfile" ]; then
        pid=$(cat "$pidfile" 2>/dev/null)
        if [ -n "$pid" ] && grep -q 'dhcpcd' "/proc/$pid/cmdline" 2>/dev/null; then
            kill "$pid" 2>/dev/null
        fi
        rm -f "$pidfile"
    fi
}

dhcp_once() {
    stop_dhcp
    log "starting dhcpcd (one instance)"
    /system/bin/dhcpcd -b -q "$IFACE" >> "$LOG" 2>&1

    n=0
    while [ $n -lt 40 ]; do
        if have_lease; then
            log "DHCP lease on $IFACE after ${n}s"
            state_block
            repair_routing
            ensure_adb
            adb_net_check
            console_banner
            return 0
        fi
        if [ $((n % 10)) -eq 0 ]; then
            log "waiting for a DHCP lease (${n}s)"
            sync_wpa_log
        fi
        sleep 2
        n=$((n + 2))
    done
    log "dhcpcd gave no lease on $IFACE after 40 s"
    return 1
}

wait_for_carrier() {
    seconds=$1
    label=$2
    n=0
    while [ $n -lt "$seconds" ]; do
        if [ "$(carrier)" = "1" ]; then
            log "$label: carrier=1 after ${n}s"
            return 0
        fi
        if [ $((n % 10)) -eq 0 ]; then
            state_block
            wpa_events
            sync_wpa_log
        fi
        sleep 2
        n=$((n + 2))
    done
    log "$label: no carrier after ${seconds}s"
    state_block
    wpa_events
    sync_wpa_log
    return 1
}

watch_link() {
    was_down=0
    last_sync=$(now_s)
    while :; do
        now=$(now_s)

        if [ ! -e "/sys/class/net/$IFACE" ]; then
            if [ "$was_down" -eq 0 ]; then
                log "watchdog: $IFACE disappeared; waiting for mwifiex to re-probe"
                save_wpa_log
                state_block
                was_down=1
            fi
            sleep 5
            continue
        fi

        if [ "$was_down" -eq 1 ]; then
            log "watchdog: $IFACE is back; restarting the supplicant"
            was_down=0
            /sbin/busybox ifconfig "$IFACE" up >> "$LOG" 2>&1
            stop_wpa
            start_wpa "$CTRL_CONF" "watchdog reconnect"
            if wait_for_carrier 40 "watchdog reconnect"; then
                dhcp_once || true
            fi
            sync_wpa_log
            last_sync=$(now_s)
            sleep 5
            continue
        fi

        if [ "$(carrier)" != "1" ]; then
            log "watchdog: carrier lost"
            save_wpa_log
            n=0
            while [ $n -lt 30 ]; do
                [ "$(carrier)" = "1" ] && break
                sleep 2
                n=$((n + 2))
            done
            if [ "$(carrier)" = "1" ]; then
                log "watchdog: carrier returned after ${n}s"
                dhcp_once || true
            else
                log "watchdog: carrier did not return; restarting the supplicant"
                stop_wpa
                start_wpa "$CTRL_CONF" "watchdog restart"
                if wait_for_carrier 40 "watchdog restart"; then
                    dhcp_once || true
                fi
            fi
            sync_wpa_log
            last_sync=$(now_s)
            sleep 5
            continue
        fi

        if ! have_lease; then
            log "watchdog: no IPv4 lease; running dhcpcd"
            dhcp_once || true
            sync_wpa_log
            last_sync=$(now_s)
            sleep 5
            continue
        fi

        if ! routing_ok; then
            log "watchdog: policy routing lost; repairing"
            repair_routing
            console_banner
        fi

        if [ $((now - last_sync)) -ge 300 ]; then
            log "watchdog: link healthy (uptime ${now}s, $(/sbin/busybox ifconfig "$IFACE" 2>/dev/null | grep 'inet addr:' | sed 's/^[[:space:]]*//'))"
            ensure_adb
            console_banner
            sync_wpa_log
            last_sync=$now
        fi
        sleep 10
    done
}

build_apscan_conf() {
    {
        echo "ap_scan=2"
        awk '
            function close_network() {
                if (in_net) {
                    if (!proto)    print "    proto=RSN"
                    if (!pairwise) print "    pairwise=CCMP"
                    if (!key_mgmt) print "    key_mgmt=WPA-PSK"
                    in_net = 0
                }
            }
            /^[[:space:]]*network[[:space:]]*=/ {
                close_network()
                in_net = 1
                proto = 0
                pairwise = 0
                key_mgmt = 0
            }
            in_net && /^[[:space:]]*proto[[:space:]]*=/    { proto = 1 }
            in_net && /^[[:space:]]*pairwise[[:space:]]*=/ { pairwise = 1 }
            in_net && /^[[:space:]]*key_mgmt[[:space:]]*=/ { key_mgmt = 1 }
            in_net && /^[[:space:]]*}/ { close_network() }
            { print }
        ' "$CTRL_CONF"
    } > "$APSCAN_CONF" 2>/dev/null
}

mkdir -p "$WORK" "$WORK/sockets" /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null
chown 1014:1014 /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null
chmod 0770 /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null

mkdir -p "$UR0" 2>/dev/null
[ -f "$UR0_LOG" ] && mv -f "$UR0_LOG" "$UR0/vita-net.prev.log" 2>/dev/null
[ -f "$UR0_WPA_LOG" ] && mv -f "$UR0_WPA_LOG" "$UR0/wpa_supplicant.prev.log" 2>/dev/null

: > "$LOG"
: > "$UR0_LOG" 2>/dev/null
log "starting (Android 2.3.7 Gingerbread)"
log "adbd tcp port: $(getprop service.adb.tcp.port 2>/dev/null)"

if [ ! -f "$CONF" ]; then
    log "no $CONF; Wi-Fi skipped"
    exit 0
fi

if [ ! -x /vita-linux/lib/ld-linux-armhf.so.3 ] || [ ! -x /vita-linux/usr/sbin/wpa_supplicant ]; then
    log "glibc wpa_supplicant is missing from the initramfs; Wi-Fi skipped"
    exit 1
fi

i=0
while [ $i -lt 30 ]; do
    [ -e /sys/class/net/mlan0 ] && break
    [ -e /sys/class/net/wlan0 ] && break
    sleep 1
    i=$((i + 1))
done
if [ -e /sys/class/net/mlan0 ]; then
    IFACE=mlan0
elif [ -e /sys/class/net/wlan0 ]; then
    IFACE=wlan0
else
    log "no wireless interface after 30 s (firmware missing or mwifiex failed)"
    log "check dmesg for mwifiex and /vita-linux/lib/firmware/mrvl/sd8787_uapsta.bin"
    exit 1
fi

log "using interface $IFACE"
stop_wpa
stop_dhcp

{
    tr -d '\r' < "$CONF" \
        | grep -v -e '^[[:space:]]*ctrl_interface[[:space:]]*=' \
                  -e '^[[:space:]]*disable_scan_offload[[:space:]]*=' \
                  -e '^[[:space:]]*ap_scan[[:space:]]*='
    echo
    echo "ctrl_interface=$WORK/sockets"
    echo "disable_scan_offload=1"
    if ! tr -d '\r' < "$CONF" | grep -qi '^[[:space:]]*country[[:space:]]*='; then
        echo "country=IE"
    fi
} > "$CTRL_CONF"

if ! grep -q '^[[:space:]]*network[[:space:]]*=' "$CTRL_CONF"; then
    log "no network block in $CONF; Wi-Fi skipped"
    exit 1
fi
log "config: $(grep -m1 '^[[:space:]]*ssid[[:space:]]*=' "$CTRL_CONF" | sed 's/^[[:space:]]*//') (psk hidden)"
country=$(grep -m1 '^[[:space:]]*country[[:space:]]*=' "$CTRL_CONF" | sed 's/^[[:space:]]*//')
log "config: ${country:-country: none}"
log "generated $CTRL_CONF (psk hidden):"
grep -vi -E '^[[:space:]]*psk[[:space:]]*=' "$CTRL_CONF" | while IFS= read -r line; do
    log "  $line"
done

log "bringing $IFACE up"
/sbin/busybox ifconfig "$IFACE" up >> "$LOG" 2>&1
state_block

start_wpa "$CTRL_CONF" "normal scan, scan offload disabled"
if wait_for_carrier 50 "normal scan"; then
    dhcp_once || true
else
    log "association did not complete with normal scanning; trying ap_scan=2"
    tail -n 1000 "$WPA_LOG" > "$UR0/wpa_supplicant.normal.log.new" 2>/dev/null \
        && mv -f "$UR0/wpa_supplicant.normal.log.new" "$UR0/wpa_supplicant.normal.log" 2>/dev/null
    sync_logs
    stop_wpa
    build_apscan_conf
    if grep -q '^ap_scan=2$' "$APSCAN_CONF"; then
        start_wpa "$APSCAN_CONF" "ap_scan=2 fallback"
        if wait_for_carrier 40 "ap_scan=2"; then
            dhcp_once || true
        fi
    else
        log "could not build $APSCAN_CONF; skipping the fallback"
    fi
fi

sync_wpa_log
state_block

{
    echo "vita-net: --- dmesg (mwifiex/cfg80211/regulatory) ---"
    dmesg 2>/dev/null | grep -iE 'mwifiex|cfg80211|regulatory|mlan' | tail -n 100
    echo "vita-net: --- end dmesg ---"
} > "$STATE_TMP" 2>&1
cat "$STATE_TMP" >> "$LOG" 2>/dev/null
cat "$STATE_TMP" >> "$UR0_LOG" 2>/dev/null
rm -f "$STATE_TMP"
sync_logs

if have_lease; then
    log "Wi-Fi up on $IFACE"
    sync_logs
    watch_link
else
    log "no IPv4 address on $IFACE after the supplicant fallback"
    log "check the SSID/psk in $CONF, $UR0_WPA_LOG, and $WORK/vita-net.log"
    sync_logs
fi