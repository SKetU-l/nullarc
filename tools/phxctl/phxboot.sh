#!/bin/bash
# Once per boot (LaunchDaemon local.phxboot): GPU bring-up, the Metal accelerator claim and, when M2 is on, the display
# pipe arming and one WindowServer restart. Fail-safe: a boot whose previous attempt never finished skips everything
# and turns itself off (the marker survives a hang); the kill switch is the file /Users/Shared/gpu/phxboot-off.
LOG=/var/log/phxboot.log; MARK=/var/db/phxboot.pending; OFF=/Users/Shared/gpu/phxboot-off
exec >>"$LOG" 2>&1
echo "== $(date '+%F %T') phxboot"
[ -e "$OFF" ] && { echo "kill switch present: nothing to do"; exit 0; }
if [ -e "$MARK" ]; then echo "previous attempt did not finish: turning phxboot off"; touch "$OFF"; rm -f "$MARK"; exit 0; fi
for i in $(seq 1 60); do ioreg -l | grep -q '"phx-build"' && break; sleep 2; done
ioreg -l | grep -q '"phx-build"' || { echo "PhxBringup not loaded"; exit 0; }
touch "$MARK"; sync
MC=1 CS=1 ADOPT=1 GFX=1 AG=1 bash /Users/Shared/gpu/full.sh kiqsched | grep -E "^compute:|^gfx:|adopt"
/Users/Shared/gpu/phxctl/mesboot claim
sleep 3
if [ -e /Users/Shared/gpu/phxboot-m2 ]; then   # M2: WindowServer composites on the 780M
    sysctl -w debug.nvaccelfb=1; sleep 1; sysctl -w debug.nvaccel_iop=1; sysctl -w debug.nvaccelfb=3; sleep 1
    killall WindowServer
fi
rm -f "$MARK"; sync
echo "done"
