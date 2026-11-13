#!/system/bin/sh
# Supervise the DHCP client for the Android 4.2 framework's dhcpcd_mlan0
# service.
#
# libnetutils.dhcp_do_request() does roughly:
#
#     setprop dhcp.mlan0.result ""
#     setprop ctl.start dhcpcd_mlan0:-f /system/etc/dhcpcd/dhcpcd.conf mlan0
#     wait for init.svc.dhcpcd_mlan0=running
#     wait for dhcp.mlan0.result to become non-empty (30 s)
#
# The SDK image ships the dhcpcd hooks but no /system/etc/dhcpcd/dhcpcd.conf,
# and it has no dhcpcd_mlan0 service, so this wrapper owns the daemon.  Keep
# dhcpcd in the foreground (-B) from the wrapper's point of view so init
# continues to report init.svc.dhcpcd_mlan0=running while the lease is valid.
#
# The stock 95-configured hook only sets dhcp.<iface>.result=ok on a fresh
# BOUND.  When a lease already exists (the common case: vita-net obtained one
# at boot, then WifiService clears result before starting this service),
# dhcpcd reuses the lease without running that hook and the framework times
# out.  Republish result=ok whenever the framework clears it.

PATH=/bin:/system/bin:/system/xbin
export PATH

IFACE=mlan0
for arg in "$@"; do
    case "$arg" in
    mlan0|wlan0) IFACE=$arg ;;
    esac
done

WORK=/data/misc/wifi
LOG=$WORK/vita-dhcp.log
PIDFILE=/data/misc/dhcp/dhcpcd-$IFACE.pid
dhcp_pid=

log() {
    echo "vita-dhcp: $*" > /dev/kmsg 2>/dev/null
    echo "vita-dhcp: $*" >> "$LOG" 2>/dev/null
}

have_lease() {
    /sbin/busybox ifconfig "$IFACE" 2>/dev/null | grep -q 'inet addr:'
}

have_dhcp_props() {
    [ -n "$(getprop "dhcp.$IFACE.ipaddress" 2>/dev/null)" ]
}

publish_dns() {
    dns1=$(getprop "dhcp.$IFACE.dns1" 2>/dev/null)
    dns2=$(getprop "dhcp.$IFACE.dns2" 2>/dev/null)
    [ -n "$dns1" ] && setprop net.dns1 "$dns1" 2>/dev/null
    [ -n "$dns2" ] && setprop net.dns2 "$dns2" 2>/dev/null
    [ -n "$dns1" ] && setprop "net.$IFACE.dns1" "$dns1" 2>/dev/null
    [ -n "$dns2" ] && setprop "net.$IFACE.dns2" "$dns2" 2>/dev/null
}

cleanup() {
    trap - INT TERM EXIT
    if [ -n "$dhcp_pid" ]; then
        kill "$dhcp_pid" 2>/dev/null
        sleep 1
        kill -9 "$dhcp_pid" 2>/dev/null
    fi
    rm -f "$PIDFILE"
    log "stopped"
    exit 0
}

trap cleanup INT TERM EXIT

mkdir -p /data/misc/dhcp "$WORK" 2>/dev/null
chown 1014:1014 /data/misc/dhcp 2>/dev/null
chmod 0770 /data/misc/dhcp 2>/dev/null

log "starting on $IFACE (framework args: $*)"
/sbin/busybox ifconfig "$IFACE" up >> "$LOG" 2>&1

# dhcpcd exits when the interface disappears (driver reload, Wi-Fi toggled
# off).  Restart it so the init service stays "running" from WifiService's
# point of view; WifiService stops the service explicitly when Wi-Fi is off.
while :; do
    /system/bin/dhcpcd -B -q "$IFACE" >> "$LOG" 2>&1 &
    dhcp_pid=$!
    log "dhcpcd pid $dhcp_pid"

    while kill -0 "$dhcp_pid" 2>/dev/null; do
        if have_lease && have_dhcp_props; then
            if [ "$(getprop "dhcp.$IFACE.result" 2>/dev/null)" != "ok" ]; then
                log "republishing dhcp.$IFACE.result=ok"
                setprop "dhcp.$IFACE.result" ok 2>/dev/null
            fi
            publish_dns
        fi
        sleep 2
    done

    wait "$dhcp_pid" 2>/dev/null
    log "dhcpcd exited; restarting in 2 s"
    dhcp_pid=
    sleep 2
done
