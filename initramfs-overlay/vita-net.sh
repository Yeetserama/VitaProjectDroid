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
# This script therefore:
#   * adds disable_scan_offload=1 so wpa_supplicant performs normal scans,
#   * adds country=IE unless the user config already sets a country,
#   * runs one supplicant with -d -t debug logging and mirrors the log to ur0,
#   * waits for the carrier to come up before starting dhcpcd exactly once,
#   * retries once with ap_scan=2 if normal scanning still does not associate.
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
log "starting v3 (disable_scan_offload, debug supplicant, carrier-gated dhcpcd)"
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
else
    log "no IPv4 address on $IFACE after the supplicant fallback"
    log "check the SSID/psk in $CONF, $UR0_WPA_LOG, and $WORK/vita-net.log"
fi
sync_logs
