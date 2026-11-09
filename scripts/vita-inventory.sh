#!/bin/sh
# AndroidOnVita - Phase 0 hardware inventory
#
# Read-only. Does not mount anything read-write, does not load modules, and does
# not change sysfs state. Safe to run on a working LinuxOnVita install.
#
# Run it from the PC over SSH and capture the report locally:
#
#   scp scripts/vita-inventory.sh root@vita.local:/tmp/
#   ssh root@vita.local "sh /tmp/vita-inventory.sh" > vita-inventory.txt
#
# Or run it on the Vita console:
#
#   sh /tmp/vita-inventory.sh | tee /tmp/vita-inventory.txt
#
# Also run LinuxOnVita's own collector for the full logs and tarball:
#
#   vita-diagnostics
#
# The values here feed the Android boot package: framebuffer geometry, the ur0
# block device, evdev capabilities, Wi-Fi firmware status, and kernel config gaps.

PATH=/usr/sbin:/usr/bin:/sbin:/bin:$PATH
export PATH

section() { printf '\n\n########## %s ##########\n' "$1"; }
sub()     { printf '\n--- %s ---\n' "$1"; }

file() {
    sub "$1"
    if [ -r "$1" ]; then
        cat "$1" 2>/dev/null || echo "(read failed)"
    else
        echo "(not present)"
    fi
}

cmd() {
    sub "$*"
    "$@" 2>&1 || echo "(command failed: $*)"
}

# ---------------------------------------------------------------- identity ---
section "IDENTITY"
cmd date
cmd uname -a
file /proc/cmdline
file /etc/os-release
file /proc/device-tree/model
file /sys/firmware/devicetree/base/model

# --------------------------------------------------------------------- CPU ---
section "CPU"
file /proc/cpuinfo
file /sys/devices/system/cpu/online
file /sys/devices/system/cpu/possible
file /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies
file /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors
file /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq
file /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq
sub "current frequencies"
for c in /sys/devices/system/cpu/cpu[0-9]*; do
    [ -d "$c/cpufreq" ] || continue
    printf '%s: %s kHz\n' "$(basename "$c")" \
        "$(cat "$c/cpufreq/scaling_cur_freq" 2>/dev/null || echo '?')"
done

# ------------------------------------------------------------------ memory ---
section "MEMORY"
file /proc/meminfo
cmd free -m
file /proc/swaps
sub "ZRAM"
for z in /sys/block/zram*; do
    [ -d "$z" ] || continue
    printf '%s:\n' "$(basename "$z")"
    for a in disksize comp_algorithm mm_stat stat; do
        printf '  %-14s %s\n' "$a" \
            "$(cat "$z/$a" 2>/dev/null || echo '(none)')"
    done
done

# ----------------------------------------------------------------- storage ---
section "STORAGE"
file /proc/partitions
cmd mount
cmd df -h
sub "block devices (/sys/class/block)"
printf '%-12s %-14s %-10s %-3s %-9s\n' NAME SIZE_SECTORS SIZE_MB RO REMOVABLE
for d in /sys/class/block/*; do
    [ -d "$d" ] || continue
    n=$(basename "$d")
    s=$(cat "$d/size" 2>/dev/null || echo 0)
    mb=$(awk "BEGIN { printf \"%.1f\", $s / 2048 }" 2>/dev/null || echo '?')
    printf '%-12s %-14s %-10s %-3s %-9s\n' "$n" "$s" "$mb" \
        "$(cat "$d/ro" 2>/dev/null || echo '?')" \
        "$(cat "$d/removable" 2>/dev/null || echo '?')"
done

# ------------------------------------------------------------- framebuffer ---
section "FRAMEBUFFER / DISPLAY"
file /proc/fb
for f in /sys/class/graphics/fb*; do
    [ -d "$f" ] || continue
    sub "$(basename "$f")"
    for a in name virtual_size stride bits_per_pixel state blank rotate modes mode; do
        if [ -r "$f/$a" ]; then
            printf '%-18s %s\n' "$a" "$(cat "$f/$a" 2>/dev/null)"
        fi
    done
done
sub "DRM / KMS"
if [ -d /sys/class/drm ]; then
    ls -l /sys/class/drm 2>/dev/null
else
    echo "(no /sys/class/drm)"
fi
if [ -e /dev/dri ]; then
    ls -l /dev/dri
else
    echo "(no /dev/dri)"
fi

# ------------------------------------------------------------------ input ---
section "INPUT"
file /proc/bus/input/devices
sub "evdev capabilities"
for e in /sys/class/input/event*; do
    [ -d "$e" ] || continue
    printf '\n[%s]\n' "$(basename "$e")"
    printf 'name:  %s\n' "$(cat "$e/device/name" 2>/dev/null || echo '?')"
    printf 'props: %s\n' "$(cat "$e/device/properties" 2>/dev/null || echo '?')"
    for cap in ev key rel abs msc sw led snd ff; do
        v=$(cat "$e/device/capabilities/$cap" 2>/dev/null || true)
        [ -n "$v" ] && printf '%-4s %s\n' "$cap:" "$v"
    done
done
sub "tools"
if command -v evtest >/dev/null 2>&1; then
    echo "evtest: $(command -v evtest)"
else
    echo "evtest: not installed (install it in the Alpine chroot if needed)"
fi

# ------------------------------------------------------------------- wi-fi ---
section "WI-FI / FIRMWARE"
cmd ip -o link
cmd ip -o addr
if command -v iw >/dev/null 2>&1; then
    cmd iw dev
else
    echo "(iw not installed)"
fi
file /proc/net/wireless
sub "mwifiex / firmware dmesg"
dmesg 2>/dev/null | grep -iE 'mwifiex|sd8787|marvell|firmware' | tail -n 60 \
    || echo "(no matching dmesg lines)"
sub "firmware files"
ls -l /lib/firmware/mrvl 2>/dev/null || echo "(no /lib/firmware/mrvl)"
md5sum /lib/firmware/mrvl/* 2>/dev/null || true
ls -l /lib/firmware/regulatory.db /lib/firmware/regulatory.db.p7s 2>/dev/null \
    || echo "(no regulatory.db)"

# ------------------------------------------------- audio / power / thermal ---
section "AUDIO, POWER, THERMAL, MISC"
file /proc/asound/cards
cmd ls -l /dev/snd
file /sys/power/state
sub "power supplies"
for p in /sys/class/power_supply/*; do
    [ -d "$p" ] || continue
    printf '%s:\n' "$(basename "$p")"
    for a in type status present online capacity voltage_now temp; do
        [ -r "$p/$a" ] && printf '  %-12s %s\n' "$a" "$(cat "$p/$a" 2>/dev/null)"
    done
done
sub "thermal zones"
for t in /sys/class/thermal/thermal_zone*; do
    [ -d "$t" ] || continue
    printf '%s: type=%s temp=%s\n' "$(basename "$t")" \
        "$(cat "$t/type" 2>/dev/null || echo '?')" \
        "$(cat "$t/temp" 2>/dev/null || echo '?')"
done
file /sys/class/rtc/rtc0/name

# ------------------------------------------------------------- device nodes ---
section "DEVICE NODES THAT MATTER TO ANDROID"
for d in /dev/binder /dev/ashmem /dev/ion /dev/dri /dev/fb0 /dev/fb1 \
         /dev/input /dev/uinput /dev/loop-control /dev/loop0 /dev/sw_sync \
         /dev/sync /dev/video0 /dev/snd /dev/ttyS0; do
    if [ -e "$d" ]; then
        ls -ld "$d"
    else
        printf '%-20s missing\n' "$d"
    fi
done

# --------------------------------------------------------- config audit ------
section "ANDROID KERNEL CONFIG AUDIT"
CFG=/tmp/vita-config.$$
if [ -r /proc/config.gz ]; then
    gzip -dc /proc/config.gz > "$CFG" 2>/dev/null \
        || zcat /proc/config.gz > "$CFG" 2>/dev/null
elif [ -r "/boot/config-$(uname -r)" ]; then
    cat "/boot/config-$(uname -r)" > "$CFG"
fi

if [ -s "$CFG" ]; then
    printf '%-38s %s\n' OPTION STATE
    for o in \
        ANDROID_BINDER_IPC ANDROID_BINDERFS ANDROID_BINDER_DEVICES \
        ASHMEM SW_SYNC SYNC_FILE DMABUF_HEAPS ION \
        SECURITY SECURITY_SELINUX SECURITY_SELINUX_BOOTPARAM SECURITY_NETWORK LSM \
        CGROUPS CGROUP_SCHED CGROUP_CPUACCT CGROUP_FREEZER MEMCG \
        FUSE_FS SDCARD_FS \
        NETFILTER_XT_MATCH_OWNER NETFILTER_XT_MATCH_QTAGUID \
        NETFILTER_XT_TARGET_CT NF_CONNTRACK IP_NF_IPTABLES IP6_NF_IPTABLES \
        TMPFS_XATTR EXT4_FS_SECURITY BLK_DEV_LOOP DEVTMPFS DEVTMPFS_MOUNT \
        FB_SIMPLE FRAMEBUFFER_CONSOLE INPUT_EVDEV INPUT_UINPUT \
        VT UNIX98_PTYS MODULES IKCONFIG_PROC \
        ZRAM ZSMALLOC LZ4_COMPRESS USB_SUPPORT USB_GADGET \
        BINFMT_ELF COREDUMP AIO KALLSYMS MAGIC_SYSRQ
    do
        if grep -qE "^CONFIG_${o}=" "$CFG"; then
            v=$(grep -E "^CONFIG_${o}=" "$CFG" | head -n1)
            printf '%-38s %s\n' "CONFIG_$o" "${v#CONFIG_${o}=}"
        elif grep -qE "^# CONFIG_${o} is not set" "$CFG"; then
            printf '%-38s %s\n' "CONFIG_$o" "n"
        else
            printf '%-38s %s\n' "CONFIG_$o" "(not in config)"
        fi
    done
    rm -f "$CFG"
else
    echo "(no kernel config available; CONFIG_IKCONFIG_PROC should be y)"
fi

# ---------------------------------------------------------------- dmesg ------
section "DMESG (last 120 lines; full log via vita-diagnostics)"
dmesg 2>/dev/null | tail -n 120 || echo "(dmesg unavailable)"

section "DONE"
echo "Report complete. Also run: vita-diagnostics"
echo "It writes vita-diagnostics.txt and vita-diagnostics.tar.gz to ux0: and ur0:."
