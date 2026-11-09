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

log() {
    echo "vita-net: $*" > /dev/kmsg
    echo "vita-net: $*" >> "$LOG"
}

mkdir -p "$WORK" 2>/dev/null
: > "$LOG"

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
    log "no wireless interface (firmware missing or mwifiex failed)"
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
mkdir -p "$WORK/sockets" 2>/dev/null

/system/bin/ip link set "$IFACE" up

log "starting wpa_supplicant"
/vita-linux/lib/ld-linux-armhf.so.3 \
    --library-path /vita-linux/lib:/vita-linux/usr/lib \
    /vita-linux/usr/sbin/wpa_supplicant \
    -B -Dnl80211 -i "$IFACE" -c "$CTRL_CONF" \
    -f "$WORK/wpa_supplicant.log" >> "$LOG" 2>&1

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
if /system/bin/ip -4 addr show dev "$IFACE" 2>/dev/null | grep -q 'inet '; then
    log "Wi-Fi up on $IFACE"
    # Keep the address on ur0 so it can be read from VitaOS without adb.
    /system/bin/ip -4 addr show dev "$IFACE" >> /mnt/ur0/android/vita-net.log 2>/dev/null || true
else
    log "no DHCP lease on $IFACE; see $LOG"
fi
