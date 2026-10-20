#!/bin/bash
# Once per boot (LaunchDaemon local.phxboot): GPU bring-up and the Metal accelerator claim; when PhxAMDFB took the panel
# (M2) also the display arming, before WindowServer opens the panel. PhxAMDFB holds the GPU busy from boot until this
# script releases it (or its 120 s timer does), so GPUWrangler publishes the GPU with its accelerator and WindowServer
# composites on the 780M from its first start. Fail-safes: a boot whose previous attempt never finished skips everything,
# turns itself off and leaves PhxAMDFB off (the marker survives a hang); WindowServer crashing 3 times in 90 s is taken
# off the GPU; NVRAM phxfb-boot is armed again only after a good M2 boot and only while $G/phxboot-m2 exists.
# Kill switch: the file $G/phxboot-off.
G=/Users/Shared/gpu; export HOME=/Users/Shared
LOG=/var/log/phxboot.log; MARK=/var/db/phxboot.pending; OFF=$G/phxboot-off
ALLOW=/Library/GPUBundles/nvmtl-allow.txt; DR=/Library/Logs/DiagnosticReports
exec >>"$LOG" 2>&1
echo "== $(date '+%F %T') phxboot"
release() { sysctl -w debug.phxfb_hold=0 >/dev/null 2>&1; }
apps_only() { printf '# phxboot: Metal for the built-in test tools only\n' > $ALLOW; }
# PhxAMDFB without the arming leaves the panel black: give up on M2 (until $G/phxboot-m2 is put back) and reboot into
# the firmware framebuffer
m2_fail() {
    echo "M2 failed ($1): PhxAMDFB off, rebooting"
    apps_only; sysctl -w debug.phxagdc=0 >/dev/null 2>&1
    nvram -d phxfb-boot; rm -f "$MARK" $G/phxboot-m2; sync; shutdown -r now; exit 0
}
# PhxAMDFB comes from the Aux KC, which loads well after PhxBringup: wait for the panel's framebuffer (PhxAMDFB, or
# IONDRV when PhxAMDFB was not armed) before telling which one took it
wait_fb() { for i in $(seq 1 90); do ioreg -rc IOFramebuffer -d0 | grep -q 'class ' && break; sleep 1; done; }
if [ -e "$OFF" ]; then echo "kill switch present: nothing to do"; release; exit 0; fi
if [ -e "$MARK" ]; then
    echo "previous attempt did not finish: phxboot and PhxAMDFB off"
    touch "$OFF"; rm -f "$MARK"; nvram -d phxfb-boot; sync
    wait_fb; ioreg -rc PhxAMDFB -d0 | grep -q PhxAMDFB && { echo "PhxAMDFB took the panel: rebooting"; shutdown -r now; }
    release; exit 0
fi
for i in $(seq 1 60); do ioreg -l | grep -q '"phx-build"' && break; sleep 1; done
wait_fb
M2=0; ioreg -rc PhxAMDFB -d0 | grep -q PhxAMDFB && M2=1
ioreg -l | grep -q '"phx-build"' || { [ $M2 = 1 ] && m2_fail "PhxBringup not loaded"; echo "PhxBringup not loaded"; exit 0; }
echo "PhxAMDFB on the panel: $M2"
touch "$MARK"; sync
apps_only
MC=1 CS=1 ADOPT=1 GFX=1 AG=1 bash $G/full.sh kiqsched | grep -E "^compute:|^gfx:|adopt"
$G/phxctl/mesboot claim
# no BO cache flushing per submit/wait (the GPU maps system memory snooped): 41 fps window drag instead of 13, and no
# artifacts seen (2026-10-20)
sysctl -w debug.phxbos_flush=0 >/dev/null 2>&1
for i in $(seq 1 20); do ioreg -rc NMXAccel -d0 | grep -q NMXAccel && break; sleep 1; done
if ! ioreg -rc NMXAccel -d0 | grep -q NMXAccel; then
    [ $M2 = 1 ] && m2_fail "NMXAccel did not start"
    echo "NMXAccel did not start"; release; rm -f "$MARK"; exit 0
fi
if [ $M2 = 1 ]; then
    sysctl -w debug.nvaccelfb=1 >/dev/null; sleep 1
    sysctl -w debug.nvaccel_iop=1 >/dev/null; sysctl -w debug.nvaccelfb=3 >/dev/null; sleep 1
    sysctl -w debug.phxagdc=1 >/dev/null; sleep 1
    printf '# phxboot: WindowServer composites on the 780M (armed only)\n!WindowServer\n' > $ALLOW
    # WindowServer's process starts early but opens the panel only once GPUWrangler publishes the GPU, which the busy
    # hold delays. A panel opened before the arming (the hold timed out) stays black, and a restarted WindowServer then
    # asserts in CoreDisplay_CreateDisplayForCGXDisplayDevice and hangs: no way back but a reboot.
    ioreg -rc PhxAMDFB -d2 | grep -q IOFramebufferUserClient && m2_fail "WindowServer opened the panel before the arming"
    # crash reports and watchdog spins (WindowServer_..._userspace_watchdog_timeout.spin) both count
    before=$(ls $DR | grep -c '^WindowServer[-_]')
    release
    echo "armed and released at $(date '+%T'); agdc $(sysctl -n debug.phxagdc)"
    for ((t = 0; t < 90; t += 5)); do
        sleep 5
        n=$(( $(ls $DR | grep -c '^WindowServer[-_]') - before ))
        [ $n -ge 3 ] && m2_fail "WindowServer crashed or hung $n times on the 780M"
    done
    pgrep -qx WindowServer || m2_fail "no WindowServer"
    echo "WindowServer pid $(pgrep -x WindowServer), frames on the panel $(sysctl -n debug.nvaccel_iop_ok)"
else
    release
fi
[ -e $G/phxboot-m2 ] && nvram phxfb-boot=1 && echo "PhxAMDFB armed for the next boot"
rm -f "$MARK"; sync
echo "done $(date '+%T')"
