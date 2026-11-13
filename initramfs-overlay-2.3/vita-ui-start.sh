#!/system/bin/sh
# Wait for the mediaserver to publish its binder services, then start zygote.
#
# Android 2.3's system_server connects to media.audio_flinger and
# media.player while AudioService starts.  `start media` only forks the
# process; it does not wait for those services to appear in servicemanager.
# Starting zygote in the same property action raced that registration on the
# Vita, so system_server could see a half-started media service.  This helper
# gives media a short, bounded head start and still starts zygote even if media
# never comes up, so a media failure cannot leave the UI disabled.

PATH=/bin:/sbin:/system/bin:/system/xbin
export PATH

log() {
    echo "vita-ui-start: $*" > /dev/kmsg 2>/dev/null
}

state=$(getprop init.svc.media 2>/dev/null)
i=0
while [ "$state" != "running" ] && [ "$i" -lt 15 ]; do
    sleep 1
    i=$((i + 1))
    state=$(getprop init.svc.media 2>/dev/null)
done

if [ "$state" = "running" ]; then
    # mediaserver is up.  AudioFlinger, MediaPlayerService and
    # AudioPolicyService register from its main thread just after fork;
    # two seconds is enough on the Vita and keeps boot latency low.
    sleep 2
    log "media is running after ${i}s; starting zygote"
else
    log "media did not reach running after ${i}s (state=${state:-unknown}); starting zygote anyway"
fi

setprop ctl.start zygote
