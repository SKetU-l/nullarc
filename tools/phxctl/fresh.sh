#!/bin/bash
# usage: fresh.sh "<remote cmd>" -> reboot Mac, wait for kext, run command
SSH_USER="${MAC_SSH_USER:-admin}"
SSH_PASS="${MAC_SSH_PASS:-}"
IP="${MAC_IP:-192.168.1.100}"
O="-o StrictHostKeyChecking=no -o ConnectTimeout=10"

if [ -n "$SSH_PASS" ]; then
    sshpass -p "$SSH_PASS" ssh $O "$SSH_USER@$IP" "echo '$SSH_PASS' | sudo -S -p '' shutdown -r now" >/dev/null 2>&1
else
    ssh $O "$SSH_USER@$IP" "sudo shutdown -r now" >/dev/null 2>&1
fi
sleep 75
for i in $(seq 1 45); do
    if [ -n "$SSH_PASS" ]; then
        sshpass -p "$SSH_PASS" ssh $O "$SSH_USER@$IP" 'ioreg -l | grep -c "phx-build"' 2>/dev/null | grep -q '^[1-9]' && break
    else
        ssh $O "$SSH_USER@$IP" 'ioreg -l | grep -c "phx-build"' 2>/dev/null | grep -q '^[1-9]' && break
    fi
    sleep 8
done
if [ -n "$SSH_PASS" ]; then
    sshpass -p "$SSH_PASS" ssh $O "$SSH_USER@$IP" "$1"
else
    ssh $O "$SSH_USER@$IP" "$1"
fi
