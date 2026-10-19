#!/bin/bash
# usage: fresh.sh "<remote cmd>"  -> reboot Mac, wait for kext, run command
O="-o UserKnownHostsFile=/home/luuumi-dev/.ssh/known_hosts_firebat_mac -o StrictHostKeyChecking=no -o ConnectTimeout=10"
IP=192.168.31.240
sshpass -p PASSWORD ssh $O luuumi-dev@$IP 'eval $SUDO_CMD shutdown -r now' >/dev/null 2>&1; sleep 75
for i in $(seq 1 45); do sshpass -p PASSWORD ssh $O luuumi-dev@$IP 'ioreg -l | grep -c "phx-build\" = 12"' 2>/dev/null | grep -q '^[1-9]' && break; sleep 8; done
sshpass -p PASSWORD ssh $O luuumi-dev@$IP "$1"
