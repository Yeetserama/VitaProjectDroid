#!/system/bin/sh
# Bring up the SD8787 Wi-Fi using the glibc wpa_supplicant bundled in the
# initramfs at /vita-linux.  Runs as a oneshot late_start service.
#
# v3 exists because the first hardware boot showed wpa_supplicant starting a
# driver scheduled scan and then never associating.  The same radio, firmware
# and credentials work under LinuxOnVita, so the likely difference is the
# scan-offload path: mwifiex reports that it supports NL80211 scheduled scan,
# but this kernel/firmware combination never delivers the results.
#
# v4 adds a link watchdog.  The v3 boot associated and got a DHCP lease, then
# the SD8787 firmware was reset after ~150 s.  btmrvl_sdio shares the card and
# firmware with mwifiex: its probe waits ~100 s for a Bluetooth image, fails,
# and the probe/remove cycle takes Wi-Fi with it.  The kernel fragment now
# disables CONFIG_BT_MRVL/CONFIG_BT_MRVL_SDIO.  If a reset still happens, the
# watchdog restarts the supplicant when the interface comes back, or waits for
# mwifiex to re-probe and then reconnects.
#
# v5 checks adb itself.  The v4 boot had a stable link and a running adbd but
# the PC could neither ping the Vita nor connect to 5555, so the log now
# records whether adbd really owns a LISTEN socket, the address to connect to,
# the gateway ping, the ARP cache, the route table and the INPUT rules.  The
# kernel fragment also disables cfg80211 power save: the SD8787 firmware can
# drop unicast frames while the station sleeps between beacons, which would
# leave DHCP (broadcast) working while ping and adb time out.
#
# v6 repairs Android's policy routing.  The v5 log showed a DHCP lease, a
# LISTEN socket and a directly-connected route in the main table, but
# "ping: sendto: Network unreachable" and an empty ARP cache: no frame ever
# left the Vita.  Android 5.1 netd's RouteController::Init() starts with
# `ip rule flush`, which deletes the kernel's default "from all lookup main"
# rule.  It then fails to add the replacement: Android encodes the UID range
# of its "directly connected" rule in the private attributes FRA 18/19, which
# mainline assigns to FRA_FWMARK/FRA_FLOW, so the kernel rejects the rule with
# EINVAL ("failed to initialize RouteController (Invalid argument)").  The
# failure is deterministic on this kernel, not a race.  With no rule
# consulting the main table every unicast destination is unreachable even
# though dhcpcd installed the subnet route, while DHCP (broadcast/raw) still
# works.  The script restores a "lookup main" rule and the default route after
# every lease, keeps them in place from the watchdog, and logs the rules and
# the kernel's own route lookup before and after the repair.
#
# This script therefore:
#   * adds disable_scan_offload=1 so wpa_supplicant performs normal scans,
#   * adds country=IE unless the user config already sets a country,
#   * runs one supplicant with -d -t debug logging and mirrors the log to ur0,
#   * waits for the carrier to come up before starting dhcpcd exactly once,
#   * retries once with ap_scan=2 if normal scanning still does not associate,
#   * restores the policy rule and default route that netd removed,
#   * keeps a watchdog on carrier, lease and routing after the first lease,
#   * verifies adbd is listening after every lease and logs the connect command,
#   * prints the connect command on the Vita's framebuffer console.
#
# Every script can also be overridden from ur0:/android/; see /init.vita.

# Prefer the busybox applets for the small text utilities; /bin is populated
# by /init.vita before Android starts.
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
    # The ur0 copy survives a reboot and is readable from VitaOS even when
    # adb never comes up.
    echo "vita-net: $*" >> "$UR0_LOG" 2>/dev/null
}

# exFAT does not flush on its own.  Sync at phase boundaries and every few
# seconds while waiting, otherwise a hard reset loses the interesting lines.
sync_logs() {
    sync
}

carrier() {
    cat "/sys/class/net/$IFACE/carrier" 2>/dev/null
}

# Integer uptime seconds, used by the watchdog to rate-limit its log writes.
now_s() {
    cut -d' ' -f1 /proc/uptime 2>/dev/null | cut -d. -f1
}

iface_ip() {
    /sbin/busybox ifconfig "$IFACE" 2>/dev/null \
        | sed -n 's/.*inet addr:\([0-9.]*\).*/\1/p' | head -n 1
}

# /proc/net/tcp{,6} lists "address:port" with the port in uppercase hex and
# state 0A for LISTEN.  Checking the kernel table is more reliable than
# guessing from the adbd process: adbd can run without a TCP listener.
adb_listener() {
    port=${1:-5555}
    hex=$(printf '%04X' "$port" 2>/dev/null)
    [ -n "$hex" ] || return 1
    awk -v p=":$hex" 'FNR > 1 && $4 == "0A" && substr($2, length($2) - length(p) + 1) == p { found = 1 } END { exit(found ? 0 : 1) }' \
        /proc/net/tcp /proc/net/tcp6 2>/dev/null
}

# Netmask of the lease, e.g. 255.255.255.0.
iface_mask() {
    /sbin/busybox ifconfig "$IFACE" 2>/dev/null \
        | sed -n 's/.*Mask:\([0-9.]*\).*/\1/p' | head -n 1
}

# Length of a dotted-quad netmask, e.g. 255.255.255.0 -> 24.
mask_prefix() {
    iface_mask | awk -F. 'NF == 4 { n = 0; for (i = 1; i <= 4; i++) { v = $i + 0; while (v > 0) { n += v % 2; v = int(v / 2) } } print n }'
}

# Network address for the lease, e.g. 192.168.137.170 + 255.255.255.0 ->
# 192.168.137.0.  The kernel rejects a route whose destination has host bits
# set (fib_valid_key_len), so this cannot be skipped by passing the host CIDR.
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

# The rule table, the main route table, and the kernel's own lookup of the
# gateway.  `ip route get` is what turns "ping says unreachable" into either
# "no rule consults main" or "the rule is fine and the frame is lost later".
routing_dump() {
    gw=$(getprop "dhcp.$IFACE.gateway" 2>/dev/null)
    echo "vita-net: ip rule:"
    ip_rule_list
    echo "vita-net: ip route show:"
    /sbin/busybox ip route show 2>&1
    echo "vita-net: ip route get ${gw:-<no gateway>}:"
    [ -n "$gw" ] && /sbin/busybox ip route get "$gw" 2>&1
}

# Android 5.1 netd's RouteController::Init() runs `ip rule flush`, which
# deletes the kernel-default "from all lookup main" rule.  Its replacement
# includes a UID-scoped "directly connected" rule built from Android's private
# FRA 18/19 attributes; mainline reads those as FRA_FWMARK/FRA_FLOW and
# rejects the rule with EINVAL.  Nothing then consults main, so every unicast
# destination is unreachable even though the directly-connected route exists.
# Put the default lookup back above netd's priorities.
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

# dhcpcd cannot install the default route while main is unreachable ("add_route:
# Network is unreachable"), so add it after the rule is back.  Both commands
# are idempotent; the watchdog calls this again if netd restarts.
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

# stdin -> state file -> both logs, in two pieces so the "before" and "after"
# halves of a check stay in order in the log file.
append_state() {
    cat > "$STATE_TMP" 2>&1
    cat "$STATE_TMP" >> "$LOG" 2>/dev/null
    cat "$STATE_TMP" >> "$UR0_LOG" 2>/dev/null
    rm -f "$STATE_TMP"
    sync_logs
}

# The OLED shows the kernel console, but /dev/console is ttyS0 (the last
# console= on the cmdline), so Android's own shell never appears there.  Print
# the line that matters on the framebuffer.  tty0 is not a devtmpfs device, so
# Android's tmpfs /dev has no node for it until we create one.
console_banner() {
    [ -e /dev/tty0 ] || mknod /dev/tty0 c 4 0 2>/dev/null
    [ -c /dev/tty0 ] || return 0
    banner_ip=$(iface_ip)
    {
        echo
        echo "Android 5.1.1 on PS Vita (headless)"
        echo "Wi-Fi: ${banner_ip:-no lease} via $IFACE  gateway: $(getprop "dhcp.$IFACE.gateway" 2>/dev/null)"
        echo "adb connect ${banner_ip:-<vita-ip>}:$(getprop service.adb.tcp.port 2>/dev/null)"
        echo
    } > /dev/tty0 2>/dev/null || true
}

# One compact post-lease snapshot for the cases where the link is healthy but
# adb does not answer: address, gateway, DHCP properties, policy rules, route,
# ARP cache, firewall chains, ndc networks and the mwifiex power state.  The
# routing state is logged twice, before and after repair_routing, because the
# v5 failure (ENETUNREACH with a directly-connected route present) is only
# visible in the rule table.
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
        echo "vita-net: iptables -S fw_INPUT:"
        /system/bin/iptables -S fw_INPUT 2>&1
        echo "vita-net: iptables -S bw_INPUT:"
        /system/bin/iptables -S bw_INPUT 2>&1
        echo "vita-net: ndc network list:"
        /system/bin/ndc network list 2>&1
        echo "vita-net: --- mwifiex power state ---"
        grep -E 'ps_mode|ps_state|deep_sleep|sleep_params' \
            "/sys/kernel/debug/mwifiex/$IFACE/debug" 2>/dev/null \
            || echo "  (no debugfs entry)"
        echo "vita-net: --- end adb check ---"
    } | append_state
}

# adbd is started by Android init before Wi-Fi exists.  If its listener is
# missing, set the port property and restart it once the interface has an
# address.  Always log the exact connect command so it is visible on the
# framebuffer console, in vita-net.log, and in the ur0 copy.
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

# Both the busybox and the toolbox ifconfig print an IPv4 address, but the
# toolbox one prints `ip 0.0.0.0` for an unconfigured interface.  Use the
# busybox applet, whose output only contains `inet addr:` once a lease exists.
have_lease() {
    /sbin/busybox ifconfig "$IFACE" 2>/dev/null | grep -q 'inet addr:'
}

# Append one network-state block to both the data and the ur0 log.
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

# Keep the ur0 copy short but recent.  tail is a busybox applet.
sync_wpa_log() {
    [ -f "$WPA_LOG" ] || return 0
    tail -n 400 "$WPA_LOG" > "$UR0_WPA_LOG.new" 2>/dev/null || return 0
    mv "$UR0_WPA_LOG.new" "$UR0_WPA_LOG" 2>/dev/null || return 0
    sync_logs
}

# Preserve the log of the phase that just failed before start_wpa truncates it.
save_wpa_log() {
    [ -f "$WPA_LOG" ] || return 0
    tail -n 1000 "$WPA_LOG" > "$UR0/wpa_supplicant.crash.log.new" 2>/dev/null \
        && mv -f "$UR0/wpa_supplicant.crash.log.new" "$UR0/wpa_supplicant.crash.log" 2>/dev/null
    sync_logs
}

# Show the handful of supplicant events that explain an association attempt.
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
        # /dev is a tmpfs and is cleared on boot, but check the command line
        # anyway so a reused pid can never kill an unrelated service.
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

    # -B returns before the control socket exists.  Waiting for it turns a bad
    # config or a missing nl80211 driver into a clear log line.
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
        # The pid file survives a hard reset on /data, so never trust it
        # without checking that the pid still belongs to dhcpcd.
        if [ -n "$pid" ] && grep -q 'dhcpcd' "/proc/$pid/cmdline" 2>/dev/null; then
            kill "$pid" 2>/dev/null
        fi
        rm -f "$pidfile"
    fi
}

# Run one dhcpcd instance and poll for the lease.  The old script started a
# new one every 5 s; after the first, every extra copy failed on the pid-file
# lock and just filled the log.
dhcp_once() {
    stop_dhcp
    log "starting dhcpcd (one instance)"
    /system/bin/dhcpcd -b -q "$IFACE" >> "$LOG" 2>&1

    n=0
    while [ $n -lt 40 ]; do
        if have_lease; then
            log "DHCP lease on $IFACE after ${n}s"
            state_block
            # netd flushed the policy rules before dhcpcd ran, so the default
            # route could not be installed and even the on-link subnet is
            # unreachable until the main lookup is restored.
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

# Stay alive after the first lease and recover from a firmware reset.  The
# SDIO card can disappear and come back; wpa_supplicant does not reattach to
# the new interface instance by itself, so kill and restart it when that
# happens.  If only the carrier drops, first give the driver 30 s to recover,
# then restart the supplicant.  dhcpcd is always started exactly once per
# recovery attempt.
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
            # Avoid the state-block output of wait_for_carrier while polling.
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

        # netd only flushes the rules in RouteController::Init, so a netd
        # restart would take the main lookup away again.  Re-check cheaply on
        # every pass instead of waiting for the five-minute heartbeat.
        if ! routing_ok; then
            log "watchdog: policy routing lost; repairing"
            repair_routing
            console_banner
        fi

        # One heartbeat every five minutes is enough to prove the link is
        # still alive without filling ur0 with duplicate state blocks.  Repeat
        # the adb listener check here too, so a listener that died after the
        # lease is noticed and adbd is restarted.
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

# ap_scan=2 asks nl80211 to associate without a scan.  The nl80211 driver
# warns that this can fail, but it is the only fallback available when the
# scan path itself is broken.  It requires explicit security parameters, so
# add them when the network block does not have them.
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

# init.rc creates /data/misc and /data/misc/dhcp in post-fs-data.  Create the
# directories defensively anyway: this service is ordered after post-fs-data,
# but a manual `start vita-net` from adb must work too.  dhcpcd drops to
# AID_DHCP (1014) and writes its lease there; the SDK image has no /etc/passwd,
# so use numeric ids.
mkdir -p "$WORK" "$WORK/sockets" /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null
chown 1014:1014 /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null
chmod 0770 /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null

mkdir -p "$UR0" 2>/dev/null
# Keep the previous run, so a second boot does not destroy the evidence from
# the first one.
[ -f "$UR0_LOG" ] && mv -f "$UR0_LOG" "$UR0/vita-net.prev.log" 2>/dev/null
[ -f "$UR0_WPA_LOG" ] && mv -f "$UR0_WPA_LOG" "$UR0/wpa_supplicant.prev.log" 2>/dev/null

: > "$LOG"
: > "$UR0_LOG" 2>/dev/null
log "starting v6 (main-lookup rule repair, routing dumps, console banner, watchdog)"
log "adbd tcp port: $(getprop service.adb.tcp.port 2>/dev/null)"

if [ ! -f "$CONF" ]; then
    log "no $CONF; Wi-Fi skipped"
    exit 0
fi

if [ ! -x /vita-linux/lib/ld-linux-armhf.so.3 ] || [ ! -x /vita-linux/usr/sbin/wpa_supplicant ]; then
    log "glibc wpa_supplicant is missing from the initramfs; Wi-Fi skipped"
    exit 1
fi

# mwifiex probes asynchronously and may need a moment to load firmware.
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

# Rewrite the sample config for Android: point the control socket at a
# writable directory, force normal scans, and default the regdomain to the
# country reported for this unit when the config does not set one.
# CRLF from a Windows editor is stripped.  Keep any user-specified country.
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

# The Android ip binary produced no output at all on the first boot, so use
# busybox ifconfig, which is a symlink created by /init.vita.
log "bringing $IFACE up"
/sbin/busybox ifconfig "$IFACE" up >> "$LOG" 2>&1
state_block

start_wpa "$CTRL_CONF" "normal scan, scan offload disabled"
if wait_for_carrier 50 "normal scan"; then
    dhcp_once || true
else
    log "association did not complete with normal scanning; trying ap_scan=2"
    # start_wpa truncates the supplicant log, so keep the normal-scan phase
    # for diagnosis before restarting.
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

# dmesg and the regulatory state explain an empty scan (wrong band, regdomain,
# firmware reload, or a hidden/out-of-range AP).
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
    # Keep the service alive to recover from a firmware reset.  The watchdog
    # only starts after a successful lease; an initial failure still exits
    # with the diagnostic log above.
    watch_link
else
    log "no IPv4 address on $IFACE after the supplicant fallback"
    log "check the SSID/psk in $CONF, $UR0_WPA_LOG, and $WORK/vita-net.log"
    sync_logs
fi
