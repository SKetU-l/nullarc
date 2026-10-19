#!/bin/bash
# full.sh — Automated bringup & execution harness
SUDO_CMD="sudo"
if [ -n "$SUDO_PASS" ]; then
    SUDO_CMD="echo '$SUDO_PASS' | sudo -S -p ''"
fi

bash ~/gpu/bringup5.sh >/dev/null 2>&1
eval $SUDO_CMD bash -c "nohup shutdown -r +4 >/dev/null 2>&1 &"
eval $SUDO_CMD ~/gpu/phxctl/phxctl wreg 0xDE0 1 | tail -1
eval $SUDO_CMD ~/gpu/phxctl/phxctl wreg 0xA802 0 | tail -1
eval $SUDO_CMD ~/gpu/phxctl/phxctl wreg 0xA803 0x01000000 | tail -1
eval $SUDO_CMD ~/gpu/phxctl/mesboot gart
eval $SUDO_CMD ~/gpu/phxctl/mesboot mes
eval $SUDO_CMD env MESBOOT_MC=$MC MESBOOT_CS=$CS MESBOOT_VMT=$VMT PTE_SYS=$PS PTE_NOFB=$NF FAULT_DEFAULT=$FD PT_SYS=$PTS VARONLY=$VAR PTE_EXTRA=$PX ROOT_FL=$RF AGPTEST=$AGP CTX0_OFF=$C0 VM8=$V8 VM8_MT=$VMT8 IB0=$IB0 VM8_FLAT=$FLAT VM8_SYS=$VSYS ADOPT=$ADOPT VM8_IBSYS=$IBSYS VM8_HI=$VHI ~/gpu/phxctl/mesboot "$1" 2>&1 | tail -12
eval $SUDO_CMD killall shutdown 2>/dev/null
