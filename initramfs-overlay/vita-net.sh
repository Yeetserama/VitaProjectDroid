#!/system/bin/sh
# Bring up the SD8787 Wi-Fi using the glibc wpa_supplicant bundled in the
# initramfs at /vita-linux.  Runs as a oneshot late_start service.

# Prefer the busybox applets for the small text utilities; /bin is populated
# by /init.vita before Android starts.
PATH=/bin:/system/bin:/system/xbin
export PATH

CONF=/mnt/ur0/android/wpa_supplicant.conf
IFACE=mlan0
WORK=/data/misc/wifi
CTRL_CONF=$WORK/vita-wpa.conf
LOG=$WORK/vita-net.log
UR0=/mnt/ur0/android
UR0_LOG=$UR0/vita-net.log

log() {
    echo "vita-net: $*" > /dev/kmsg
    echo "vita-net: $*" >> "$LOG"
    # The ur0 copy survives a reboot and is readable from VitaOS even when
    # adb never comes up.
    echo "vita-net: $*" >> "$UR0_LOG" 2>/dev/null
}

# init.rc creates /data/misc and /data/misc/dhcp in post-fs-data.  Create the
# directories defensively anyway: this service is ordered after post-fs-data,
# but a manual `start vita-net` from adb must work too.  dhcpcd drops to
# AID_DHCP (1014) and writes its lease there; the SDK image has no /etc/passwd,
# so use numeric ids.
mkdir -p "$WORK" "$WORK/sockets" /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null
chown 1014:1014 /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null
chmod 0770 /data/misc/dhcp /data/misc/dhcpcd 2>/dev/null

: > "$LOG"
mkdir -p "$UR0" 2>/dev/null
: > "$UR0_LOG" 2>/dev/null
log "starting"

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

# The LinuxOnVita sample config uses /var/run/wpa_supplicant, which does not
# exist on Android and is on a read-only rootfs anyway.  Rewrite it to point
# at the writable Android Wi-Fi directory, and tolerate CRLF configs written
# from Windows.
{
    tr -d '\r' < "$CONF" | grep -v '^[[:space:]]*ctrl_interface[[:space:]]*='
    echo "ctrl_interface=$WORK/sockets"
} > "$CTRL_CONF"

/system/bin/ip link set "$IFACE" up
: > "$WORK/wpa_supplicant.log"

log "starting wpa_supplicant"
/vita-linux/lib/ld-linux-armhf.so.3 \
    --library-path /vita-linux/lib:/vita-linux/usr/lib \
    /vita-linux/usr/sbin/wpa_supplicant \
    -B -Dnl80211 -i "$IFACE" -c "$CTRL_CONF" \
    -f "$WORK/wpa_supplicant.log" >> "$LOG" 2>&1

# -B returns before the control socket exists.  Waiting for it turns a bad
# config or a missing nl80211 driver into a clear log line instead of a silent
# DHCP retry loop.
i=0
while [ $i -lt 10 ]; do
    [ -S "$WORK/sockets/$IFACE" ] && break
    sleep 1
    i=$((i + 1))
done
if [ -S "$WORK/sockets/$IFACE" ]; then
    log "wpa_supplicant control socket ready"
else
    log "no wpa_supplicant control socket after 10 s; see $WORK/wpa_supplicant.log"
fi

# Wait for association, then request a lease.  dhcpcd exits quickly when the
# link has no carrier, so retry it for a while.
i=0
while [ $i -lt 12 ]; do
    if /system/bin/ip -4 addr show dev "$IFACE" 2>/dev/null | grep -q 'inet '; then
        break
    fi
    /system/bin/dhcpcd -b -q "$IFACE" >> "$LOG" 2>&1
    sleep 5
    i=$((i + 1))
done

/system/bin/ip -4 addr show dev "$IFACE" >> "$LOG" 2>&1
/system/bin/ip -4 addr show dev "$IFACE" >> "$UR0_LOG" 2>/dev/null || true
cp "$WORK/wpa_supplicant.log" "$UR0/wpa_supplicant.log" 2>/dev/null || true

if /system/bin/ip -4 addr show dev "$IFACE" 2>/dev/null | grep -q 'inet '; then
    log "Wi-Fi up on $IFACE"
else
    log "no DHCP lease on $IFACE after 60 s"
    log "check the SSID/psk in $CONF, $WORK/wpa_supplicant.log, and /sys/class/net/$IFACE/operstate"
fi
