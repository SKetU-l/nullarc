#!/bin/bash
# Run the offscreen smoke test with a reboot watchdog; no WindowServer arming.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
G=/Users/Shared/gpu
args=$(nvram boot-args 2>/dev/null | cut -f2-)
case " $args " in *" nullarc_test=1 "*) ;; *) echo "not the NullArc test entry"; exit 1;; esac
echo "Reboot watchdog: 4 minutes. A failed/hung test leaves it armed."
nohup shutdown -r +4 >/dev/null 2>&1 &
if ! "$G/phxctl/mtlprobe" 2>&1 | tee /var/log/nullarc-metal-test.log; then
    echo "Test failed; watchdog left armed. Do not enable WindowServer."
    exit 1
fi
killall shutdown 2>/dev/null || true
echo "Smoke test passed; watchdog cancelled. App/stability testing is still required."
