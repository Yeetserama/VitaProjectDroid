#!/system/bin/sh
# Preserve the state around a Gingerbread zygote/media restart.
#
# The headless gate and the UI trigger both use Android property services, so
# init can restart zygote and media faster than a human can attach adb.  This
# daemon watches init.svc.* and the process IDs, then snapshots logcat, dmesg,
# ANR traces, tombstones and memory state to ur0:/android/diag/ the first time
# a service disappears or changes PID.  It is deliberately independent of
# vita-log: boot.log is overwritten every two seconds, while these files stay.

PATH=/bin:/sbin:/system/bin:/system/xbin
export PATH

BB=/sbin/busybox
DIR=/mnt/ur0/android/diag
MAX_CRASHES=4

log() {
    echo "vita-diag: $*" > /dev/kmsg 2>/dev/null
}

service_state() {
    getprop "init.svc.$1" 2>/dev/null
}

first_pid() {
    $BB pidof "$1" 2>/dev/null | $BB awk '{print $1}'
}

capture() {
    reason=$1
    now=$($BB date +%Y%m%d-%H%M%S 2>/dev/null)
    [ -n "$now" ] || now=$($BB date +%s 2>/dev/null)
    dest="$DIR/crash-$now-$reason.txt"
    tmp="$dest.tmp"

    $BB mkdir -p "$DIR" 2>/dev/null
    {
        echo "=== vita-diag: $reason ==="
        echo "date: $($BB date 2>/dev/null)"
        echo "uptime: $($BB cat /proc/uptime 2>/dev/null)"
        echo "vita.ui=$(getprop vita.ui 2>/dev/null)"
        echo "init.svc.zygote=$(service_state zygote)"
        echo "init.svc.media=$(service_state media)"
        echo "zygote pid=$(first_pid zygote)"
        echo "mediaserver pid=$(first_pid mediaserver)"

        echo "--- init.svc.* ---"
        getprop 2>/dev/null | $BB grep '^\[init\.svc\.'

        echo "--- properties ---"
        getprop 2>/dev/null | $BB grep -E '^\[(ro\.kernel|ro\.build|ro\.product|dalvik|vita|persist\.sys)' || true

        echo "--- ps ---"
        $BB ps 2>/dev/null | $BB head -n 250

        echo "--- meminfo ---"
        $BB cat /proc/meminfo 2>/dev/null

        echo "--- vmstat ---"
        $BB cat /proc/vmstat 2>/dev/null | $BB grep -E 'oom|pgmajfault|pswp|pgscan' || true

        echo "--- logcat ---"
        for buf in main system events radio; do
            echo "### buffer $buf"
            $BB timeout 10 /system/bin/logcat -b "$buf" -d -v threadtime 2>&1 \
                | $BB tail -n 800
        done

        echo "--- dmesg ---"
        $BB dmesg 2>/dev/null | $BB tail -n 1200

        echo "--- anr ---"
        $BB ls -l /data/anr 2>/dev/null
        $BB tail -n 400 /data/anr/traces.txt 2>/dev/null

        echo "--- tombstones ---"
        $BB ls -lt /data/tombstones 2>/dev/null
        latest_tombstone=$($BB ls -1t /data/tombstones/tombstone_* 2>/dev/null | $BB head -n 1)
        if [ -n "$latest_tombstone" ]; then
            echo "### $latest_tombstone"
            $BB tail -n 400 "$latest_tombstone" 2>/dev/null
        fi

        echo "--- dropbox ---"
        $BB ls -lt /data/system/dropbox 2>/dev/null | $BB head -n 30
    } > "$tmp" 2>/dev/null

    $BB sync
    if $BB mv -f "$tmp" "$dest" 2>/dev/null; then
        $BB cp -f "$dest" "$DIR/last-crash.txt" 2>/dev/null
        log "captured $reason to ur0:/android/diag/${dest##*/}"
    else
        log "could not write $dest"
        $BB rm -f "$tmp" 2>/dev/null
        return 1
    fi

    # Keep the newest few crash files only.  The baseline also counts, so a
    # reboot loop cannot fill ur0 with multi-megabyte logs.
    $BB ls -1t "$DIR"/crash-*.txt 2>/dev/null | $BB tail -n +$((MAX_CRASHES + 1)) \
        | while read -r old; do
            $BB rm -f "$old" 2>/dev/null
        done
    $BB sync
}

i=0
while [ ! -d /mnt/ur0/android ] && [ "$i" -lt 30 ]; do
    sleep 1
    i=$((i + 1))
done
if [ ! -d /mnt/ur0/android ]; then
    log "ur0 is not mounted; diagnostics disabled"
    exit 1
fi

$BB mkdir -p "$DIR" 2>/dev/null
log "watching zygote/media (output: ur0:/android/diag)"

prev_z=$(service_state zygote)
prev_m=$(service_state media)
prev_zp=$(first_pid zygote)
prev_mp=$(first_pid mediaserver)
capture "baseline"

cooldown=0
while true; do
    sleep 2

    z=$(service_state zygote)
    m=$(service_state media)
    zp=$(first_pid zygote)
    mp=$(first_pid mediaserver)

    reason=

    if [ "$z" != "$prev_z" ]; then
        case "$z" in
            running) ;;
            *) reason="zygote-${z:-gone}" ;;
        esac
    fi

    if [ -z "$reason" ] && [ -n "$prev_zp" ] && [ "$zp" != "$prev_zp" ]; then
        reason="zygote-pid-change"
    fi

    if [ -z "$reason" ] && [ "$m" != "$prev_m" ]; then
        case "$m" in
            running) ;;
            *) reason="media-${m:-gone}" ;;
        esac
    fi

    if [ -z "$reason" ] && [ -n "$prev_mp" ] && [ "$mp" != "$prev_mp" ]; then
        reason="media-pid-change"
    fi

    if [ -n "$reason" ] && [ "$cooldown" -le 0 ]; then
        capture "$reason"
        cooldown=10
    fi
    [ "$cooldown" -gt 0 ] && cooldown=$((cooldown - 2))

    prev_z=$z
    prev_m=$m
    prev_zp=$zp
    prev_mp=$mp
done
